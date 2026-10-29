// FileWatch on Linux: inotify on the file's folder, its descriptor non-blocking so the event
// loop can poll it. Names compare exactly, as Linux file systems do.
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <sys/inotify.h>

#include "platform/file_watch.hpp"

namespace smalti::platform {

namespace {

// What counts as a change: a file written and closed, created, deleted, or renamed into or out
// of the folder (a save that replaces the file). Not every write: a save arrives at its close.
constexpr uint32_t kEvents =
    IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE_SELF;
// Room for many events per read; the loop reads until the queue is empty anyway.
constexpr size_t kBufferBytes = 16 * 1024;

std::string folder_text(const std::filesystem::path& folder) {
  const std::u8string s = folder.u8string();
  return std::string(s.begin(), s.end());
}

}  // namespace

struct FileWatch::State {
  int fd = -1;
  std::string name;  // the file's name within the folder
};

FileWatch::FileWatch(const std::filesystem::path& file) : state_(std::make_unique<State>()) {
  std::filesystem::path folder = file.parent_path();
  if (folder.empty()) folder = ".";
  state_->name = file.filename().string();
  state_->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (state_->fd < 0) {
    throw std::runtime_error(std::string("cannot watch files for changes (") +
                             std::strerror(errno) + ")");
  }
  if (inotify_add_watch(state_->fd, folder.c_str(), kEvents) < 0) {
    const int error = errno;
    close(state_->fd);
    throw std::runtime_error("cannot watch the folder \"" + folder_text(folder) +
                             "\" for changes (" + std::strerror(error) + ")");
  }
}

FileWatch::~FileWatch() { close(state_->fd); }

Waitable FileWatch::waitable() const { return state_->fd; }

bool FileWatch::take_changes() {
  alignas(inotify_event) char buffer[kBufferBytes];
  bool changed = false;
  for (;;) {
    const ssize_t got = read(state_->fd, buffer, sizeof buffer);
    if (got <= 0) break;  // EAGAIN: nothing more queued
    for (ssize_t offset = 0; offset + static_cast<ssize_t>(sizeof(inotify_event)) <= got;) {
      const auto* e = reinterpret_cast<const inotify_event*>(buffer + offset);
      // An overflow, or the folder itself gone, may have included the file.
      if (e->mask & (IN_Q_OVERFLOW | IN_DELETE_SELF | IN_IGNORED)) changed = true;
      if (e->len > 0 && state_->name == e->name) changed = true;
      offset += static_cast<ssize_t>(sizeof(inotify_event) + e->len);
    }
  }
  return changed;
}

}  // namespace smalti::platform
