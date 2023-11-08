AC_PREREQ([2.71])
AC_INIT([libecs-cpp],[0.4.0],[tim@metaverse.systems],[libecs-cpp],[https://metaverse.systems])
LT_INIT
AC_CONFIG_SRCDIR([src/Manager.cpp])
AC_CONFIG_HEADERS([config.h])

PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

AC_ARG_ENABLE([builtin-uuid],
	AS_HELP_STRING([--enable-builtin-uuid],
		[Use builtin uuid implementation instead of libuuid [default=yes]]),
	[builtin_uuid="${enableval}"])

AM_CONDITIONAL([BUILTIN_UUID], [test "$builtin_uuid" != "no"])

AS_IF([test "$builtin_uuid" != "no" ], [
    AC_DEFINE([USE_BUILTIN_UUID], [1], [Define if using builtin uuid.])
])

PKG_CHECK_MODULES([UUID], uuid, [], [dont_abort_configure="yes"])
PKG_CHECK_MODULES([CATCH2], catch2)

AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([ecs-cpp.pc])
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT

