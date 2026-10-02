#include "eye.h"

#include <vector>

#include "../Uncanny_Eyes/uncannyEyes/graphics/defaultEye.h"

namespace uncanny {
namespace {

const EyeArtwork kDefaultEye{
    "default",
    reinterpret_cast<const std::uint16_t*>(sclera),
    SCLERA_WIDTH,
    SCLERA_HEIGHT,
    reinterpret_cast<const std::uint16_t*>(iris),
    IRIS_MAP_WIDTH,
    IRIS_MAP_HEIGHT,
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint16_t*>(polar),
    IRIS_WIDTH,
    IRIS_HEIGHT,
    120,
    720,
};

}  // namespace

const EyeArtwork& defaultEyeArtwork() {
    return kDefaultEye;
}

}  // namespace uncanny
