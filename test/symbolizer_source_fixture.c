// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

__attribute__((visibility("default"))) int
symbolizer_source_fixture(int value) {
#line 100 "symbolizer-source-first.c"
  __asm__ volatile(".global symbolizer_source_first\nsymbolizer_source_first:");
  value += 1;
#line 200 "symbolizer-source-second.c"
  __asm__ volatile(
      ".global symbolizer_source_second\nsymbolizer_source_second:");
  value += 2;
  return value;
}
