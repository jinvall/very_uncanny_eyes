#include "eye.h"

#include "generated/customDragonEye_B.h"

namespace uncanny {
namespace {

const EyeArtwork kCustomDragonEyeB{
    "customDragonB",
    reinterpret_cast<const std::uint16_t*>(sclera),
    SCLERA_WIDTH,
    SCLERA_HEIGHT,
    reinterpret_cast<const std::uint16_t*>(iris_dragonB),
    IRIS_MAP_WIDTH,
    IRIS_MAP_HEIGHT,
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint16_t*>(polar),
    IRIS_WIDTH,
    IRIS_HEIGHT,
    IRIS_MIN,
    IRIS_MAX,
};

}  // namespace

const EyeArtwork& customDragonEyeBArtwork() {
    return kCustomDragonEyeB;
}

}  // namespace uncanny
