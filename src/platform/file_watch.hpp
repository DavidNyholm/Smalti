// Watching one file for changes through the operating system, with no thread and no polling:
// the watch is an event handle (Windows) or a descriptor (Linux) that the event loop adds to
// its wait, and take_changes() reads what the system reported once it is signalled.
//
// The folder is watched, not the file: editors save by writing a new file and renaming it over
// the old one, and a watch on the file itself would be lost with the old file.
#pragma once

#include <filesystem>
#include <memory>

#include "smalti/callback.hpp"

namespace smalti::platform {

#if defined(_WIN32)
using Waitable = void*;  // a HANDLE: an event, signalled while changes wait to be read
#else
using Waitable = int;  // a descriptor that polls readable while changes wait to be read
#endif

class FileWatch {
 public:
  // Starts watching `file`. Throws std::runtime_error, saying why, if its folder cannot be
  // watched (it does not exist, or the system refuses).
  explicit FileWatch(const std::filesystem::path& file);
  ~FileWatch();
  FileWatch(const FileWatch&) = delete;
  FileWatch& operator=(const FileWatch&) = delete;

  Waitable waitable() const;
  // Reads what the system has reported, without blocking: whether the file was among the
  // changes. A report too long to read whole counts as a change, as it may have included it.
  bool take_changes();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

// A backend's one watched file, and what to call when it changes.
class WatchedFile {
 public:
  // Replaces any earlier watch. Throws std::runtime_error, with nothing watched, if the file's
  // folder cannot be watched.
  void start(const std::filesystem::path& file, Callback<void()> changed) {
    stop();
    watch_ = std::make_unique<FileWatch>(file);
    changed_ = std::move(changed);
  }
  void stop() {
    watch_.reset();
    changed_ = {};
  }
  bool active() const { return watch_ != nullptr; }
  Waitable waitable() const { return watch_->waitable(); }
  // Calls the callback if the system reported a change to the file. Never blocks.
  void poll() {
    if (watch_ && watch_->take_changes() && changed_) changed_();
  }

 private:
  std::unique_ptr<FileWatch> watch_;
  Callback<void()> changed_;
};

}  // namespace smalti::platform
