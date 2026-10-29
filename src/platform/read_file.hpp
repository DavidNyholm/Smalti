// Reading a small text file whole, safely: it is opened once, checked to be a regular file
// before anything is read (a FIFO or a device never blocks or streams forever), and read to its
// end under a byte limit, so a file replaced or growing meanwhile is read as it is, or refused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace smalti::platform {

enum class ReadStatus : uint8_t { ok, missing, folder, not_a_file, too_large, unreadable };

struct FileText {
  ReadStatus status = ReadStatus::unreadable;
  std::string text;  // the whole file when status is ok
};

FileText read_small_file(const std::filesystem::path& path, size_t max_bytes);

}  // namespace smalti::platform
