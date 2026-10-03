#pragma once
#include "settings.hpp"
namespace rey::service {
bool command_settings(const Settings &, const std::string &, Settings &, std::string &error);
}
