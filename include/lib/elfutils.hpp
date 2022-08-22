// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <link.h>
#include <string_view>

namespace ddprof {

const ElfW(Sym) *
    gnu_hash_lookup(const char *strtab, const ElfW(Sym) * symtab,
                    const uint32_t *hashtab, std::string_view name);
const ElfW(Sym) *
    elf_hash_lookup(const char *strtab, const ElfW(Sym) * symtab,
                    const uint32_t *hashtab, std::string_view name);

uint32_t elf_hash_symbol_count(const uint32_t *hashtab);
uint32_t gnu_hash_symbol_count(const uint32_t *hashtab);

// Lookup first symbol matching `symbol_name` and different from
// `not_this_symbol`
ElfW(Sym)
    lookup_symbol(std::string_view symbol_name, bool accept_null_sized_symbol,
                  void *not_this_symbol = nullptr);

void override_symbol(std::string_view symbol_name, void *new_symbol);

} // namespace ddprof
