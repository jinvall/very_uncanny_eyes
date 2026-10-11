#include "eye.h"

#include <vector>

#include "generated/blueholeEye.h"

namespace uncanny {
namespace {

const EyeArtwork kBlueholeEye{
    "bluehole",
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
        0, 0.0,  // pupilShape, slitRatio (defaults)
    EyeCalibration{100.0F, 0.12F, 0.42F, 100.0F, 100.0F}  // pupilGeometry
};

}  // namespace

const EyeArtwork& blueholeEyeArtwork() {
    return kBlueholeEye;
}

}  // namespace uncanny
