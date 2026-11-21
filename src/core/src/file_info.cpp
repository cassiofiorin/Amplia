#include "wamp/file_info.h"

#include <chrono>
#include <filesystem>

namespace wamp {
namespace fs = std::filesystem;

std::int64_t fileSizeBytes(const std::string& path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error) return 0;
  return static_cast<std::int64_t>(size);
}

std::int64_t fileModifiedMs(const std::string& path) {
  std::error_code error;
  const auto time = fs::last_write_time(path, error);
  if (error) return 0;
  const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
  return std::chrono::duration_cast<std::chrono::milliseconds>(system.time_since_epoch())
      .count();
}

}  // namespace wamp
