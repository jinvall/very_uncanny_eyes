#include "artwork.h"

#include <vector>

namespace uncanny {

std::vector<const EyeArtwork*> artworkList() {
    return {&defaultEyeArtwork(), &catEyeArtwork(), &dragonEyeArtwork(),
            &customDragonEyeArtwork(), &customDragonEyeAArtwork(),
            &customDragonEyeBArtwork(), &testyEyeArtwork(),
            &noScleraEyeArtwork(), &icuEyeArtwork(),
            &alienDiabeticEyeArtwork(), &coldBorgEyeArtwork(),
            &coldborgjrEyeArtwork(), &coldBlueEyeArtwork(),
            &fireEyeArtwork(), &nightBloomEyeArtwork(), &sgtpkldckEyeArtwork(), &blueholeEyeArtwork(), &nptestEyeArtwork(), &zwholeEyeArtwork(), &chamEyeArtwork(), &xwholeEyeArtwork(), &yolkEyeArtwork(), &agateEyeArtwork(), &chalkBlueEyeArtwork()};
}

}  // namespace uncanny
