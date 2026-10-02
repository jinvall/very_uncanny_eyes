#include "eye.h"

#include "generated/customDragonEye_A.h"

namespace uncanny {
namespace {

const EyeArtwork kCustomDragonEye{
    "customDragon",
    reinterpret_cast<const std::uint16_t*>(sclera),
    SCLERA_WIDTH,
    SCLERA_HEIGHT,
    reinterpret_cast<const std::uint16_t*>(iris_dragonA),
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

const EyeArtwork& customDragonEyeArtwork() {
    return kCustomDragonEye;
}

}  // namespace uncanny
