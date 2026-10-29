#include "platform/win32_gpu.hpp"

#include <d3d11.h>
#include <dxgi1_3.h>

#include <algorithm>

#include <wrl/client.h>

namespace smalti::platform {

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kSwapBuffers = 2;
constexpr int kBytesPerPixel = 4;

using CreateDeviceFn = decltype(&D3D11CreateDevice);

// d3d11.dll is not on the KnownDLLs list, so it is loaded explicitly from the system directory:
// a d3d11.dll planted next to the application is never picked up.
CreateDeviceFn load_create_device() {
  static CreateDeviceFn fn = [] {
    const HMODULE module = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module ? reinterpret_cast<CreateDeviceFn>(GetProcAddress(module, "D3D11CreateDevice"))
                  : nullptr;
  }();
  return fn;
}

}  // namespace

struct GpuDeviceImpl {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIFactory2> factory;
};

struct GpuSurfaceImpl {
  GpuSurfaceImpl() = default;
  GpuSurfaceImpl(const GpuSurfaceImpl&) = delete;
  GpuSurfaceImpl& operator=(const GpuSurfaceImpl&) = delete;
  ~GpuSurfaceImpl() {
    if (waitable) CloseHandle(waitable);  // owned from the moment it is acquired
  }

  GpuDeviceImpl* dev = nullptr;
  HWND hwnd = nullptr;
  ComPtr<IDXGISwapChain2> swap;
  ComPtr<ID3D11Texture2D> frame;  // the composited frame, updated rectangle by rectangle
  HANDLE waitable = nullptr;
  detail::Bitmap pixels;
  int width = 0, height = 0;

  bool make_frame_texture() {
    frame.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    return SUCCEEDED(dev->device->CreateTexture2D(&td, nullptr, frame.GetAddressOf()));
  }
};

GpuDevice::GpuDevice(std::unique_ptr<GpuDeviceImpl> impl) : impl_(std::move(impl)) {}
GpuDevice::~GpuDevice() = default;

std::unique_ptr<GpuDevice> GpuDevice::create() {
  const CreateDeviceFn create_device = load_create_device();
  if (!create_device) return nullptr;
  auto impl = std::make_unique<GpuDeviceImpl>();
  const HRESULT hr = create_device(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
      D3D11_SDK_VERSION, impl->device.GetAddressOf(), nullptr, impl->context.GetAddressOf());
  if (FAILED(hr)) return nullptr;
  ComPtr<IDXGIDevice> dxgi;
  ComPtr<IDXGIAdapter> adapter;
  if (FAILED(impl->device.As(&dxgi)) || FAILED(dxgi->GetAdapter(adapter.GetAddressOf())) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(impl->factory.GetAddressOf())))) {
    return nullptr;
  }
  return std::unique_ptr<GpuDevice>(new GpuDevice(std::move(impl)));
}

GpuSurface::GpuSurface(std::unique_ptr<GpuSurfaceImpl> impl) : impl_(std::move(impl)) {}

GpuSurface::~GpuSurface() = default;

std::unique_ptr<GpuSurface> GpuSurface::create(GpuDevice& device, HWND hwnd, int width,
                                               int height) {
  auto s = std::make_unique<GpuSurfaceImpl>();
  s->dev = &device.impl();
  s->hwnd = hwnd;
  s->width = std::max(1, width);
  s->height = std::max(1, height);
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = static_cast<UINT>(s->width);
  desc.Height = static_cast<UINT>(s->height);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = kSwapBuffers;
  desc.Scaling = DXGI_SCALING_NONE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  ComPtr<IDXGISwapChain1> swap1;
  if (FAILED(s->dev->factory->CreateSwapChainForHwnd(s->dev->device.Get(), hwnd, &desc, nullptr,
                                                     nullptr, swap1.GetAddressOf())) ||
      FAILED(swap1.As(&s->swap))) {
    return nullptr;
  }
  s->dev->factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
  // One frame in flight: a frame is started only when the display can take it, which keeps
  // input latency low and turns the waitable object into the frame clock.
  s->swap->SetMaximumFrameLatency(1);
  s->waitable = s->swap->GetFrameLatencyWaitableObject();
  if (!s->make_frame_texture()) return nullptr;
  s->pixels = detail::Bitmap(s->width, s->height);
  return std::unique_ptr<GpuSurface>(new GpuSurface(std::move(s)));
}

detail::PixelBuffer GpuSurface::frame_buffer() { return impl_->pixels.view(); }

bool GpuSurface::resize(int width, int height) {
  GpuSurfaceImpl& s = *impl_;
  s.width = std::max(1, width);
  s.height = std::max(1, height);
  s.dev->context->ClearState();
  if (FAILED(s.swap->ResizeBuffers(0, static_cast<UINT>(s.width), static_cast<UINT>(s.height),
                                   DXGI_FORMAT_UNKNOWN,
                                   DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) {
    return false;
  }
  s.pixels = detail::Bitmap(s.width, s.height);
  return s.make_frame_texture();
}

bool GpuSurface::present(std::span<const detail::RectI> dirty) {
  GpuSurfaceImpl& s = *impl_;
  for (const detail::RectI& r : dirty) {
    const detail::RectI c = r.intersect({0, 0, s.width, s.height});
    if (c.empty()) continue;
    const D3D11_BOX box{static_cast<UINT>(c.x),       static_cast<UINT>(c.y),        0,
                        static_cast<UINT>(c.right()), static_cast<UINT>(c.bottom()), 1};
    const uint32_t* src =
        s.pixels.pixels.data() + static_cast<size_t>(c.y) * static_cast<size_t>(s.width) + c.x;
    s.dev->context->UpdateSubresource(s.frame.Get(), 0, &box, src,
                                      static_cast<UINT>(s.width * kBytesPerPixel), 0);
  }
  // A flip-model back buffer starts undefined, so the whole frame is copied: one GPU copy,
  // a fraction of a millisecond even at 4K.
  ComPtr<ID3D11Texture2D> back;
  if (FAILED(s.swap->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf())))) return false;
  s.dev->context->CopyResource(back.Get(), s.frame.Get());
  const HRESULT hr = s.swap->Present(1, 0);
  return SUCCEEDED(hr) || hr == DXGI_STATUS_OCCLUDED;
}

HANDLE GpuSurface::frame_waitable() const { return impl_->waitable; }

size_t GpuSurface::bytes() const {
  const size_t frame =
      static_cast<size_t>(impl_->width) * static_cast<size_t>(impl_->height) * kBytesPerPixel;
  return frame * (kSwapBuffers + 1);  // the swap chain's buffers and the frame texture
}

}  // namespace smalti::platform
