// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "symbol_hdr.hpp"

#include "logger.hpp"

#include <datadog/profiling.h>
#include <new>

namespace ddprof {

ProfilesDictionary::ProfilesDictionary() {
  ::ddog_prof_Status status = ::ddog_prof_ProfilesDictionary_new(&_handle);
  if (status.err != nullptr) {
    LG_ERR("Failed to create ProfilesDictionary (OOM): %s", status.err);
    ::ddog_prof_Status_drop(&status);
    throw std::bad_alloc{};
  }
}

ProfilesDictionary::~ProfilesDictionary() { reset(); }

void ProfilesDictionary::reset() {
  if (_handle) {
    ::ddog_prof_ProfilesDictionary_drop(&_handle);
    _handle = nullptr;
  }
}

SymbolHdr::SymbolHdr(std::string_view path_to_proc)
    : _runtime_symbol_lookup(path_to_proc) {}

} // namespace ddprof
