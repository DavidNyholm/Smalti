// Chooses the platform backend.
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "platform/platform.hpp"

namespace smalti::platform {

namespace {
// The dots per inch at a scale of 1, and the range of settings taken as real: a quarter of
// that to ten times it. Anything else (a typo, "inf") is ignored rather than trusted.
constexpr float kBaseDpi = 96;
constexpr float kMinDpi = kBaseDpi / 4, kMaxDpi = kBaseDpi * 10;
}  // namespace

int window_pixels(float logical, float scale) {
  const double v = static_cast<double>(logical) * static_cast<double>(scale);
  if (!(v >= 1)) return 1;  // also NaN
  if (v >= kMaxWindowPixels) return kMaxWindowPixels;
  return static_cast<int>(std::lround(v));
}

float scale_from_dpi_text(const char* text) {
  if (!text) return 1;
  const char* end = text + std::strlen(text);
  while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n')) --end;
  float dpi = 0;
  const auto [stop, ec] = std::from_chars(text, end, dpi);
  if (ec != std::errc{} || stop != end || !std::isfinite(dpi) || dpi < kMinDpi || dpi > kMaxDpi) {
    return 1;
  }
  return dpi / kBaseDpi;
}

std::unique_ptr<PlatformBackend> create_backend(Backend kind,
                                                [[maybe_unused]] Presentation presentation,
                                                BackendHost& host) {
#if defined(SMALTI_HEADLESS_ONLY)
  // Built without native backends (SMALTI_HEADLESS_ONLY): every window draws into memory.
  if (kind == Backend::win32 || kind == Backend::x11) {
    throw std::runtime_error("smalti: this build has no native backend (SMALTI_HEADLESS_ONLY)");
  }
  return create_headless_backend(host);
#else
  switch (kind) {
    case Backend::headless:
      return create_headless_backend(host);
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
    case Backend::automatic:
      break;
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
