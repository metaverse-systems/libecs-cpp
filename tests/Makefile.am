AM_CPPFLAGS = ${CATCH2_CFLAGS} -I../include
AM_CXXFLAGS = -std=c++20 $(SANITIZER_CXXFLAGS)
AM_LDFLAGS = $(SANITIZER_LDFLAGS)

check_PROGRAMS = test_Manager test_System test_Entity test_Container test_Timing

TEST_EXTENSIONS = .sh
LOG_COMPILER = $(SHELL) $(srcdir)/run-test.sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
	ECS_SANITIZER='@SANITIZER@'; export ECS_SANITIZER; \
	ECS_KNOWN_GAPS_FILE='$(srcdir)/known-gaps.txt'; export ECS_KNOWN_GAPS_FILE; \
	ECS_TEST_PROGRAMS='$(check_PROGRAMS)'; export ECS_TEST_PROGRAMS; \
	ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:abort_on_error=0:print_summary=1'; export ASAN_OPTIONS; \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:print_summary=1'; export UBSAN_OPTIONS; \
	TSAN_OPTIONS='halt_on_error=0:exitcode=66:second_deadlock_stack=1:print_summary=1'; export TSAN_OPTIONS;

if !CROSS_COMPILING
TESTS = $(check_PROGRAMS) run-test-selftest.sh
else
check-local:
	@echo "cross-compiling for $(if $(host_alias),$(host_alias),$(host)): test programs were built but not run"
endif

EXTRA_DIST = run-test.sh run-test-selftest.sh known-gaps.txt

test_Manager_SOURCES = test_Manager.cpp
test_Manager_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_System_SOURCES = test_System.cpp
test_System_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Entity_SOURCES = test_Entity.cpp
test_Entity_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Timing_SOURCES = test_Timing.cpp
test_Timing_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Container_SOURCES = test_Container.cpp
test_Container_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Container_LDFLAGS = $(AM_LDFLAGS) -pthread
