#pragma once

#if defined(__has_builtin)
#  if __has_builtin(__builtin_expect)
#    define likely(x) __builtin_expect(!!(x), 1)
#    define unlikely(x) __builtin_expect(!!(x), 0)
#  else
#    define likely(x) (x)
#    define unlikely(x) (x)
#  endif
#else
#  define likely(x) (x)
#  define unlikely(x) (x)
#endif
