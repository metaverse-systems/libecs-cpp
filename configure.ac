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

AC_CHECK_HEADER(
  [windows.h],
  [UUID_LIBS="-lrpcrt4 $LIBS"; AC_SUBST([UUID_REQUIRE], "")],
  [PKG_CHECK_MODULES([UUID], uuid, AC_SUBST([UUID_REQUIRE], "uuid"))]
)
PKG_CHECK_MODULES([JSONCPP], jsoncpp)

AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([ecs-cpp.pc])
AC_OUTPUT(Makefile src/Makefile)

