// The Windows clipboard. Only one program can have it open at a time, and clipboard managers,
// remote desktop clients and the cloud clipboard open it after every change to read it, so
// opening is retried for a moment before it counts as a failure.
#include "platform/win32_clipboard.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>

#include "platform/text_input.hpp"

namespace smalti::platform {

namespace {

// How long another program may keep the clipboard open. Sleep rounds the pause between tries up
// to the system timer's tick (often 15.6 ms), so the wait can end up to a tick later.
constexpr std::chrono::milliseconds kOpenWait{50};
constexpr DWORD kOpenRetryMs = 5;

// The clipboard, open while this lives.
class ClipboardOpen {
 public:
  explicit ClipboardOpen(HWND owner) {
    const auto deadline = std::chrono::steady_clock::now() + kOpenWait;
    while (!(open_ = OpenClipboard(owner) != FALSE) && std::chrono::steady_clock::now() < deadline)
      Sleep(kOpenRetryMs);
  }
  ~ClipboardOpen() {
    if (open_) CloseClipboard();
  }
  ClipboardOpen(const ClipboardOpen&) = delete;
  ClipboardOpen& operator=(const ClipboardOpen&) = delete;
  explicit operator bool() const { return open_; }

 private:
  bool open_ = false;
};

// A global memory block's contents, locked while this lives; null if it could not be locked.
class GlobalLocked {
 public:
  explicit GlobalLocked(HGLOBAL memory) : memory_(memory), data_(GlobalLock(memory)) {}
  ~GlobalLocked() {
    if (data_) GlobalUnlock(memory_);
  }
  GlobalLocked(const GlobalLocked&) = delete;
  GlobalLocked& operator=(const GlobalLocked&) = delete;
  void* data() const { return data_; }

 private:
  HGLOBAL memory_;
  void* data_;
};

struct GlobalFreer {
  void operator()(void* memory) const { GlobalFree(memory); }
};
using OwnedGlobal = std::unique_ptr<void, GlobalFreer>;

}  // namespace

std::optional<std::string> read_clipboard_text(HWND owner) {
  // Asked without opening the clipboard, so a clipboard without text stays free for others.
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return std::nullopt;
  const ClipboardOpen clipboard(owner);
  if (!clipboard) return std::nullopt;
  const HANDLE memory = GetClipboardData(CF_UNICODETEXT);  // the clipboard's own; never freed
  if (!memory) return std::nullopt;
  const GlobalLocked locked(memory);
  if (!locked.data()) return std::nullopt;
  // The text ends at its NUL, or at the end of the block if a program left the NUL out.
  const std::wstring_view block(static_cast<const wchar_t*>(locked.data()),
                                GlobalSize(memory) / sizeof(wchar_t));
  const std::wstring_view units = block.substr(0, std::min(block.find(L'\0'), block.size()));
  // UTF-8 takes at least a byte per UTF-16 unit: more units than that is more bytes too.
  if (units.size() > detail::kMaxClipboardBytes) return std::nullopt;
  std::string out;
  if (units.empty()) return out;
  const int n_units = static_cast<int>(units.size());
  const int n =
      WideCharToMultiByte(CP_UTF8, 0, units.data(), n_units, nullptr, 0, nullptr, nullptr);
  if (n <= 0 || static_cast<size_t>(n) > detail::kMaxClipboardBytes) return std::nullopt;
  out.resize(static_cast<size_t>(n));
  WideCharToMultiByte(CP_UTF8, 0, units.data(), n_units, out.data(), n, nullptr, nullptr);
  return out;
}

bool write_clipboard_text(HWND owner, std::string_view plain) {
  const std::string text = detail::with_crlf(plain);
  if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
  const int size = static_cast<int>(text.size());
  const int n = size == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
  if (size != 0 && n <= 0) return false;
  // Filled before the clipboard is opened: other programs wait on it only for the hand-over.
  OwnedGlobal memory(GlobalAlloc(GMEM_MOVEABLE, (static_cast<size_t>(n) + 1) * sizeof(wchar_t)));
  if (!memory) return false;
  {
    const GlobalLocked locked(memory.get());
    if (!locked.data()) return false;
    auto* units = static_cast<wchar_t*>(locked.data());
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), size, units, n);
    units[n] = L'\0';
  }
  const ClipboardOpen clipboard(owner);
  if (!clipboard || !EmptyClipboard()) return false;
  if (!SetClipboardData(CF_UNICODETEXT, memory.get())) return false;
  static_cast<void>(memory.release());  // the system's now
  return true;
}

}  // namespace smalti::platform
