#include "manager.hpp"
#include <sstream>
#include <limits>

namespace rey::service {
std::string Manager::asio_request(const std::string &command, DWORD sender_pid) {
  std::istringstream in(command);
  std::string prefix, action, id, extra;
  in >> prefix >> action >> id;
  auto fail = [](const std::string &message) { return "REY_ASIO_ERROR " + message; };
  if (prefix != "ASIO" || id.empty()) return fail("Malformed command");
  std::lock_guard<std::mutex> lock(mutex_);
  auto *session = session_.get();
  rey::asio::Info info;
  if (id != "auto" && (!valid_device_id(id) || id != identity_)) return fail("Unknown USB DeviceId");
  if (!session || !session->asio().info(info)) return fail("No active USB ASIO session");
  if (action == "INFO") {
    if (in >> extra) return fail("Malformed INFO");
    std::ostringstream out;
    out << "REY_ASIO_INFO " << info.id << ' ' << info.rate << ' ' << info.bits << ' '
        << info.block << ' ' << info.depth << ' ' << info.generation;
    return out.str();
  }
  if (!sender_pid) return fail("Unauthenticated client");
  if (action == "OPEN") {
    uint64_t pid = 0, mapping = 0, event = 0;
    unsigned block = 0, lead = 0;
    if (!(in >> pid >> mapping >> event >> block >> lead) || (in >> extra) || pid != sender_pid ||
        !mapping || !event || !rey::asio::valid_block(block) || lead < 1 || lead > 4)
      return fail("Malformed or unauthenticated OPEN");
    std::string error;
    return session->asio().connect(sender_pid, mapping, event, block, lead, error)
        ? "REY_ASIO_OK" : fail(error);
  }
  if (action == "CLOSE") {
    if (in >> extra) return fail("Malformed CLOSE");
    return session->asio().disconnect(sender_pid) ? "REY_ASIO_OK" : fail("ASIO ownership differs");
  }
  if (action == "RATE") {
    unsigned rate = 0;
    if (!(in >> rate) || (in >> extra) || !rey::asio::valid_rate(rate)) return fail("Invalid rate");
    auto next = session->settings(); next.usb.rate = rate;
    std::string error;
    return persist(next, error) ? "REY_ASIO_OK" : fail(error);
  }
  return fail("Unknown ASIO command");
}
} // namespace rey::service
