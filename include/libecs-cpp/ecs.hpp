#pragma once

#include <libecs-cpp/Manager.hpp>
#include <libecs-cpp/Container.hpp>

#include <libecs-cpp/System.hpp>
#include <libecs-cpp/Component.hpp>
#include <libecs-cpp/Entity.hpp>

#include <string>
#include <map>
typedef std::map<std::string, std::string> ConfigMap;

extern ecs::Manager *ECS;
