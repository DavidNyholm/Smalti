// FileWatch on Windows: ReadDirectoryChangesW on the file's folder, overlapped, with an event
// the event loop waits on. Names compare without case, as Windows' file systems do.
#include <windows.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/file_watch.hpp"

namespace smalti::platform {

namespace {

// What counts as a change: a file appearing, disappearing or being renamed (a save that
// replaces the file), and writes to it.
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
// Room for the notifications between two reads; more than fit is reported as an overflow,
// which counts as a change.
constexpr size_t kBufferBytes = 16 * 1024;

std::string folder_text(const std::filesystem::path& folder) {
  const std::u8string s = folder.u8string();
  return std::string(s.begin(), s.end());
}

}  // namespace

struct FileWatch::State {
  HANDLE folder = INVALID_HANDLE_VALUE;
  HANDLE event = nullptr;
  OVERLAPPED overlapped{};
  std::vector<DWORD> buffer = std::vector<DWORD>(kBufferBytes / sizeof(DWORD));  // DWORD-aligned
  std::wstring name;   // the file's name within the folder
  bool armed = false;  // a read is pending: the system may write into `buffer`

  // Asks for the next batch of notifications. False once the folder can no longer be watched.
  bool arm() {
    overlapped = {};
    overlapped.hEvent = event;
    armed = ReadDirectoryChangesW(folder, buffer.data(), static_cast<DWORD>(kBufferBytes), FALSE,
                                  kNotifyFilter, nullptr, &overlapped, nullptr) != 0;
    return armed;
  }

  // Whether the notifications in the first `bytes` of the buffer name the file.
  bool names_file(DWORD bytes) const {
    const auto* base = reinterpret_cast<const BYTE*>(buffer.data());
    for (DWORD offset = 0; offset + sizeof(FILE_NOTIFY_INFORMATION) <= bytes;) {
      const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset);
      const int length = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
      if (CompareStringOrdinal(info->FileName, length, name.c_str(), static_cast<int>(name.size()),
                               TRUE) == CSTR_EQUAL) {
        return true;
      }
      if (info->NextEntryOffset == 0) break;
      offset += info->NextEntryOffset;
    }
    return false;
  }
};

FileWatch::FileWatch(const std::filesystem::path& file) : state_(std::make_unique<State>()) {
  std::filesystem::path folder = file.parent_path();
  if (folder.empty()) folder = ".";
  state_->name = file.filename().wstring();
  state_->folder = CreateFileW(
      folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
  if (state_->folder == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("cannot watch the folder \"" + folder_text(folder) +
                             "\" for changes (error " + std::to_string(GetLastError()) + ")");
  }
  state_->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual reset, see take_changes
  if (!state_->event || !state_->arm()) {
    const DWORD error = GetLastError();
    if (state_->event) CloseHandle(state_->event);
    CloseHandle(state_->folder);
    throw std::runtime_error("cannot watch the folder \"" + folder_text(folder) +
                             "\" for changes (error " + std::to_string(error) + ")");
  }
}

FileWatch::~FileWatch() {
  if (state_->armed) {
    // The system writes into the buffer until the read ends: cancel it, and wait until it has
    // ended, before the buffer is freed.
    CancelIoEx(state_->folder, &state_->overlapped);
    DWORD bytes = 0;
    GetOverlappedResult(state_->folder, &state_->overlapped, &bytes, TRUE);
  }
  CloseHandle(state_->event);
  CloseHandle(state_->folder);
}

Waitable FileWatch::waitable() const { return state_->event; }

bool FileWatch::take_changes() {
  State& s = *state_;
  if (!s.armed || WaitForSingleObject(s.event, 0) != WAIT_OBJECT_0) return false;
  DWORD bytes = 0;
  bool changed = true;  // a failed read (the folder went away) or an overflow may include it
  if (GetOverlappedResult(s.folder, &s.overlapped, &bytes, FALSE) && bytes > 0) {
    changed = s.names_file(bytes);
  }
  ResetEvent(s.event);
  s.arm();  // when this fails the folder is gone: the event stays clear and nothing more comes
  return changed;
}

}  // namespace smalti::platform
