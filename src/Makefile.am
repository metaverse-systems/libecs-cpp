ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libecs-cpp.la
pkginclude_HEADERS = \
	../include/libecs-cpp/ecs.hpp \
        ../include/libecs-cpp/Manager.hpp \
	../include/libecs-cpp/Container.hpp \
	../include/libecs-cpp/System.hpp \
	../include/libecs-cpp/Component.hpp

libecs_cpp_la_CXXFLAGS = -std=c++17 -I../include $(UUID_CFLAGS)
libecs_cpp_la_LIBADD = -lpthread $(UUID_LIBS)
libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp
libecs_cpp_la_LDFLAGS = -no-undefined
