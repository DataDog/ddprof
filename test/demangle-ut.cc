// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "demangler/demangler.hpp"

#include <gtest/gtest.h>

namespace ddprof {

struct DemangleTestContent {
  std::string test;
  std::string answer;
};

// Partly borrowed from the LLVM unit tests
std::vector<struct DemangleTestContent> s_demangle_cases = {
    {"_", "_"},
    {"_Z3fooi", "foo(int)"},
    {"_RNvC3foo3bar", "foo::bar"},
    {"_ZN4llvm4yaml7yamlizeISt6vectorINSt7__cxx1112basic_stringIcSt11char_"
     "traitsIcESaIcEEESaIS8_EENS0_12EmptyContextEEENSt9enable_ifIXsr18has_"
     "SequenceTraitsIT_EE5valueEvE4typeERNS0_2IOERSD_bRT0_",
     "std::enable_if<has_SequenceTraits<std::vector<std::__cxx11::basic_string<"
     "char, std::char_traits<char>, std::allocator<char> >, "
     "std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> > > > >::value, void>::type "
     "llvm::yaml::yamlize<std::vector<std::__cxx11::basic_string<char, "
     "std::char_traits<char>, std::allocator<char> >, "
     "std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> > > >, llvm::yaml::EmptyContext>(llvm::yaml::IO&, "
     "std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, "
     "std::char_traits<char>, std::allocator<char> > > >&, bool, "
     "llvm::yaml::EmptyContext&)"},
    {"_ZWowThisIsWrong", "_ZWowThisIsWrong"},

    // Following cases were taken by the libiberty project and are used here
    // with recognition
    {"_ZN4main4main17he714a2e23ed7db23E", "main::main"},
    {"_ZN4main4main17he714a2e23ed7db23E", "main::main"},
    {"_ZN4main4main18h1e714a2e23ed7db23E", "main::main::h1e714a2e23ed7db23"},
    {"_ZN4main4main16h714a2e23ed7db23E", "main::main::h714a2e23ed7db23"},
    {"_ZN4main4main17he714a2e23ed7db2gE", "main::main::he714a2e23ed7db2g"},
    {"_ZN4main4$99$17he714a2e23ed7db23E", "main::$99$"},
    {"_ZN71_$LT$Test$u20$$u2b$$u20$$u27$static$u20$as$u20$foo..Bar$LT$Test$GT$$"
     "GT$3bar17h930b740aa94f1d3aE",
     "<Test + 'static as foo::Bar<Test>>::bar"},
    {"_ZN54_$LT$I$u20$as$u20$core..iter..traits..IntoIterator$GT$9into_"
     "iter17h8581507801fb8615E",
     "<I as core::iter::traits::IntoIterator>::into_iter"},
    {"_ZN10parse_tsan4main17hdbbfdf1c6a7e27d9E", "parse_tsan::main"},
    {"_ZN65_$LT$std..env..Args$u20$as$u20$core..iter..iterator..Iterator$GT$"
     "4next17h420a7c8d0c7eef40E",
     "<std::env::Args as core::iter::iterator::Iterator>::next"},
    {"_ZN4core3str9from_utf817hdcea28871313776dE", "core::str::from_utf8"},
    {"_ZN4core3mem7size_of17h18bde9bb8c22e2cfE", "core::mem::size_of"},
    {"_ZN5alloc4heap8allocate17hd55c03e6cb81d924E", "alloc::heap::allocate"},
    {"_ZN4core3ptr8null_mut17h736cce09ca0ac11aE", "core::ptr::null_mut"},
    {"_ZN40_$LT$alloc..raw_vec..RawVec$LT$T$GT$$GT$6double17h4166e2b47539e1ffE",
     "<alloc::raw_vec::RawVec<T>>::double"},
    {"_ZN39_$LT$collections..vec..Vec$LT$T$GT$$GT$4push17hd4b6b23c1b88141aE",
     "<collections::vec::Vec<T>>::push"},
    {"_ZN70_$LT$collections..vec..Vec$LT$T$GT$$u20$as$u20$core..ops..DerefMut$"
     "GT$9deref_mut17hf299b860dc5a831cE",
     "<collections::vec::Vec<T> as core::ops::DerefMut>::deref_mut"},
    {"_ZN63_$LT$core..ptr..Unique$LT$T$GT$$u20$as$u20$core..ops..Deref$GT$"
     "5deref17hc784b4a166cb5e5cE",
     "<core::ptr::Unique<T> as core::ops::Deref>::deref"},
    {"_ZN40_$LT$alloc..raw_vec..RawVec$LT$T$GT$$GT$3ptr17h7570b6e9070b693bE",
     "<alloc::raw_vec::RawVec<T>>::ptr"},
    {"_ZN53_$LT$$u5b$T$u5d$$u20$as$u20$core..slice..SliceExt$GT$10as_mut_"
     "ptr17h153241df1c7d1666E",
     "<[T] as core::slice::SliceExt>::as_mut_ptr"},
    {"_ZN4core3ptr5write17h651fe53ec860e780E", "core::ptr::write"},
    {"_ZN65_$LT$std..env..Args$u20$as$u20$core..iter..iterator..Iterator$GT$"
     "4next17h420a7c8d0c7eef40E",
     "<std::env::Args as core::iter::iterator::Iterator>::next"},
    {"_ZN54_$LT$I$u20$as$u20$core..iter..traits..IntoIterator$GT$9into_"
     "iter17he06cb713aae5b465E",
     "<I as core::iter::traits::IntoIterator>::into_iter"},
    {"_ZN71_$LT$collections..vec..IntoIter$LT$T$GT$$u20$as$u20$core..ops..Drop$"
     "GT$4drop17hf7f23304ebe62eedE",
     "<collections::vec::IntoIter<T> as core::ops::Drop>::drop"},
    {"_ZN86_$LT$collections..vec..IntoIter$LT$T$GT$$u20$as$u20$core..iter.."
     "iterator..Iterator$GT$4next17h04b3fbf148c39713E",
     "<collections::vec::IntoIter<T> as core::iter::iterator::Iterator>::next"},
    {"_ZN75_$LT$$RF$$u27$a$u20$mut$u20$I$u20$as$u20$core..iter..iterator.."
     "Iterator$GT$4next17ha050492063e0fd20E",
     "<&'a mut I as core::iter::iterator::Iterator>::next"},
    {"_ZN13drop_contents17hfe3c0a68c8ad1c74E", "drop_contents"},
    {"_ZN13drop_contents17h48cb59bef15bb555E", "drop_contents"},
    {"_ZN4core3mem7size_of17h900b33157bf58f26E", "core::mem::size_of"},
    {"_ZN67_$LT$alloc..raw_vec..RawVec$LT$T$GT$$u20$as$u20$core..ops..Drop$GT$"
     "4drop17h96a5cf6e94807905E",
     "<alloc::raw_vec::RawVec<T> as core::ops::Drop>::drop"},
    {"_ZN68_$LT$core..nonzero..NonZero$LT$T$GT$$u20$as$u20$core..ops..Deref$GT$"
     "5deref17hc49056f882aa46dbE",
     "<core::nonzero::NonZero<T> as core::ops::Deref>::deref"},
    {"_ZN63_$LT$core..ptr..Unique$LT$T$GT$$u20$as$u20$core..ops..Deref$GT$"
     "5deref17h19f2ad4920655e85E",
     "<core::ptr::Unique<T> as core::ops::Deref>::deref"},
    {"_ZN11issue_609253foo37Foo$LT$issue_60925..llv$u6d$..Foo$GT$"
     "3foo17h059a991a004536adE",
     "issue_60925::foo::Foo<issue_60925::llvm::Foo>::foo"},
    {"_RNvC6_123foo3bar", "123foo::bar"},
    {"_RNqCs4fqI2P2rA04_11utf8_identsu30____7hkackfecea1cbdathfdh9hlq6y",
     "utf8_idents::საჭმელად_გემრიელი_სადილი"},
    {"_RNCNCNgCs6DXkGYLi8lr_2cc5spawn00B5_",
     "cc::spawn::{closure#0}::{closure#0}"},
    {"_RNCINkXs25_NgCsbmNqQUJIY6D_4core5sliceINyB9_4IterhENuNgNoBb_"
     "4iter8iterator8Iterator9rpositionNCNgNpB9_6memchr7memrchrs_0E0Bb_",
     "<core::slice::Iter<u8> as "
     "core::iter::iterator::Iterator>::rposition::<core::slice::memchr::"
     "memrchr::{closure#1}>::{closure#0}"},
    {"_RINbNbCskIICzLVDPPb_5alloc5alloc8box_freeDINbNiB4_"
     "5boxed5FnBoxuEp6OutputuEL_ECs1iopQbuBiw2_3std",
     "alloc::alloc::box_free::<dyn alloc::boxed::FnBox<(), Output = ()>>"},
    {"_RNvMC0INtC8arrayvec8ArrayVechKj7b_E3new",
     "<arrayvec::ArrayVec<u8, 123>>::new"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_8UnsignedKhb_E",
     "<const_generic::Unsigned<11>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_6SignedKs98_E",
     "<const_generic::Signed<152>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_6SignedKanb_E",
     "<const_generic::Signed<-11>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4BoolKb0_E",
     "<const_generic::Bool<false>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4BoolKb1_E",
     "<const_generic::Bool<true>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKc76_E",
     "<const_generic::Char<'v'>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKca_E",
     "<const_generic::Char<'\\n'>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKc2202_E",
     "<const_generic::Char<'\\u{2202}'>>"},
    {"_RNvNvMCs4fqI2P2rA04_13const_genericINtB4_3FooKpE3foo3FOO",
     "<const_generic::Foo<_>>::foo::FOO"},
    {"_RNvC6_123foo3bar", "123foo::bar"},
    {"_RNqCs4fqI2P2rA04_11utf8_identsu30____7hkackfecea1cbdathfdh9hlq6y",
     "utf8_idents::საჭმელად_გემრიელი_სადილი"},
    {"_RNCNCNgCs6DXkGYLi8lr_2cc5spawn00B5_",
     "cc::spawn::{closure#0}::{closure#0}"},
    {"_RNCINkXs25_NgCsbmNqQUJIY6D_4core5sliceINyB9_4IterhENuNgNoBb_"
     "4iter8iterator8Iterator9rpositionNCNgNpB9_6memchr7memrchrs_0E0Bb_",
     "<core::slice::Iter<u8> as "
     "core::iter::iterator::Iterator>::rposition::<core::slice::memchr::"
     "memrchr::{closure#1}>::{closure#0}"},
    {"_RINbNbCskIICzLVDPPb_5alloc5alloc8box_freeDINbNiB4_"
     "5boxed5FnBoxuEp6OutputuEL_ECs1iopQbuBiw2_3std",
     "alloc::alloc::box_free::<dyn alloc::boxed::FnBox<(), Output = ()>>"},
    {"_RNvMC0INtC8arrayvec8ArrayVechKj7b_E3new",
     "<arrayvec::ArrayVec<u8, 123>>::new"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_8UnsignedKhb_E",
     "<const_generic::Unsigned<11>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_6SignedKs98_E",
     "<const_generic::Signed<152>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_6SignedKanb_E",
     "<const_generic::Signed<-11>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4BoolKb0_E",
     "<const_generic::Bool<false>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4BoolKb1_E",
     "<const_generic::Bool<true>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKc76_E",
     "<const_generic::Char<'v'>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKca_E",
     "<const_generic::Char<'\\n'>>"},
    {"_RMCs4fqI2P2rA04_13const_genericINtB0_4CharKc2202_E",
     "<const_generic::Char<'\\u{2202}'>>"},
    {"_RNvNvMCs4fqI2P2rA04_13const_genericINtB4_3FooKpE3foo3FOO",
     "<const_generic::Foo<_>>::foo::FOO"},
};

#define BUF_LEN 1024
TEST(DemangleTest, Positive) {
  for (auto const &tcase : s_demangle_cases) {
    std::string demangled_func = Demangler::demangle(tcase.test);
    EXPECT_EQ(demangled_func, tcase.answer);
  }
}
} // namespace ddprof
