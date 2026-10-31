// The Windows clipboard's text: CF_UNICODETEXT, UTF-16 with "\r\n" line endings, which Windows
// also makes from the older text formats when a program offers only those.
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace smalti::platform {

// The clipboard's text in UTF-8, as Windows holds it (unpaired surrogates become U+FFFD);
// nullopt if it holds no text or more than detail::kMaxClipboardBytes, or if another program
// keeps it open past the wait.
std::optional<std::string> read_clipboard_text(HWND owner);

// Replaces the clipboard's contents with plain text ("\n" line endings), written with "\r\n".
// `owner` is a window of this thread; the text stays on the clipboard after it is destroyed.
bool write_clipboard_text(HWND owner, std::string_view plain);

}  // namespace smalti::platform
