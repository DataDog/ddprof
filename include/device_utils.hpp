// A more portable version is available here
// https://stackoverflow.com/questions/35392291/how-to-convert-between-a-dev-t-and-major-minor-device-numbers
#if defined(custom_makedev) && defined(custom_major) && defined(custom_minor)
/* Already defined */
#else

#  undef custom_makedev
#  undef custom_major
#  undef custom_minor

#  if defined(__linux__) || defined(__GLIBC__)
/* Linux, Android, and other systems using GNU C library */
#    include <sys/sysmacros.h>
#    define custom_makedev(dmajor, dminor) makedev(dmajor, dminor)
#    define custom_major(devnum) major(devnum)
#    define custom_minor(devnum) minor(devnum)
#  endif
#endif
