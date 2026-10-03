AM_CPPFLAGS = -I../include ${CATCH2_CFLAGS}
AM_CXXFLAGS = -std=c++20 $(SANITIZER_CXXFLAGS)
AM_LDFLAGS = $(SANITIZER_LDFLAGS)

check_PROGRAMS = test_Manager test_System test_Entity test_Container test_Timing test_Elapsed test_Compatibility test_UpdateAllocation test_Uuid test_Threading test_Validation test_Lifecycle test_ProcessManager test_Access test_Logging test_Resources test_Export

TEST_EXTENSIONS = .sh
LOG_COMPILER = $(SHELL) $(srcdir)/run-test.sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
	ECS_SANITIZER='@SANITIZER@'; export ECS_SANITIZER; \
	ECS_KNOWN_GAPS_FILE='$(srcdir)/known-gaps.txt'; export ECS_KNOWN_GAPS_FILE; \
	ECS_TEST_PROGRAMS='$(check_PROGRAMS)'; export ECS_TEST_PROGRAMS; \
	ECS_EXPORT_LIBRARY='../src/.libs/libecs-cpp.so'; export ECS_EXPORT_LIBRARY; \
	ECS_EXPORT_INCLUDE='$(top_srcdir)/include'; export ECS_EXPORT_INCLUDE; \
	ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:abort_on_error=0:print_summary=1'; export ASAN_OPTIONS; \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:print_summary=1'; export UBSAN_OPTIONS; \
	TSAN_OPTIONS='halt_on_error=0:exitcode=66:second_deadlock_stack=1:print_summary=1'; export TSAN_OPTIONS;

if !CROSS_COMPILING
TESTS = $(check_PROGRAMS) run-test-selftest.sh run-example.sh run-logging.sh check-exports-selftest.sh check-exports.sh
else
check-local:
	@echo "cross-compiling for $(if $(host_alias),$(host_alias),$(host)): test programs were built but not run"
endif

EXTRA_DIST = run-test.sh run-test-selftest.sh run-example.sh run-logging.sh check-exports.sh check-exports-selftest.sh known-gaps.txt bench_update_pass.cpp bench_entity_creation.cpp bench_message_submit.cpp bench_component_get.cpp

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

test_Elapsed_SOURCES = test_Elapsed.cpp
test_Elapsed_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Elapsed_LDFLAGS = $(AM_LDFLAGS) -pthread
test_Compatibility_SOURCES = test_Compatibility.cpp
test_Compatibility_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Compatibility_LDFLAGS = $(AM_LDFLAGS) -pthread

test_UpdateAllocation_SOURCES = test_UpdateAllocation.cpp
test_UpdateAllocation_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}

test_Uuid_SOURCES = test_Uuid.cpp
test_Uuid_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Uuid_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Threading_SOURCES = test_Threading.cpp
test_Threading_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Threading_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Validation_SOURCES = test_Validation.cpp
test_Validation_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Validation_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Lifecycle_SOURCES = test_Lifecycle.cpp
test_Lifecycle_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Lifecycle_LDFLAGS = $(AM_LDFLAGS) -pthread

test_ProcessManager_SOURCES = test_ProcessManager.cpp
test_ProcessManager_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_ProcessManager_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Access_SOURCES = test_Access.cpp
test_Access_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Access_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Logging_SOURCES = test_Logging.cpp
test_Logging_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Logging_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Resources_SOURCES = test_Resources.cpp
test_Resources_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Resources_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Export_SOURCES = test_Export.cpp
test_Export_LDADD = ../src/libecs-cpp.la ${CATCH2_LIBS}
test_Export_LDFLAGS = $(AM_LDFLAGS) -pthread
