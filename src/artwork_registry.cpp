#include "artwork.h"

#include <vector>

namespace uncanny {

std::vector<const EyeArtwork*> artworkList() {
    return {&defaultEyeArtwork(), &catEyeArtwork(), &dragonEyeArtwork(),
            &customDragonEyeArtwork(), &customDragonEyeAArtwork(),
            &customDragonEyeBArtwork(), &coldBlueEyeArtwork(), &fireEyeArtwork(),
            &chalkBlueEyeArtwork()};
}

}  // namespace uncanny
