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
	../include/libecs-cpp/uuid_v4.h \
	../include/libecs-cpp/endianness.h \
	../include/libecs-cpp/json.hpp \
    ../include/libecs-cpp/Timing.hpp

if BUILTIN_UUID
libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall -Wno-psabi -mssse3 -mavx2 $(SANITIZER_CXXFLAGS)
else
libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall -Wno-psabi ${UUID_CFLAGS} -DDISABLE_BUILTIN_UUID $(SANITIZER_CXXFLAGS)
libecs_cpp_la_LIBADD = ${UUID_LIBS}
endif

libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp Uuid.cpp
libecs_cpp_la_LDFLAGS = -no-undefined -pthread $(SANITIZER_LDFLAGS)

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I../include -Werror -Wall $(SANITIZER_CXXFLAGS)
example_LDADD = libecs-cpp.la
example_LDFLAGS = -pthread $(SANITIZER_LDFLAGS)
