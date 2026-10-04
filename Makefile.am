pkgconfig_DATA = ecs-cpp.pc
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

EXTRA_DIST = README.md GUIDE.md REFERENCE.md CONTRIBUTING.md NEWS.md ecs-cpp.pc.in Doxyfile.in LICENSE package.json

# The unpacked archive is read-only: do not let its configure step rerun the autotools.
AM_DISTCHECK_CONFIGURE_FLAGS = --disable-maintainer-mode

# Reference documentation, written to doxygen/html in the build directory. It is not part of all, check or
# install, and neither the generated site nor the generated Doxyfile is distributed.
.PHONY: doxygen
doxygen: Doxyfile
	@if test -z "$(DOXYGEN)"; then \
	  echo "make doxygen needs doxygen (and graphviz for the diagrams); install both and run configure again" >&2; \
	  exit 1; \
	fi
	$(DOXYGEN) Doxyfile

clean-local:
	rm -rf doxygen
