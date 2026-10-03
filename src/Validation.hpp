#ifndef LIBECS_CPP_VALIDATION_HPP
#define LIBECS_CPP_VALIDATION_HPP

// Internal helpers that check input before the library acts on it. This header is
// not installed. Every check is read-only and allocates nothing when the input is
// valid; the text of an error is only built on the failure branch.

#include <libecs-cpp/json.hpp>

#include <stdexcept>
#include <string>

namespace ecs::validation
{
    // Names the public function that is checking its input. The text is built only
    // when an error is raised. A null world means the manager's function.
    struct Caller
    {
        const std::string *world;

        std::string text() const
        {
            if(this->world == nullptr) return "ecs::Manager::MessageSubmit()";
            return "ecs::Container(\"" + *this->world + "\")::MessageSubmit()";
        }
    };

    [[noreturn]] inline void fail(Caller caller, const std::string &condition)
    {
        throw std::runtime_error(caller.text() + ": " + condition + ".");
    }

    [[noreturn]] inline void failNotObject(Caller caller, const nlohmann::json &message)
    {
        fail(caller, std::string("message must be a JSON object, got ") + message.type_name());
    }

    [[noreturn]] inline void failDestinationMissing(Caller caller)
    {
        fail(caller, "message.destination is missing");
    }

    [[noreturn]] inline void failDestinationType(Caller caller, const nlohmann::json &value)
    {
        fail(caller, std::string("message.destination must be a JSON object, got ") + value.type_name());
    }

    [[noreturn]] inline void failFieldMissing(Caller caller, const char *field)
    {
        fail(caller, std::string("message.destination.") + field + " is missing");
    }

    [[noreturn]] inline void failFieldType(Caller caller, const char *field, const nlohmann::json &value)
    {
        fail(caller, std::string("message.destination.") + field + " must be text, got " + value.type_name());
    }

    [[noreturn]] inline void failFieldEmpty(Caller caller, const char *field)
    {
        fail(caller, std::string("message.destination.") + field + " is empty");
    }

    // Returns the destination object of a message, or throws.
    inline const nlohmann::json &messageDestination(const nlohmann::json &message, Caller caller)
    {
        if(!message.is_object()) failNotObject(caller, message);
        auto found = message.find("destination");
        if(found == message.end()) failDestinationMissing(caller);
        if(!found->is_object()) failDestinationType(caller, *found);
        return *found;
    }

    inline const std::string &destinationField(const nlohmann::json &destination, const char *field, Caller caller)
    {
        auto found = destination.find(field);
        if(found == destination.end()) failFieldMissing(caller, field);
        if(!found->is_string()) failFieldType(caller, field, *found);
        const std::string &name = found->get_ref<const std::string &>();
        if(name.empty()) failFieldEmpty(caller, field);
        return name;
    }

    // Checks the message up to and including the destination system name.
    // Does not read destination.container.
    inline const std::string &messageSystem(const nlohmann::json &message, Caller caller)
    {
        return destinationField(messageDestination(message, caller), "system", caller);
    }

    // Checks the message up to and including the destination container name.
    inline const std::string &messageContainer(const nlohmann::json &message, Caller caller)
    {
        return destinationField(messageDestination(message, caller), "container", caller);
    }
}

#endif
