#include <cstring>
#include <ucontext.h>

#include "codeCache.h"
#include "stackWalker.h"
#include "symbols.h"

struct CodeCacheArraySingleton {
    static CodeCacheArray *getInstance();
    static CodeCacheArray *instance;
};

CodeCacheArray *CodeCacheArraySingleton::instance = nullptr;
CodeCacheArray *CodeCacheArraySingleton::getInstance() {
    // XXX(nick): I don't know that I need to care about concurrency. This
    // should be read-only once init() is called.
    if (instance == nullptr) {
        instance = new CodeCacheArray();
    }
    return instance;
}

static CodeBlob *asmcgocall_bounds = nullptr;
static uintptr_t asmcgocall_base = 0;

static __attribute__((constructor)) void init(void) {
    auto a = CodeCacheArraySingleton::getInstance();
    Symbols::parseLibraries(a, false);

    int count = a->count();
    for (int i = 0; i < count; i++) {
        CodeCache *c = a->operator[](i);
        const void *p = NULL;
        p = c->findSymbol("runtime.asmcgocall.abi0");
        if (p == nullptr) {
            // amscgocall name has "abi0" suffix on more recent Go versions
            // but not on older versions
            p = c->findSymbol("runtime.asmcgocall");
        }
        if (p == nullptr) {
            continue;
        }
        auto cb = c->find(p);
        if (cb != nullptr) {
            asmcgocall_bounds = cb;
            asmcgocall_base = (uintptr_t) c->getTextBase();
        }
    }
}   

void populateStackContext(ap::StackContext &sc, void *ucontext);

int stackWalk(CodeCacheArray *cache, ap::StackContext &sc, const void** callchain, int max_depth, int skip);
bool stepStackContext(ap::StackContext &sc, CodeCacheArray *cache);

extern "C"  {

static int enabled = 1;

// for benchmarking
void async_cgo_traceback_internal_set_enabled(int value) {
    enabled = value;
}

#define STACK_MAX 32

struct cgo_context {
    const void *pc;
    uintptr_t sp;
    uintptr_t fp;
    uintptr_t stack[STACK_MAX];
    int cached;
    int inuse;
};

// There may be multiple C->Go transitions for a single C tread, so we have a
// per-thread free list of contexts.
//
// Thread-local storage for the context list is safe. A context will be taken
// from the list when a C thread transitions to Go, and that context will be
// released as soon as the Go call returns. Thus the thread that the context
// came from will be alive the entire time the context is in use.
#define cgo_contexts_length 256
static __thread struct cgo_context cgo_contexts[cgo_contexts_length];

// XXX: The runtime.SetCgoTraceback docs claim that cgo_context can be called
// from a signal handler. I know in practice that doesn't happen but maybe it
// could in the future. If so, can we make sure that accessing this list of
// cgo_contexts is signal safe?

static struct cgo_context *cgo_context_get(void) {
    for (int i = 0; i < cgo_contexts_length; i++) {
        if (cgo_contexts[i].inuse == 0) {
            cgo_contexts[i].inuse = 1;
            cgo_contexts[i].cached = 0;
            return &cgo_contexts[i];
        }
    }
    return NULL;
}

static void cgo_context_release(struct cgo_context *c) {
    c->inuse = 0;
}

// truncate_asmcgocall truncates a call stack after asmcgocall, if asmcgocall is
// present in the stack. This function is the first function in the C call stack
// for a Go -> C call, and it is not the responsibility of this library to
// unwind past that function.
static void truncate_asmcgocall(void **stack, int size) {
    if (asmcgocall_bounds == nullptr) {
        return;
    }
    for (int i = 0; i < size; i++) {
        uintptr_t a = (uintptr_t) stack[i];
        a += asmcgocall_base;
        if ((a >= (uintptr_t) asmcgocall_bounds->_start) && (a <= (uintptr_t) asmcgocall_bounds->_end)) {
            if ((i + 1) < size) {
                // zero out the thing AFTER asmcgocall. We want to stop at
                // asmcgocall since that's the "top" of the C stack in a
                // Go -> C (-> Go) call
                stack[i + 1] = 0;
                return;
            }
        }
    }
}

struct cgo_context_arg {
    uintptr_t p;
};

#ifndef C_G_THING
void async_cgo_context(void *p) {
}
#else 
void async_cgo_context(void *p) {
    if (enabled == 0) {
        return;
    }

    cgo_context_arg *arg = (cgo_context_arg *)p;
    struct cgo_context *ctx = (struct cgo_context *) arg->p;
    if (ctx != NULL) {
        cgo_context_release(ctx);
        return;
    }
    ctx = cgo_context_get();
    if (ctx == NULL) {
        return;
    }
    ap::StackContext sc;
    populateStackContext(sc, nullptr);
    CodeCacheArray *cache = (CodeCacheArraySingleton::getInstance());
    // There are two frames in the call stack we should skip.  The first is this
    // function, and the second is _cgo_wait_runtime_init_done, which calls this
    // function to save the C call stack context before calling into Go code.
    // The next frame after that is the exported C->Go function, which is where
    // unwinding should begin for this context in the traceback function.
    stepStackContext(sc, cache);
    stepStackContext(sc, cache);
    ctx->pc = sc.pc;
    ctx->sp = sc.sp;
    ctx->fp = sc.fp;
    arg->p = (uintptr_t) ctx;
    return;
}
#endif


struct cgo_traceback_arg {
	uintptr_t  context;
	uintptr_t  sig_context;
	uintptr_t* buf;
	uintptr_t  max;
};

#ifndef C_GO_THINGS
void async_cgo_traceback(void *p) {
}
#else
void async_cgo_traceback(void *p) {
    if (enabled == 0) {
        return;
    }

    struct cgo_traceback_arg *arg = (struct cgo_traceback_arg *)p;
    struct cgo_context *ctx = NULL;
    ap::StackContext sc;

    // If we had a previous context, then we're being called to unwind some
    // previous C portion of a mixed C/Go call stack. We use the call stack
    // information saved in the context.
    if (arg->context != 0) {
        ctx = (struct cgo_context *) arg->context;
        if (ctx->cached == 0) {
            CodeCacheArray *cache = (CodeCacheArraySingleton::getInstance());
            sc.pc = ctx->pc;
            sc.sp = ctx->sp;
            sc.fp = ctx->fp;
            int n = stackWalk(cache, sc, (const void **) ctx->stack, STACK_MAX, 0);
            truncate_asmcgocall((void **) ctx->stack, n);
            ctx->cached = 1;
        }
        uintptr_t n = (arg->max < STACK_MAX) ? arg->max : STACK_MAX;
        memcpy(arg->buf, ctx->stack, n * sizeof(uintptr_t));
        return;
    }

    populateStackContext(sc, (void *) arg->sig_context);
    CodeCacheArray *cache = (CodeCacheArraySingleton::getInstance());
    int n = stackWalk(cache, sc, (const void **) arg->buf, arg->max, 0);
    if (n < arg->max) {
        arg->buf[n] = 0;
    }
    truncate_asmcgocall((void **) arg->buf, n);

    return;
}
#endif


} // extern "C"