ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libecs-cpp.la
pkginclude_HEADERS = \
	$(top_srcdir)/include/libecs-cpp/ecs.hpp \
	$(top_srcdir)/include/libecs-cpp/Manager.hpp \
	$(top_srcdir)/include/libecs-cpp/Container.hpp \
	$(top_srcdir)/include/libecs-cpp/System.hpp \
	$(top_srcdir)/include/libecs-cpp/Component.hpp \
	$(top_srcdir)/include/libecs-cpp/Entity.hpp \
	$(top_srcdir)/include/libecs-cpp/Uuid.hpp \
	$(top_srcdir)/include/libecs-cpp/Resource.hpp \
	$(top_srcdir)/include/libecs-cpp/json.hpp \
	$(top_srcdir)/include/libecs-cpp/Clock.hpp \
	$(top_srcdir)/include/libecs-cpp/Timing.hpp

# Internal header: shipped in the source distribution, never installed.
noinst_HEADERS = Validation.hpp ConsoleLog.hpp

libecs_cpp_la_CXXFLAGS = -std=c++20 -I$(top_srcdir)/include -Wall $(WERROR_CXXFLAGS) $(SANITIZER_CXXFLAGS)

libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Entity.cpp Uuid.cpp
# Binary-compatibility version, as current:revision:age.
#   - Library code changed, interface unchanged: raise revision.
#   - Interface added, old binaries still work: raise current and age, reset revision to zero.
#   - Interface removed or changed incompatibly (a type layout, a signature, a removed
#     name, an inline function that compiled code depends on): raise current, reset
#     revision and age to zero.
# The soname major is current minus age, so a binary built against an earlier
# incompatible library is refused by the loader. The expected file names in
# tests/check-install.sh change in the same commit as this value.
# It starts at 2:0:0 because the 1.x and 2.0.0 releases all shipped soname 0 while
# changing type layouts and removing names.
libecs_cpp_la_LDFLAGS = -version-info 2:0:0 -no-undefined -pthread $(SANITIZER_LDFLAGS)

noinst_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I$(top_srcdir)/include -Wall $(WERROR_CXXFLAGS) $(SANITIZER_CXXFLAGS)
example_LDADD = libecs-cpp.la
example_LDFLAGS = -pthread $(SANITIZER_LDFLAGS)

# Headers that earlier versions installed and this version no longer ships.
# Removing them on install stops a consumer from picking up stale copies.
retired_headers = uuid_v4.h endianness.h

install-data-hook:
	for h in $(retired_headers); do rm -f "$(DESTDIR)$(pkgincludedir)/$$h"; done
