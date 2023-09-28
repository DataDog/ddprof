// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "elfutils.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <functional>
#include <span>
#include <sys/mman.h>
#include <unistd.h>

#ifndef __ELF_NATIVE_CLASS
#  define __ELF_NATIVE_CLASS 64
#endif

namespace ddprof {
namespace {
// NOLINTBEGIN(readability-magic-numbers)
uint32_t gnu_hash(std::string_view name) {
  uint32_t h = 5381;

  for (auto c : name) {
    h = (h << 5) + h + c;
  }

  return h;
}

uint32_t elf_hash(std::string_view name) {
  uint32_t h = 0;
  for (auto c : name) {
    h = (h << 4) + c;
    uint32_t const g = h & 0xf0000000;
    h ^= g >> 24;
  }
  return h & 0x0fffffff;
}
// NOLINTEND(readability-magic-numbers)

bool check(const ElfW(Sym) & sym, const char *symname, std::string_view name) {
  auto stt = ELF64_ST_TYPE(sym.st_info);

  if (sym.st_value == 0 && sym.st_shndx != SHN_ABS && stt != STT_TLS
      //|| (type_class & (sym->st_shndx == SHN_UNDEF))))
  ) {
    return false;
  }

  if (((1 << stt) &
       ((1 << STT_NOTYPE) | (1 << STT_OBJECT) | (1 << STT_FUNC) |
        (1 << STT_COMMON) | (1 << STT_TLS) | (1 << STT_GNU_IFUNC))) == 0) {
    return false;
  }

  return name == symname;
}

struct DynamicInfo {
  std::string_view strtab;
  std::span<const ElfW(Sym)> symtab;
  std::span<const ElfW(Rel)> rels;
  std::span<const ElfW(Rela)> relas;
  std::span<const ElfW(Rela)> jmprels;
  const uint32_t *elf_hash;
  const uint32_t *gnu_hash;
  ElfW(Addr) base;
};

DynamicInfo retrieve_dynamic_info(const ElfW(Dyn) * dyn_begin,
                                  // cppcheck-suppress unknownMacro
                                  ElfW(Addr) base) {

  // Addresses are sometimes relative sometimes absolute
  // * on musl, addresses are relative
  // * on glic, addresses are absolutes
  // (https://elixir.bootlin.com/glibc/glibc-2.36/source/elf/get-dynamic-info.h#L84)
  auto correct_address = [base](ElfW(Addr) ptr) {
    return ptr > base ? ptr : base + ptr;
  };

  const char *strtab = nullptr;
  size_t strtab_size = 0;
  const ElfW(Sym) *symtab = nullptr;
  const ElfW(Rel) *rels = nullptr;
  size_t rels_size = 0;
  const ElfW(Rela) *relas = nullptr;
  size_t relas_size = 0;
  const ElfW(Rela) *jmprels = nullptr;
  size_t jmprels_size = 0;
  const uint32_t *elf_hash = nullptr;
  const uint32_t *gnu_hash = nullptr;
  ElfW(Sword) pltrel_type = 0;

  for (const auto *it = dyn_begin; it->d_tag != DT_NULL; ++it) {
    switch (it->d_tag) {
    case DT_STRTAB:
      strtab = reinterpret_cast<const char *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_STRSZ:
      strtab_size = it->d_un.d_val;
      break;
    case DT_SYMTAB:
      symtab =
          reinterpret_cast<const ElfW(Sym) *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_HASH:
      // \fixme{nsavoire} Avoid processing DT_HASH since it sometimes points in
      // kernel address range on Centos 7...

      // elf_hash =
      //     reinterpret_cast<const uint32_t
      //     *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_GNU_HASH:
      gnu_hash =
          reinterpret_cast<const uint32_t *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_REL:
      rels =
          reinterpret_cast<const ElfW(Rel) *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_RELA:
      relas =
          reinterpret_cast<const ElfW(Rela) *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_JMPREL:
      jmprels =
          reinterpret_cast<const ElfW(Rela) *>(correct_address(it->d_un.d_ptr));
      break;
    case DT_RELSZ:
      rels_size = it->d_un.d_val;
      break;
    case DT_RELASZ:
      relas_size = it->d_un.d_val;
      break;
    case DT_PLTRELSZ:
      jmprels_size = it->d_un.d_val;
      break;
    case DT_PLTREL:
      pltrel_type = it->d_un.d_val;
      break;
    default:
      break;
    }
  }

  if (pltrel_type != DT_RELA) {
    jmprels = nullptr;
    jmprels_size = 0;
  }

  uint32_t sym_count = gnu_hash
      ? gnu_hash_symbol_count(gnu_hash)
      : (elf_hash ? elf_hash_symbol_count(elf_hash) : 0);

  return {.strtab = {strtab, strtab_size},
          .symtab = {symtab, sym_count},
          .rels = {rels, rels_size / sizeof(ElfW(Rel))},
          .relas = {relas, relas_size / sizeof(ElfW(Rela))},
          .jmprels = {jmprels, jmprels_size / sizeof(ElfW(Rela))},
          .elf_hash = elf_hash,
          .gnu_hash = gnu_hash,
          .base = base};
}

template <typename F>
int callback_wrapper(dl_phdr_info *info, size_t /*size*/, bool exclude_self,
                     const F &func) {
  const ElfW(Phdr) *phdr_dynamic = nullptr;

  for (auto phdr = info->dlpi_phdr, end = phdr + info->dlpi_phnum; phdr != end;
       ++phdr) {
    if (phdr->p_type == PT_DYNAMIC) {
      phdr_dynamic = phdr;
    }
    // Exclude this DSO
    if (exclude_self && phdr->p_type == PT_LOAD && phdr->p_flags & PF_X) {
      ElfW(Addr) local_symbol_addr =
          reinterpret_cast<ElfW(Addr)>(&retrieve_dynamic_info);
      if (phdr->p_vaddr + info->dlpi_addr <= local_symbol_addr &&
          local_symbol_addr < phdr->p_vaddr + info->dlpi_addr + phdr->p_memsz) {
        return 0;
      }
    }
  }

  if (phdr_dynamic) {
    const ElfW(Dyn) *dyn_begin = reinterpret_cast<const ElfW(Dyn) *>(
        info->dlpi_addr + phdr_dynamic->p_vaddr);

    DynamicInfo const dyn_info =
        retrieve_dynamic_info(dyn_begin, info->dlpi_addr);

    if (dyn_info.strtab.empty() || dyn_info.symtab.empty() ||
        !(dyn_info.elf_hash || dyn_info.gnu_hash)) {
      return 0;
    }

    if (func(info->dlpi_name, dyn_info)) {
      return 1;
    }
  }

  return 0;
}

struct CallbackWrapperBase {
  using Fun = int (*)(dl_phdr_info *info, size_t size, void *data);
  Fun fun;
  bool exclude_self;
};

template <typename F> struct CallbackWrapper : CallbackWrapperBase {
  F callback;
};

int dl_iterate_hdr_callback(dl_phdr_info *info, size_t size, void *data) {
  CallbackWrapperBase *callback = reinterpret_cast<CallbackWrapperBase *>(data);
  return callback->fun(info, size, callback);
}

template <typename F>
int dl_iterate_phdr_wrapper(F callback, bool exclude_self = false) {
  CallbackWrapper<F> my_callback{
      {[](dl_phdr_info *info, size_t size, void *data) {
         auto *wrapper = static_cast<CallbackWrapper<F> *>(data);
         return callback_wrapper(info, size, wrapper->exclude_self,
                                 wrapper->callback);
       },
       exclude_self},
      std::move(callback),
  };
  return dl_iterate_phdr(dl_iterate_hdr_callback, &my_callback);
}

class SymbolLookup {
public:
  explicit SymbolLookup(std::string_view symname, bool accept_null_sized_symbol,
                        uint64_t not_sym = 0)
      : _symname(symname), _not_sym(not_sym), _sym{},
        _accept_null_sized_symbol(accept_null_sized_symbol) {}

