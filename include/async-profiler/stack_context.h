

#pragma once

#include <cstddef>
#include <span>

namespace ap { 
struct StackContext {
    const void* pc;
    uintptr_t sp;
    uintptr_t fp;

    void set(const void* pc, uintptr_t sp, uintptr_t fp) {
        this->pc = pc;
        this->sp = sp;
        this->fp = fp;
    }
};

struct StackBuffer {
    StackBuffer(std::span<std::byte> bytes, uint64_t start, uint64_t end): 
        _bytes(bytes), sp_start(start), sp_end(end) {}
    std::span<std::byte> _bytes;
    uint64_t sp_start; // initial SP (in context of the process)
    uint64_t sp_end; // sp + size (so root functions = start of stack)
};

}
