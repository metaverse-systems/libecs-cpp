// Helper program for run-logging.sh. It is not a Catch2 test and is not run by the test wrapper.
//
// Creates a world with the default log destination and logs one line at each of four severities and one
// unknown severity, then exits with status 0. The script redirects the program's output and error streams
// in different ways and inspects what arrives.

#include <libecs-cpp/ecs.hpp>

int main()
{
    ecs::Manager manager;
    auto world = manager.Container("probe");

    world->Log("probe line one", "debug");
    world->Log("probe line two", "info");
    world->Log("probe line three", "warning");
    world->Log("probe line four", "error");
    world->Log("probe line five", "unrecognised");

    return 0;
}