  bool operator()(std::string_view /*object_name*/,
                  const DynamicInfo &dyn_info) {
    const ElfW(Sym) *s = nullptr;
    if (dyn_info.gnu_hash) {
      s = gnu_hash_lookup(dyn_info.strtab.data(), dyn_info.symtab.data(),
                          dyn_info.gnu_hash, _symname);
    } else if (dyn_info.elf_hash) {
      s = elf_hash_lookup(dyn_info.strtab.data(), dyn_info.symtab.data(),
                          dyn_info.elf_hash, _symname);
    }
    if (s && (_accept_null_sized_symbol || s->st_size > 0) &&
        (s->st_value + dyn_info.base != _not_sym)) {
      _sym = *s;
      _sym.st_value = s->st_value + dyn_info.base;
      return true;
    }
    return false;
  }

  ElfW(Sym) result() const { return _sym; }

private:
  std::string_view _symname;
  uint64_t _not_sym;
  ElfW(Sym) _sym;
  bool _accept_null_sized_symbol;
};

void override_entry(ElfW(Addr) entry_addr, uint64_t new_value) {
  static long const page_size = sysconf(_SC_PAGESIZE);
  auto *aligned_addr = reinterpret_cast<void *>(entry_addr & ~(page_size - 1));
  if (mprotect(aligned_addr, page_size, PROT_READ | PROT_WRITE) == 0) {
    memcpy(reinterpret_cast<void *>(entry_addr), &new_value, sizeof(new_value));
  }
}

class SymbolOverride {
public:
  explicit SymbolOverride(std::string_view symname, uint64_t new_symbol,
                          uint64_t do_not_override_this_symbol)
      : _symname(symname), _new_symbol(new_symbol),
        _do_not_override_this_symbol(do_not_override_this_symbol) {}

