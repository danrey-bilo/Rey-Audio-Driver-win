#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace rey {
bool update_lan_firewall(uint16_t port, std::string &error);
bool update_lan_firewall(const std::vector<uint16_t> &ports, std::string &error);
}
