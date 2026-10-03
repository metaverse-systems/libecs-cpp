ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libecs-cpp.la
pkginclude_HEADERS = \
	../include/libecs-cpp/ecs.hpp \
        ../include/libecs-cpp/Manager.hpp \
	../include/libecs-cpp/Container.hpp \
	../include/libecs-cpp/System.hpp \
	../include/libecs-cpp/Component.hpp \
        ../include/libecs-cpp/Entity.hpp \
	../include/libecs-cpp/Uuid.hpp \
	../include/libecs-cpp/Resource.hpp \
	../include/libecs-cpp/json.hpp \
	../include/libecs-cpp/Clock.hpp \
	../include/libecs-cpp/Timing.hpp

# Internal header: shipped in the source distribution, never installed.
noinst_HEADERS = Validation.hpp

libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall $(SANITIZER_CXXFLAGS)

libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp Uuid.cpp
libecs_cpp_la_LDFLAGS = -no-undefined -pthread $(SANITIZER_LDFLAGS)

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I../include -Werror -Wall $(SANITIZER_CXXFLAGS)
example_LDADD = libecs-cpp.la
example_LDFLAGS = -pthread $(SANITIZER_LDFLAGS)

# Headers that earlier versions installed and this version no longer ships.
# Removing them on install stops a consumer from picking up stale copies.
retired_headers = uuid_v4.h endianness.h

install-data-hook:
	for h in $(retired_headers); do rm -f "$(DESTDIR)$(pkgincludedir)/$$h"; done
