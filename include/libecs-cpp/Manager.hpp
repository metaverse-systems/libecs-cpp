#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <libecs-cpp/json.hpp>

namespace ecs
{
    class Container;

    /*! Creates and owns containers, routes messages between them and shuts them down.
     *
     * A program uses either the process-wide manager, ECS, or a manager of its own.
     *
     * \par Threading
     * Every member is safe to call from any thread at any time, including from inside a system's
     * Update(). The destructor is the one exclusive operation: no other thread may use a manager that is
     * being destroyed, except through sends that were already in progress.
     *
     * \par Shutdown
     * A program that uses a manager calls Shutdown() from an application thread before the manager goes
     * away or main returns. The process-wide manager, ECS, is never destroyed, so for it the call is the
     * only thing that stops the container threads.
     */
    class Manager
    {
      public:
        /*! Creates a running manager with no containers. */
        Manager();

        /*! Shuts the manager down, then destroys its containers.
         *
         * It runs Shutdown() first, so that every container with its own thread is stopped and its
         * systems receive System::Shutdown() while all containers still exist. Then it waits for sends
         * already in progress and closes the manager: later sends and creation of containers fail. Then
         * it destroys the containers, which stops the ones driven by their owner's Update() calls.
         *
         * Thread: exclusive. */
        ~Manager();

        /*! Returns the container with this handle, creating it if needed.
         *
         * The same handle always yields the same container, even when many threads ask at once. The
         * pointer is valid until the manager is destroyed. Throws std::runtime_error when a new container
         * is asked for while the manager is being destroyed. A container created after Shutdown() cannot
         * be started with Container::Start().
         *
         * Thread: any. */
        ecs::Container *Container(const std::string &handle);

        /*! Creates a container with a generated handle. The pointer is valid until the manager is
         *  destroyed. Throws std::runtime_error while the manager is being destroyed.
         *
         * Thread: any. */
        ecs::Container *Container();

        /*! Returns the handles of all containers, as a snapshot by value. Containers created later are
         *  not in it.
         *
         * Thread: any. */
        std::vector<std::string> ContainersGet();

        /*! Says whether the manager is running: true until Shutdown() has been called. Once it has
         *  returned false it never returns true again.
         *
         * Thread: any. */
        bool IsRunning();

        /*! Sets IsRunning() to false and stops every container that has its own thread.
         *
         * The call is idempotent and the request is never withdrawn.
         *
         * - From an application thread it blocks until every container that has its own thread has
         *   delivered System::Shutdown() to its systems and its thread has ended. Afterwards no thread of
         *   this manager runs and no system is updated. Concurrent callers each return after that point.
         * - From a thread that runs container code (a system's Initialize(), Update() or Shutdown(), a
         *   timer callback or a deferred function) it only records the request and returns at once, so
         *   it never waits for itself or for another container that is waiting for it. An application
         *   thread's later call completes the wait.
         *
         * Containers driven by the application's own Update() calls are not stopped here, because the
         * library does not run application code on threads it does not own. Stop them with
         * Container::Stop() or by destroying them.
         *
         * Thread: any. */
        void Shutdown();

        /*! Sends a message to the system named in message["destination"], in the container named there.
         *
         * The message must be a JSON object with a "destination" object that holds a non-empty text
         * "container" and a non-empty text "system". Other fields are delivered unchanged. The call
         * returns without waiting for the destination's update.
         *
         * Throws std::runtime_error, changing nothing and taking no lock, if the message breaks these
         * rules; the text names the missing or wrong field and, for a wrong type, the type found. Also
         * throws std::runtime_error if the container or the system is unknown, or the manager is being
         * destroyed. Callers on other threads should catch it.
         *
         * Thread: any. */
        void MessageSubmit(const nlohmann::json &message);
      private:
        ecs::Container *containerCreate(const std::string &handle);
        std::unordered_map<std::string, std::unique_ptr<ecs::Container>> containers;
        std::mutex mutexContainers;
        std::size_t sendsInFlight = 0;
        bool closing = false;
        std::condition_variable sendsIdle;
        std::atomic<bool> running = true;
    };
}
