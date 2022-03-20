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
	../include/libecs-cpp/Resource.hpp

libecs_cpp_la_CXXFLAGS = -std=c++20 -I../include -Werror -Wall
libecs_cpp_la_LIBADD = -lpthread
libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp Uuid.cpp
libecs_cpp_la_LDFLAGS = -no-undefined

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++20 -I../include -Werror -Wall
example_LDADD = -lpthread libecs-cpp.la