  template <typename Reloc>
  void process_relocation(Reloc &reloc, const DynamicInfo &dyn_info) const {
    auto index = ELF64_R_SYM(reloc.r_info);
    // \fixme{nsavoire} size of symtab seems incorrect on CentOS 7
    auto symname =
        dyn_info.strtab.data() + dyn_info.symtab.data()[index].st_name;
    auto addr = reloc.r_offset + dyn_info.base;
    if (symname == _symname && addr != _do_not_override_this_symbol) {
      override_entry(addr, _new_symbol);
    }
  }

  bool operator()(std::string_view object_name,
                  const DynamicInfo &dyn_info) const {
    if (object_name.find("linux-vdso") != std::string_view::npos ||
        object_name.find("/ld-linux") != std::string_view::npos) {
      return false;
    }

    std::for_each(dyn_info.rels.begin(), dyn_info.rels.end(),
                  [&](auto &rel) { process_relocation(rel, dyn_info); });
    std::for_each(dyn_info.relas.begin(), dyn_info.relas.end(),
                  [&](auto &rel) { process_relocation(rel, dyn_info); });
    std::for_each(dyn_info.jmprels.begin(), dyn_info.jmprels.end(),
                  [&](auto &rel) { process_relocation(rel, dyn_info); });
    return false;
  }

private:
  std::string_view _symname;
  uint64_t _new_symbol = 0;
  uint64_t _do_not_override_this_symbol = 0;
};

int count_callback(dl_phdr_info * /*info*/, size_t /*size*/, void *data) {
  ++(*reinterpret_cast<int *>(data));
  return 0;
}

} // namespace

// https://flapenguin.me/elf-dt-hash
const ElfW(Sym) *
    elf_hash_lookup(const char *strtab, const ElfW(Sym) * symtab,
                    const uint32_t *hashtab, std::string_view symname) {
  const uint32_t hash = elf_hash(symname);

  const uint32_t nbuckets = *(hashtab++);
  ++hashtab;
  const uint32_t *buckets = hashtab;
  hashtab += nbuckets;
  const uint32_t *chain = hashtab;

  for (auto symidx = buckets[hash % nbuckets]; symidx != STN_UNDEF;
       symidx = chain[symidx]) {
    const auto &sym = symtab[symidx];
    if (check(sym, strtab + sym.st_name, symname)) {
      return &sym;
    }
  }
  return nullptr;
}

// https://flapenguin.me/elf-dt-gnu-hash
const ElfW(Sym) *
    gnu_hash_lookup(const char *strtab, const ElfW(Sym) * symtab,
                    const uint32_t *hashtab, std::string_view symname) {
  const uint32_t nbuckets = *(hashtab++);
  const uint32_t symbias = *(hashtab++);
  const uint32_t bloom_size = *(hashtab++);
  const uint32_t bloom_shift = *(hashtab++);
  const ElfW(Addr) *bloom = reinterpret_cast<const ElfW(Addr) *>(hashtab);
  hashtab += __ELF_NATIVE_CLASS / (CHAR_BIT * sizeof(uint32_t)) * bloom_size;
  const uint32_t *buckets = hashtab;
  hashtab += nbuckets;
  const uint32_t *chain_zero = hashtab - symbias;

  if (nbuckets == 0) {
    return nullptr;
  }

  const uint32_t hash = gnu_hash(symname);

  ElfW(Addr) bitmask_word =
      bloom[(hash / __ELF_NATIVE_CLASS) & (bloom_size - 1)];
  uint32_t const hashbit1 = hash & (__ELF_NATIVE_CLASS - 1);
  uint32_t const hashbit2 = (hash >> bloom_shift) & (__ELF_NATIVE_CLASS - 1);

  if (!((bitmask_word >> hashbit1) & (bitmask_word >> hashbit2) & 1)) {
    return nullptr;
  }

  uint32_t symidx = buckets[hash % nbuckets];
  if (symidx == 0) {
    return nullptr;
  }

  while (true) {
    const uint32_t h = chain_zero[symidx];
    if (((h ^ hash) >> 1) == 0) {
      const auto &sym = symtab[symidx];
      if (check(sym, strtab + sym.st_name, symname)) {
        return &sym;
      }
    }

    if (h & 1) {
      break;
    }

    ++symidx;
  }

  return nullptr;
}

uint32_t elf_hash_symbol_count(const uint32_t *hashtab) { return hashtab[1]; }

uint32_t gnu_hash_symbol_count(const uint32_t *hashtab) {
  const uint32_t nbuckets = *(hashtab++);
  const uint32_t symbias = *(hashtab++);
  const uint32_t bloom_size = *(hashtab++);
  ++hashtab;
  hashtab += __ELF_NATIVE_CLASS / (sizeof(uint32_t) * CHAR_BIT) * bloom_size;
  const uint32_t *buckets = hashtab;
  hashtab += nbuckets;
  const uint32_t *chain_zero = hashtab - symbias;

  if (nbuckets == 0) {
    return 0;
  }
  uint32_t idx = *std::max_element(buckets, buckets + nbuckets);
  while (!(chain_zero[idx] & 1)) {
    ++idx;
  }
  return idx + 1;
}

ElfW(Sym) lookup_symbol(std::string_view symbol_name,
                        bool accept_null_sized_symbol, void *not_this_symbol) {
  SymbolLookup lookup{symbol_name, accept_null_sized_symbol,
                      reinterpret_cast<uint64_t>(not_this_symbol)};
  dl_iterate_phdr_wrapper(std::ref(lookup));
  return lookup.result();
}

void override_symbol(std::string_view symbol_name, void *new_symbol,
                     void *do_not_override_this_symbol) {
  SymbolOverride symbol_override{
      symbol_name, reinterpret_cast<uint64_t>(new_symbol),
      reinterpret_cast<uint64_t>(do_not_override_this_symbol)};
  dl_iterate_phdr_wrapper(std::ref(symbol_override));
}

int count_loaded_libraries() {
  int count = 0;
  dl_iterate_phdr(&count_callback, &count);
  return count;
}
} // namespace ddprof
