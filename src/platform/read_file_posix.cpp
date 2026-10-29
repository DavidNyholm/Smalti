// read_small_file on Linux: open without blocking (opening a FIFO with no writer would wait
// forever), fstat the opened file to turn away anything but a regular file, then read to EOF.
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

#include <sys/stat.h>

#include "platform/read_file.hpp"

namespace smalti::platform {

namespace {

constexpr size_t kChunkBytes = 64 * 1024;

class Descriptor {
 public:
  explicit Descriptor(int fd) : fd_(fd) {}
  ~Descriptor() {
    if (fd_ >= 0) close(fd_);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const { return fd_; }

 private:
  int fd_;
};

}  // namespace

FileText read_small_file(const std::filesystem::path& path, size_t max_bytes) {
  const Descriptor file(open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY));
  if (file.get() < 0) {
    return {errno == ENOENT || errno == ENOTDIR ? ReadStatus::missing : ReadStatus::unreadable, {}};
  }
  struct stat info{};
  if (fstat(file.get(), &info) != 0) return {ReadStatus::unreadable, {}};
  if (S_ISDIR(info.st_mode)) return {ReadStatus::folder, {}};
  if (!S_ISREG(info.st_mode)) return {ReadStatus::not_a_file, {}};
  FileText out{ReadStatus::ok, {}};
  for (;;) {
    const size_t old = out.text.size();
    // One byte past the limit is enough to know the file is too large.
    const size_t want = std::min(kChunkBytes, max_bytes + 1 - old);
    out.text.resize(old + want);
    const ssize_t got = read(file.get(), out.text.data() + old, want);
    if (got < 0) {
      if (errno == EINTR) {
        out.text.resize(old);
        continue;
      }
      return {ReadStatus::unreadable, {}};
    }
    out.text.resize(old + static_cast<size_t>(got));
    if (out.text.size() > max_bytes) return {ReadStatus::too_large, {}};
    if (got == 0) return out;
  }
}

}  // namespace smalti::platform
