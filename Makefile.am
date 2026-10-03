pkgconfig_DATA = ecs-cpp.pc
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

EXTRA_DIST = README.md ecs-cpp.pc.in Doxyfile.in LICENSE package.json

# The unpacked archive is read-only: do not let its configure step rerun the autotools.
AM_DISTCHECK_CONFIGURE_FLAGS = --disable-maintainer-mode
