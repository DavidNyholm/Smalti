// The Inter fonts compiled into the library. The byte arrays are generated at configure time
// from fonts/*.ttf (see cmake/embed_fonts.cmake).
#include <cstddef>
#include <span>

#include "render/text.hpp"

namespace smalti::detail {

extern const unsigned char kInterRegular[];
extern const size_t kInterRegularSize;
extern const unsigned char kInterMedium[];
extern const size_t kInterMediumSize;
extern const unsigned char kInterSemiBold[];
extern const size_t kInterSemiBoldSize;

FontSet load_embedded_fonts() {
  auto load = [](const unsigned char* data, size_t size) {
    return std::make_unique<Font>(std::span<const uint8_t>(data, size));
  };
  return {load(kInterRegular, kInterRegularSize), load(kInterMedium, kInterMediumSize),
          load(kInterSemiBold, kInterSemiBoldSize)};
}

}  // namespace smalti::detail
