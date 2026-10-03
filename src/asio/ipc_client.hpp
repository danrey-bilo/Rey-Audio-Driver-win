#pragma once
#include "ipc_protocol.hpp"
#include <string>

namespace rey::asio {
bool command(const std::string &, std::string &reply, std::string &error);
bool query_info(const std::string &id, Info &, std::string &error);
class Client {
public:
  ~Client();
  bool connect(const Info &, unsigned block, unsigned lead, std::string &error);
  void close();
  Shared *shared() const { return shared_; }
  HANDLE event() const { return event_; }
  bool capture(Slot &, uint64_t &dropped);
  bool render(const Slot &);
private:
  Info info_;
  HANDLE mapping_ = nullptr, event_ = nullptr;
  Shared *shared_ = nullptr;
  bool connected_ = false;
};
} // namespace rey::asio
