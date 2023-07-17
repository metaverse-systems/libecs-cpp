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
	../include/libecs-cpp/endianness.h

if BUILTIN_UUID
libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall -Wno-psabi -mssse3 -mavx2
libecs_cpp_la_LIBADD = -lpthread
else
libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall -Wno-psabi ${UUID_CFLAGS}
libecs_cpp_la_LIBADD = -lpthread ${UUID_LIBS}
endif

libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp
libecs_cpp_la_LDFLAGS = -no-undefined

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I../include -Werror -Wall
example_LDADD = -lpthread libecs-cpp.la
