// read_small_file on Windows: CreateFileW (which refuses folders without backup semantics),
// GetFileType to turn away pipes, consoles and devices, then ReadFile to the end.
#include <windows.h>

#include <algorithm>

#include "platform/read_file.hpp"

namespace smalti::platform {

namespace {

constexpr DWORD kChunkBytes = 64 * 1024;

class Handle {
 public:
  explicit Handle(HANDLE h) : h_(h) {}
  ~Handle() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return h_; }

 private:
  HANDLE h_;
};

}  // namespace

FileText read_small_file(const std::filesystem::path& path, size_t max_bytes) {
  // Shared for writing and deleting, so an editor can save over the file while it is read.
  const Handle file(CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
      return {ReadStatus::missing, {}};
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
      return {ReadStatus::folder, {}};
    return {ReadStatus::unreadable, {}};
  }
  if (GetFileType(file.get()) != FILE_TYPE_DISK) return {ReadStatus::not_a_file, {}};
  FileText out{ReadStatus::ok, {}};
  for (;;) {
    const size_t old = out.text.size();
    // One byte past the limit is enough to know the file is too large.
    const DWORD want = static_cast<DWORD>(std::min<size_t>(kChunkBytes, max_bytes + 1 - old));
    out.text.resize(old + want);
    DWORD got = 0;
    if (!ReadFile(file.get(), out.text.data() + old, want, &got, nullptr))
      return {ReadStatus::unreadable, {}};
    out.text.resize(old + got);
    if (out.text.size() > max_bytes) return {ReadStatus::too_large, {}};
    if (got == 0) return out;
  }
}

}  // namespace smalti::platform
