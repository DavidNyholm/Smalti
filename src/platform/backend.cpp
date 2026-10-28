// Chooses the platform backend.
#include <cstdlib>
#include <stdexcept>

#include "platform/platform.hpp"

namespace smalti::platform {

std::unique_ptr<PlatformBackend> create_backend(Backend kind, [[maybe_unused]] Presentation presentation,
                                                BackendHost& host) {
#if defined(SMALTI_HEADLESS_ONLY)
  // Built without native backends (SMALTI_HEADLESS_ONLY): every window draws into memory.
  if (kind == Backend::win32 || kind == Backend::x11) {
    throw std::runtime_error("smalti: this build has no native backend (SMALTI_HEADLESS_ONLY)");
  }
  return create_headless_backend(host);
#else
  switch (kind) {
    case Backend::headless: return create_headless_backend(host);
    case Backend::win32:
#if defined(_WIN32)
      return create_win32_backend(presentation, host);
#else
      throw std::runtime_error("smalti: the Win32 backend exists only on Windows");
#endif
    case Backend::x11:
#if defined(__linux__)
      if (auto b = create_x11_backend(host)) return b;
      throw std::runtime_error("smalti: cannot open the X11 display (is DISPLAY set?)");
#else
      throw std::runtime_error("smalti: the X11 backend exists only on Linux");
#endif
    case Backend::automatic: break;
  }
#if defined(_WIN32)
  return create_win32_backend(presentation, host);
#elif defined(__linux__)
  if (auto b = create_x11_backend(host)) return b;
  return create_headless_backend(host);
#else
  return create_headless_backend(host);
#endif
#endif  // SMALTI_HEADLESS_ONLY
}

}  // namespace smalti::platform
