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
