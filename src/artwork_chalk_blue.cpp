#include "eye.h"

#include <vector>

#include "generated/chalkBlueEye.h"

namespace uncanny {
namespace {

// IRIS_MIN / IRIS_MAX are the iris animation clamp (0..1023, larger = bigger
// iris), not image dimensions. These match the default variant's range.
//
// IRIS_SIZE 118 is derived, not guessed: normalize_eye.py measures the limbal
// radius and the eyeball's outer edge in the source photo, then frames the
// eyeball as the 200px plate (--framing eyeball, pupil at .5,.5). If the polar
// table is much larger than the real iris, the iris texture bleeds into the
// sclera as the eye dilates.
const EyeArtwork kChalkBlueEye{
    "chalkBlue",
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

const EyeArtwork& chalkBlueEyeArtwork() {
    return kChalkBlueEye;
}

}  // namespace uncanny