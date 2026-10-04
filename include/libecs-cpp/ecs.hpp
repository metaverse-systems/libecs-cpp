#pragma once

#include <libecs-cpp/json.hpp>
#include <libecs-cpp/Resource.hpp>
#include <libecs-cpp/Clock.hpp>
#include <libecs-cpp/Uuid.hpp>
#include <libecs-cpp/Manager.hpp>
#include <libecs-cpp/Container.hpp>

#include <libecs-cpp/System.hpp>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/Entity.hpp>

/*! The process-wide manager. It is never destroyed, so a program that uses it calls ECS->Shutdown()
 *  from its main thread before main returns. */
extern ecs::Manager *ECS;
