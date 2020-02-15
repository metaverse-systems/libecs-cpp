ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libecs-cpp.la
pkginclude_HEADERS = \
	../include/libecs-cpp/ecs.hpp \
        ../include/libecs-cpp/Manager.hpp \
	../include/libecs-cpp/Container.hpp \
	../include/libecs-cpp/System.hpp \
	../include/libecs-cpp/Component.hpp \
        ../include/libecs-cpp/Entity.hpp \
    ../include/libecs-cpp/Uuid.hpp

libecs_cpp_la_CXXFLAGS = -std=c++17 -I../include $(UUID_CFLAGS) $(JSONCPP_CFLAGS)
libecs_cpp_la_LIBADD = -lpthread $(UUID_LIBS) $(JSONCPP_LIBS)
libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp Uuid.cpp
libecs_cpp_la_LDFLAGS = -no-undefined

bin_PROGRAMS = example
example_SOURCES = example.cpp
example_CXXFLAGS = -std=c++17 -I../include -Werror -Wall $(UUID_CFLAGS) $(JSONCPP_CFLAGS)
example_LDADD = -lpthread libecs-cpp.la $(UUID_LIBS) $(JSONCPP_LIBS)
