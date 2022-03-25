#include <dlfcn.h>
#include <link.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */

#define ORIGINAL_FUNC(name) get_next<decltype(&::name)>(#name)
#define DECLARE_FUNC(name) decltype(&::name) s_##name = &::name;

#define errExit(msg)                                                           \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

#define PERF_SAMPLE_STACK_SIZE (4096 * 8)
#define PERF_REGS_COUNT 20

struct Sample {
  char stack[PERF_SAMPLE_STACK_SIZE];
  size_t stack_size;
  uint64_t regs[PERF_REGS_COUNT];
};

struct RingBuffer {
  Sample samples[32];
};

namespace {
constexpr const char *shmpath = "/ddprof_ringbuffer";

// void setup_shared_mem() {
//   int fd = shm_open(shmpath, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
//   if (fd == -1)
//     errExit("shm_open");

//   if (ftruncate(fd, sizeof(RingBuffer)) == -1)
//     errExit("ftruncate");

//   /* Map the object into the caller's address space. */

//   struct shmbuf *shmp =
//       mmap(NULL, sizeof(*shmp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
// }

template <typename F> F get_next(const char *name) {
  auto *func = reinterpret_cast<F>(dlsym(RTLD_NEXT, name));
  // fprintf(stderr, "Intercepting %s\n", name);
  assert(func);
  return func;
}

DECLARE_FUNC(malloc);
DECLARE_FUNC(calloc);
DECLARE_FUNC(realloc);
DECLARE_FUNC(free);

static std::atomic<bool> s_init;
static std::mutex s_mutex;

void init();

struct TempAlloc {
  static const constexpr size_t kMaxSize = 1024;
  char buffer[kMaxSize];
  size_t allocated = 0;

  void *allocate(size_t size) {
    if (allocated + size >= kMaxSize) {
      abort();
    }
    void *ptr = buffer + allocated;
    allocated += size;
    return ptr;
  }

