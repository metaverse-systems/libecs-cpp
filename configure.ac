AC_PREREQ([2.71])
AC_INIT([libecs-cpp],[1.4.0],[tim@metaverse.systems],[libecs-cpp],[https://metaverse.systems])

AC_ARG_ENABLE([builtin-uuid],
	AS_HELP_STRING([--enable-builtin-uuid],
		[Retired: has no effect; the portable identifier generator is always used]),
	[builtin_uuid="${enableval}"],
	[builtin_uuid=""])

AS_CASE(["$builtin_uuid"],
	[no], [
		AC_MSG_ERROR([--disable-builtin-uuid is no longer supported: libecs-cpp always uses its own portable identifier generator and no longer uses libuuid. Remove the option.])
	],
	[""], [],
	[
		AC_MSG_WARN([--enable-builtin-uuid has no effect and will be removed: the portable identifier generator is always used])
	])

LT_INIT
AC_CONFIG_SRCDIR([src/Manager.cpp])
AC_CONFIG_HEADERS([config.h])

PKG_PROG_PKG_CONFIG
PKG_INSTALLDIR

AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CXX
AM_INIT_AUTOMAKE([subdir-objects])

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

