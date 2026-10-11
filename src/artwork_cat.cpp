#include "eye.h"

#include <vector>

#include "../Uncanny_Eyes/uncannyEyes/graphics/catEye.h"

namespace uncanny {
namespace {

const EyeArtwork kCatEye{
    "cat",
    reinterpret_cast<const std::uint16_t*>(sclera),
    SCLERA_WIDTH,
    SCLERA_HEIGHT,
    reinterpret_cast<const std::uint16_t*>(iris),
    1,
    1,
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint8_t*>(upper),
    reinterpret_cast<const std::uint8_t*>(lower),
    reinterpret_cast<const std::uint16_t*>(polar),
    IRIS_WIDTH,
    IRIS_HEIGHT,
    IRIS_MIN,
    IRIS_MAX,
        0, 0.0,  // pupilShape, slitRatio (defaults)
    EyeCalibration{100.0F, 0.12F, 0.42F, 100.0F, 100.0F}  // pupilGeometry
};

}  // namespace

const EyeArtwork& catEyeArtwork() {
    return kCatEye;
}

}  // namespace uncanny