  bool owns(void *ptr) const {
    return ptr >= buffer && ptr < buffer + kMaxSize;
  }
};

TempAlloc &get_temp_alloc() {
  static TempAlloc temp_alloc;
  return temp_alloc;
}

void *temp_calloc(size_t nmemb, size_t size) noexcept {
  return get_temp_alloc().allocate(nmemb * size);
}

inline void check_init() {
  if (!s_init.load()) {
    init();
  }
}

void init() {
  {
    std::lock_guard<std::mutex> lock{s_mutex};
    if (s_init.load())
      return;
    s_calloc = &temp_calloc;
    s_init.store(true);
  }
  s_malloc = ORIGINAL_FUNC(malloc);
  s_calloc = ORIGINAL_FUNC(calloc);
  s_free = ORIGINAL_FUNC(free);
  s_realloc = ORIGINAL_FUNC(realloc);
}

using ElfAddr = ElfW(Addr);
using ElfDyn = ElfW(Dyn);
using ElfSym = ElfW(Sym);
using ElfRel = ElfW(Rel);
using ElfRela = ElfW(Rela);

static size_t allocated_size;
static size_t profiler_rate = 512 * 1024;
static bool do_print = getenv("DDPRINT");

void report_malloc(size_t size) {
    allocated_size += size;
    if (allocated_size >= profiler_rate) {
        if (do_print) printf("%ld allocated bytes\n", allocated_size);
        allocated_size = 0;
    }    
}

void *mymalloc(size_t size) noexcept {
  static bool first = [] { printf("myalloc\n"); return true; }();  
  report_malloc(size);
  return malloc(size);
}

void myfree(void *ptr) noexcept { static bool first = [] { printf("myfree\n"); return true; }();  free(ptr); }

void *mycalloc(size_t nmemb, size_t size) noexcept {
  static bool first = [] { printf("mycalloc\n"); return true; }();  
  report_malloc(nmemb*size);
  return calloc(nmemb, size);
}

void *myrealloc(void *ptr, size_t size) noexcept { static bool first = [] { printf("myrealloc\n"); return true; }(); report_malloc(size);  return realloc(ptr, size); }

void write(ElfAddr addr, void *value) {
  auto page = reinterpret_cast<void *>(addr & ~(0x1000 - 1));
  mprotect(page, 0x1000, PROT_READ | PROT_WRITE);
  memcpy((void *)addr, &value, sizeof(void *));
}

template <typename RelType>
void process_rels(const RelType *rel_start, const RelType *rel_end,
                  const char *strings, const ElfSym *symbols,
                  const ElfAddr base) noexcept {
  for (auto rel = rel_start; rel < rel_end; ++rel) {
    auto index = ELF64_R_SYM(rel->r_info);
    auto symname = strings + symbols[index].st_name;
    auto addr = rel->r_offset + base;
    if (strcmp(symname, "malloc") == 0) {
      printf("Found malloc at 0x%lx\n", addr);
      write(addr, (void *)&mymalloc);
    } else if (strcmp(symname, "calloc") == 0) {
      printf("Found calloc at 0x%lx\n", addr);
      write(addr, (void *)&mycalloc);
    } else if (strcmp(symname, "realloc") == 0) {
      printf("Found realloc at 0x%lx\n", addr);
      write(addr, (void *)&myrealloc);
    } else if (strcmp(symname, "free") == 0) {
      printf("Found free at 0x%lx\n", addr);
      write(addr, (void *)&myfree);
    }
  }
}

int process_lib(dl_phdr_info *info, size_t /*size*/, void *data) noexcept {
  printf("Processing %s\n", info->dlpi_name);
  if (strstr(info->dlpi_name, "/libdd_allocation_profiling.so") ||
      strstr(info->dlpi_name, "/ld-linux") ||
      strstr(info->dlpi_name, "linux-vdso")) {
    return 0;
  }

  for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end;
       ++phdr) {
    if (phdr->p_type == PT_DYNAMIC) {
      const ElfSym *symbols = nullptr;
      size_t symbols_size = 0;
      const char *strings = nullptr;
      size_t strings_size;
      const ElfRela *jmprels;
      size_t jmprels_size;
      const ElfRela *relarels;
      size_t relarels_size;
      const ElfRel *relrels;
      size_t relrels_size;
      for (ElfDyn *dyn =
               reinterpret_cast<ElfDyn *>(phdr->p_vaddr + info->dlpi_addr);
           dyn->d_tag; dyn++) {
        switch (dyn->d_tag) {
        case DT_SYMTAB:
          symbols = reinterpret_cast<ElfSym *>(dyn->d_un.d_ptr);
          break;
        case DT_SYMENT:
          symbols_size = dyn->d_un.d_val;
          break;
        case DT_STRTAB:
          strings = reinterpret_cast<const char *>(dyn->d_un.d_ptr);
          break;
        case DT_STRSZ:
          strings_size = dyn->d_un.d_val;
          break;
        case DT_JMPREL:
          jmprels = reinterpret_cast<ElfRela *>(dyn->d_un.d_ptr);
          break;
        case DT_PLTRELSZ:
          jmprels_size = dyn->d_un.d_val;
          break;
        case DT_RELA:
          relarels = reinterpret_cast<ElfRela *>(dyn->d_un.d_ptr);
          break;
        case DT_RELASZ:
          relarels_size = dyn->d_un.d_val;
          break;
        case DT_REL:
          relrels = reinterpret_cast<ElfRel *>(dyn->d_un.d_ptr);
          break;
        case DT_RELSZ:
          relrels_size = dyn->d_un.d_val;
          break;
        }
      }
      if (jmprels) {
        process_rels(jmprels, jmprels + jmprels_size / sizeof(*jmprels),
                     strings, symbols, info->dlpi_addr);
      }
      if (relrels) {
        process_rels(relrels, relrels + relrels_size / sizeof(*relrels),
                     strings, symbols, info->dlpi_addr);
      }
      if (relarels) {
        process_rels(relarels, relarels + relarels_size / sizeof(*relarels),
                     strings, symbols, info->dlpi_addr);
      }
    }
  }
  return 0;
}

int override_got() {
  printf("Overriding GOT\n");
  return dl_iterate_phdr(&process_lib, nullptr);
}

} // namespace

#ifdef USE_PRELOAD
extern "C" {
void *malloc(size_t size) {
  check_init();
  return s_malloc(size);
}

void free(void *ptr) {
  check_init();
  return s_free(ptr);
}

void *calloc(size_t nmemb, size_t size) {
  check_init();
  return s_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size) {
  check_init();
  return s_realloc(ptr, size);
}
}
#else

namespace {
int dummy = override_got();
}
#endif