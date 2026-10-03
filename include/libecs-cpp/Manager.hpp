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

    /**
     * Manages containers.
     *
     * Every method is safe to call from any thread, including from inside a
     * system's Update(). Shutdown is atomic and never reverts once requested.
     * The destructor is the one exclusive operation: it waits for message
     * submissions already in progress, and later submissions fail as if the
     * destination container were unknown.
     *
     * Threading
     *
     * - Container(handle), Container(), ContainersGet(), IsRunning(), Shutdown() and MessageSubmit()
     *   are safe from any thread at any time. Container(handle) yields exactly one world per handle
     *   even when many threads ask at once. ContainersGet() returns a snapshot by value; worlds
     *   created later are not in it. The pointers keep their existing lifetime: valid until the
     *   manager is destroyed.
     * - Shutdown guarantee: IsRunning() and Shutdown() are atomic. Once IsRunning() has returned false
     *   it never returns true again, and repeated or concurrent requests are idempotent. A request
     *   made from a thread that runs world code (a system's Initialize(), Update() or Shutdown(), a timer
     *   callback or a deferred function) only
     *   requests the stop of the worlds that have their own thread and returns at once; from any other thread, Shutdown() returns
     *   after every threaded world has stopped, its thread has ended and its systems have been shut
     *   down. Worlds driven by their owner's calls to Update() are not touched; the owner stops them.
     * - MessageSubmit() returns without waiting for the destination's update and throws
     *   std::runtime_error if the world is unknown. Callers on other threads should catch it.
     * - Destruction: the destructor waits for sends already in progress; sends that start later fail
     *   as unknown. No outside thread may use a manager that another thread is destroying, except
     *   through sends that were already in progress.
     * - The process-wide ECS manager is intentionally never destroyed.
     */

    class Manager
    {
      public:
        Manager();

        /** Exclusive. Runs Shutdown() first, so that every threaded world is stopped and joined, and its
         *  systems are notified, while all worlds still exist. Then it closes the manager: later sends and
         *  creation of worlds fail. Then it destroys the worlds, which stops and releases the worlds that
         *  are driven by their owner's Update() calls. The process-wide ECS manager is never destroyed. */
        ~Manager();

        /** Any thread. Returns the container with this handle, creating it if needed; the same handle always yields the same container.
         *  Throws std::runtime_error when a new world is asked for while the manager is being destroyed. A world created after
         *  Shutdown() cannot be started with Start(). */
        ecs::Container *Container(const std::string &handle);

        /** Any thread. Creates a container with a generated unique handle. Throws std::runtime_error when a new world is asked for while the manager is being destroyed. */
        ecs::Container *Container();

        /** Any thread. Returns a snapshot of the handles of all containers, by value. */
        std::vector<std::string> ContainersGet();

        /** Any thread. True until Shutdown() has been called. */
        bool IsRunning();

        /** Any thread. Sets IsRunning() to false, asks every world that has its own thread to stop, and waits for it (see above);
         *  idempotent, and the request is never withdrawn.
         *
         *  From an application thread it blocks until every world that has its own thread has ended and
         *  delivered Shutdown() to its systems; afterwards no thread of this manager runs and no system
         *  is updated. Concurrent callers each return after that point. From a world thread it only
         *  requests, so it never waits for itself or for another world that is waiting for it; an
         *  application thread's later call completes the wait. Worlds driven by the application's own
         *  Update() calls are not stopped here (the library does not run application code on threads it
         *  does not own); stop them with Container::Stop() or by destroying them. A program that uses the
         *  process-wide manager calls ECS->Shutdown() from its main thread before main returns. */
        void Shutdown();

        /** Any thread. Routes a message to its destination container.
         *
         * The message must be a JSON object with a "destination" object that holds a non-empty text
         * "container" and a non-empty text "system". Other fields are delivered unchanged. A message that
         * breaks these rules throws std::runtime_error whose text names the missing or wrong field and,
         * for a wrong type, the type that was found. The checks run before any lock is taken, so a
         * rejected message changes nothing: no mailbox gains an entry and shutdown accounting is not
         * touched. Throws std::runtime_error as well if the container or the system is unknown, or the
         * manager is being destroyed. */
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
