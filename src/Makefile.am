ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libecs-cpp.la
pkginclude_HEADERS = \
	../include/libecs-cpp/ecs.hpp \
        ../include/libecs-cpp/Manager.hpp \
	../include/libecs-cpp/Container.hpp \
	../include/libecs-cpp/System.hpp \
	../include/libecs-cpp/Component.hpp \
        ../include/libecs-cpp/Entity.hpp

libecs_cpp_la_CXXFLAGS = -std=c++14 -I../include $(UUID_CFLAGS) $(JSONCPP_CFLAGS)
libecs_cpp_la_LIBADD = -lpthread $(UUID_LIBS) $(JSONCPP_LIBS)
libecs_cpp_la_SOURCES = Manager.cpp Container.cpp System.cpp Component.cpp Entity.cpp
libecs_cpp_la_LDFLAGS = -no-undefined

bin_PROGRAMS = test
test_SOURCES = test.cpp
test_CXXFLAGS = -I../include/libecs-cpp -Werror -Wall $(UUID_CFLAGS) $(JSONCPP_CFLAGS)
test_LDADD = -lpthread libecs-cpp.la $(UUID_LIBS) $(JSONCPP_LIBS)
