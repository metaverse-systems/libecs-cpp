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

libecs_cpp_la_CXXFLAGS = -std=c++20 -I$(top_srcdir)/include -Werror -Wall $(SANITIZER_CXXFLAGS)

libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Entity.cpp Uuid.cpp
libecs_cpp_la_LDFLAGS = -no-undefined -pthread $(SANITIZER_LDFLAGS)

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I$(top_srcdir)/include -Werror -Wall $(SANITIZER_CXXFLAGS)
example_LDADD = libecs-cpp.la
example_LDFLAGS = -pthread $(SANITIZER_LDFLAGS)

# Headers that earlier versions installed and this version no longer ships.
# Removing them on install stops a consumer from picking up stale copies.
retired_headers = uuid_v4.h endianness.h

install-data-hook:
	for h in $(retired_headers); do rm -f "$(DESTDIR)$(pkgincludedir)/$$h"; done
