#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
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
     *   made from inside a system is a single store and cannot deadlock. Requesting shutdown does not
     *   stop world threads or destroy worlds.
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

        /** Exclusive. Requests shutdown, waits for sends in progress, then destroys the containers. */
        ~Manager();

        /** Any thread. Returns the container with this handle, creating it if needed; the same handle always yields the same container. */
        ecs::Container *Container(const std::string &handle);

        /** Any thread. Creates a container with a generated unique handle. */
        ecs::Container *Container();

        /** Any thread. Returns a snapshot of the handles of all containers, by value. */
        std::vector<std::string> ContainersGet();

        /** Any thread. True until Shutdown() has been called. */
        bool IsRunning();

        /** Any thread. Requests shutdown; idempotent, and the request is never withdrawn. */
        void Shutdown();

        /** Any thread. Routes a message to its destination container; throws std::runtime_error if that container is unknown or the manager is being destroyed. */
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
