AC_PREREQ([2.71])
AC_INIT([libecs-cpp],[2.0.0],[tim@metaverse.systems],[libecs-cpp],[https://metaverse.systems])

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
AC_CHECK_PROG([DOXYGEN], [doxygen], [doxygen])
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE([enable])

AC_ARG_ENABLE([tests],
	AS_HELP_STRING([--enable-tests=@<:@auto|yes|no@:>@],
		[Build the tests; they need Catch2 v3, pkg-config name catch2-with-main [default=auto]]),
	[tests="${enableval}"],
	[tests="auto"])

tests_disabled_by_option=no
AS_CASE(["$tests"],
	[no], [
		tests_enabled=no
		tests_disabled_by_option=yes
		tests_reason="--enable-tests=no"
	],
	[auto|yes], [
		dnl The lookup is written out instead of using PKG_CHECK_MODULES so that
		dnl nothing about the test framework is declared when the tests are off.
		AC_MSG_CHECKING([for the test framework (catch2-with-main)])
		AS_IF([$PKG_CONFIG --exists catch2-with-main],
			[
				AC_MSG_RESULT([yes])
				TEST_FRAMEWORK_CFLAGS=`$PKG_CONFIG --cflags catch2-with-main`
				TEST_FRAMEWORK_LIBS=`$PKG_CONFIG --libs catch2-with-main`
				tests_enabled=yes
				tests_reason="found"
			],
			[
				AC_MSG_RESULT([no])
				AS_IF([test "$tests" = yes],
					[AC_MSG_ERROR([--enable-tests=yes requires Catch2 v3 (pkg-config module catch2-with-main); install it (Debian/Ubuntu: apt install catch2) or use --enable-tests=auto])])
				tests_enabled=no
				tests_reason="not found"
			])
	],
	[AC_MSG_ERROR([invalid value '$tests' for --enable-tests; accepted values are: auto, yes, no])])


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

AC_ARG_ENABLE([werror],
	AS_HELP_STRING([--enable-werror=@<:@no|yes@:>@],
		[Treat compiler warnings as errors in the library, the sample and the tests [default=no]]),
	[werror="${enableval}"],
	[werror="no"])

AS_CASE(["$werror"],
	[no], [WERROR_CXXFLAGS=],
	[yes], [WERROR_CXXFLAGS=-Werror],
	[AC_MSG_ERROR([invalid value '$werror' for --enable-werror; accepted values are: no, yes])])

AC_SUBST([cross_compiling])
AC_SUBST([WERROR_CXXFLAGS])
AC_SUBST([TEST_FRAMEWORK_CFLAGS])
AC_SUBST([TEST_FRAMEWORK_LIBS])
AC_SUBST([SANITIZER])
AC_SUBST([SANITIZER_CXXFLAGS])
AC_SUBST([SANITIZER_LDFLAGS])

AM_CONDITIONAL([CROSS_COMPILING], [test "$cross_compiling" = yes])
AM_CONDITIONAL([ENABLE_TESTS], [test "$tests_enabled" = yes])
AM_CONDITIONAL([TESTS_DISABLED_BY_OPTION], [test "$tests_disabled_by_option" = yes])

AC_CONFIG_FILES([Doxyfile])
AC_CONFIG_FILES([ecs-cpp.pc])
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT

AC_MSG_NOTICE([tests: $tests_enabled ($tests_reason)])
AC_MSG_NOTICE([warnings as errors: $werror])
AC_MSG_NOTICE([sanitizer: $SANITIZER])

