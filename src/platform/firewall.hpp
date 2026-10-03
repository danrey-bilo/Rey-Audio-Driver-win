#pragma once
#include <cstdint>
#include <string>
namespace rey {
bool update_lan_firewall(uint16_t port, std::string &error);
}
