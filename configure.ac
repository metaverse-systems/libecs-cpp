AC_PREREQ([2.71])
AC_INIT([libecs-cpp],[1.1.2],[tim@metaverse.systems],[libecs-cpp],[https://metaverse.systems])
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
PKG_CHECK_MODULES([CATCH2], [catch2-with-main])

AC_ARG_ENABLE([sanitizer],
	AS_HELP_STRING([--enable-sanitizer=@<:@no|address|thread@:>@],
		[Build the library and tests with a sanitizer [default=no]]),
	[sanitizer="${enableval}"],
	[sanitizer="no"])

AS_CASE(["$sanitizer"],
	[no], [
		SANITIZER=none
		SANITIZER_CXXFLAGS=
		SANITIZER_LDFLAGS=
	],
	[address], [
		SANITIZER=address
		SANITIZER_CXXFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
		SANITIZER_LDFLAGS="-fsanitize=address,undefined"
	],
	[thread], [
		SANITIZER=thread
		SANITIZER_CXXFLAGS="-fsanitize=thread -g -O1"
		SANITIZER_LDFLAGS="-fsanitize=thread"
	],
	[AC_MSG_ERROR([invalid value '$sanitizer' for --enable-sanitizer; accepted values are: no, address, thread])])

AC_SUBST([SANITIZER])
AC_SUBST([SANITIZER_CXXFLAGS])
AC_SUBST([SANITIZER_LDFLAGS])

AM_CONDITIONAL([CROSS_COMPILING], [test "$cross_compiling" = yes])

AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([ecs-cpp.pc])
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT

