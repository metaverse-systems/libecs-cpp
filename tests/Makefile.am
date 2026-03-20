AM_CPPFLAGS = ${CATCH2_CFLAGS} -I../include
AM_CXXFLAGS = -std=c++20

check_PROGRAMS = test_Manager test_System test_Entity test_Container

test_Manager_SOURCES = test_Manager.cpp
test_Manager_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_System_SOURCES = test_System.cpp
test_System_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Entity_SOURCES = test_Entity.cpp
test_Entity_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Container_SOURCES = test_Container.cpp
test_Container_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Container_LDFLAGS = -pthread
