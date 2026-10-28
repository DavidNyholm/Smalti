// The GPU compositor on Windows: Direct3D 11 with a flip-model swap chain presented in step
// with the display. d3d11.dll is loaded at run time from the system directory only.
#pragma once

#include <windows.h>

#include <memory>
#include <span>

#include "render/canvas.hpp"

namespace smalti::platform {

struct GpuDeviceImpl;
struct GpuSurfaceImpl;

class GpuDevice {
 public:
  // Slow (hundreds of milliseconds on some drivers): call it off the UI thread. nullptr when
  // Direct3D 11 is not available, as under some remote sessions or virtual machines.
  static std::unique_ptr<GpuDevice> create();
  ~GpuDevice();
  GpuDeviceImpl& impl() { return *impl_; }

 private:
  explicit GpuDevice(std::unique_ptr<GpuDeviceImpl> impl);
  std::unique_ptr<GpuDeviceImpl> impl_;
};

// One window's swap chain. The core draws into a CPU bitmap; present() uploads the damaged
// rectangles and shows the frame at the next refresh.
class GpuSurface {
 public:
  static std::unique_ptr<GpuSurface> create(GpuDevice& device, HWND hwnd, int width, int height);
  ~GpuSurface();
  detail::PixelBuffer frame_buffer();
  bool resize(int width, int height);  // false if the device was lost
  bool present(std::span<const detail::RectI> dirty);
  HANDLE frame_waitable() const;  // signalled when the display can take another frame
  size_t bytes() const;

 private:
  explicit GpuSurface(std::unique_ptr<GpuSurfaceImpl> impl);
  std::unique_ptr<GpuSurfaceImpl> impl_;
};

}  // namespace smalti::platform
