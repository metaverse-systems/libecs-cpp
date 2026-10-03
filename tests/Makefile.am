AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src $(TEST_FRAMEWORK_CFLAGS)
AM_CXXFLAGS = -std=c++20 -Wall $(WERROR_CXXFLAGS) $(SANITIZER_CXXFLAGS)
AM_LDFLAGS = $(SANITIZER_LDFLAGS)

# The Catch2 programs are built and run only when the tests are enabled (configure --enable-tests).
# EXTRA_PROGRAMS keeps their per-program variables known to automake either way.
test_programs = test_Manager test_System test_Entity test_Container test_Timing test_Elapsed test_Compatibility test_UpdateAllocation test_Uuid test_Threading test_Validation test_Lifecycle test_ProcessManager test_Access test_Logging test_Resources test_Export

# Script tests that do not depend on the host; they run on cross builds too.
host_independent_tests = check-install.sh check-headers.sh check-consumer.sh check-docs.sh

TEST_EXTENSIONS = .sh
LOG_COMPILER = $(SHELL) $(srcdir)/run-test.sh
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
	ECS_SANITIZER='@SANITIZER@'; export ECS_SANITIZER; \
	ECS_KNOWN_GAPS_FILE='$(abs_srcdir)/known-gaps.txt'; export ECS_KNOWN_GAPS_FILE; \
	ECS_TEST_PROGRAMS='$(check_PROGRAMS)'; export ECS_TEST_PROGRAMS; \
	ECS_SRCDIR='$(abs_top_srcdir)'; export ECS_SRCDIR; \
	ECS_BUILDDIR='$(abs_top_builddir)'; export ECS_BUILDDIR; \
	ECS_EXAMPLE='$(abs_top_builddir)/src/example$(EXEEXT)'; export ECS_EXAMPLE; \
	ECS_LOG_PROBE='$(abs_builddir)/log_probe$(EXEEXT)'; export ECS_LOG_PROBE; \
	ECS_EXPORT_LIBRARY='$(abs_top_builddir)/src/.libs/libecs-cpp.so'; export ECS_EXPORT_LIBRARY; \
	ECS_EXPORT_INCLUDE='$(abs_top_srcdir)/include'; export ECS_EXPORT_INCLUDE; \
	ECS_PREFIX='$(prefix)'; export ECS_PREFIX; \
	ECS_VERSION='$(PACKAGE_VERSION)'; export ECS_VERSION; \
	ECS_CXX='$(CXX)'; export ECS_CXX; \
	ECS_SANITIZER_FLAGS='$(SANITIZER_CXXFLAGS) $(SANITIZER_LDFLAGS)'; export ECS_SANITIZER_FLAGS; \
	ECS_HOST_OS='@host_os@'; export ECS_HOST_OS; \
	ECS_CROSS='@cross_compiling@'; export ECS_CROSS; \
	ECS_MAKE='$(MAKE)'; export ECS_MAKE; \
	ASAN_OPTIONS='halt_on_error=1:detect_leaks=1:abort_on_error=0:print_summary=1'; export ASAN_OPTIONS; \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:print_summary=1'; export UBSAN_OPTIONS; \
	TSAN_OPTIONS='halt_on_error=0:exitcode=66:second_deadlock_stack=1:print_summary=1'; export TSAN_OPTIONS;

if ENABLE_TESTS
check_PROGRAMS = $(test_programs)
TESTS = $(host_independent_tests)
if !CROSS_COMPILING
TESTS += $(check_PROGRAMS) run-test-selftest.sh run-example.sh run-logging.sh check-exports-selftest.sh check-exports.sh check-style-selftest.sh check-style.sh
else
check-local:
	@echo "cross-compiling for $(if $(host_alias),$(host_alias),$(host)): test programs were built but not run"
endif
else
if TESTS_DISABLED_BY_OPTION
check-local:
	@echo "make check: the tests are not enabled: configured with --enable-tests=no; to run the tests install Catch2 (catch2-with-main; Debian/Ubuntu: apt install catch2) and configure with --enable-tests=auto" >&2; exit 1
else
check-local:
	@echo "make check: the tests are not enabled: Catch2 (catch2-with-main) was not found; install it (Debian/Ubuntu: apt install catch2) and run ./configure again" >&2; exit 1
endif
endif

EXTRA_DIST = check-install.sh check-headers.sh check-consumer.sh check-docs.sh run-test.sh run-test-selftest.sh run-example.sh run-logging.sh check-exports.sh check-exports-selftest.sh check-style.sh check-style-selftest.sh known-gaps.txt log_probe.cpp bench_update_pass.cpp bench_entity_creation.cpp bench_message_submit.cpp bench_component_get.cpp bench_uuid.cpp

# Helper program for run-logging.sh. It is not a Catch2 program: it stays out of check_PROGRAMS, TESTS and
# ECS_TEST_PROGRAMS, and is built before the script runs through the dependency on the script's log file.
EXTRA_PROGRAMS = log_probe $(test_programs)
CLEANFILES = log_probe$(EXEEXT)
log_probe_SOURCES = log_probe.cpp
log_probe_LDADD = $(top_builddir)/src/libecs-cpp.la
log_probe_LDFLAGS = $(AM_LDFLAGS) -pthread
run-logging.log: log_probe$(EXEEXT)

test_Manager_SOURCES = test_Manager.cpp
test_Manager_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)

test_System_SOURCES = test_System.cpp
test_System_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)

test_Entity_SOURCES = test_Entity.cpp
test_Entity_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)

test_Timing_SOURCES = test_Timing.cpp
test_Timing_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)

test_Container_SOURCES = test_Container.cpp
test_Container_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Container_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Elapsed_SOURCES = test_Elapsed.cpp
test_Elapsed_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Elapsed_LDFLAGS = $(AM_LDFLAGS) -pthread
test_Compatibility_SOURCES = test_Compatibility.cpp
test_Compatibility_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Compatibility_LDFLAGS = $(AM_LDFLAGS) -pthread

test_UpdateAllocation_SOURCES = test_UpdateAllocation.cpp
test_UpdateAllocation_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)

test_Uuid_SOURCES = test_Uuid.cpp
test_Uuid_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Uuid_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Threading_SOURCES = test_Threading.cpp
test_Threading_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Threading_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Validation_SOURCES = test_Validation.cpp
test_Validation_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Validation_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Lifecycle_SOURCES = test_Lifecycle.cpp
test_Lifecycle_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Lifecycle_LDFLAGS = $(AM_LDFLAGS) -pthread

test_ProcessManager_SOURCES = test_ProcessManager.cpp
test_ProcessManager_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_ProcessManager_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Access_SOURCES = test_Access.cpp
test_Access_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Access_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Logging_SOURCES = test_Logging.cpp
test_Logging_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Logging_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Resources_SOURCES = test_Resources.cpp
test_Resources_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Resources_LDFLAGS = $(AM_LDFLAGS) -pthread

test_Export_SOURCES = test_Export.cpp
test_Export_LDADD = $(top_builddir)/src/libecs-cpp.la $(TEST_FRAMEWORK_LIBS)
test_Export_LDFLAGS = $(AM_LDFLAGS) -pthread
