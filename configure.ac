AC_PREREQ([2.69])
AC_INIT([libecs-cpp], [0.0.1], [tim@metaverse.systems], [libecs-cpp], [https://metaverse.systems])
LT_INIT
AC_CONFIG_SRCDIR([src/Manager.cpp])
AC_CONFIG_HEADERS([config.h])

PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

PKG_CHECK_MODULES([UUID], uuid)
PKG_CHECK_MODULES([JSONCPP], jsoncpp)

test -z "$SED" && SED=sed

AC_ARG_ENABLE([gperftools],
  [AS_HELP_STRING([--enable-gperftools],
                  [enable gperftools for profiling])],
  [enable_gperftools=$enableval],
  [enable_gperftools=no]
)

if test "x$enable_gperftools" = xyes; then
  dnl Remove all optimization flags from CFLAGS
  changequote({,})
  CFLAGS=`echo "$CFLAGS" | $SED -e 's/-O[0-9s]*//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | $SED -e 's/-O[0-9s]*//g'`

  CFLAGS=`echo "$CFLAGS" | $SED -e 's/-g[0-9]*//g'`
  CXXFLAGS=`echo "$CXXFLAGS" | $SED -e 's/-g[0-9]*//g'`
  changequote([,])

  PKG_CHECK_MODULES([PROFILER], libprofiler)
  AC_SUBST(PROFILER_CFLAGS)
  AC_SUBST(PROFILER_LIBS)

  CFLAGS="$CFLAGS -g -O0 -DWITHGPERFTOOLS ${PROFILER_CFLAGS}"
  CXXFLAGS="$CXXFLAGS -g -O0 -DWITHGPERFTOOLS"

  LIBS="$LIBS ${PROFILER_LIBS}"
fi

AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([ecs-cpp.pc])
AC_OUTPUT(Makefile src/Makefile)

