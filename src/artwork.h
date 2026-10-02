#pragma once

#include "eye.h"

#include <vector>

namespace uncanny {
const EyeArtwork& defaultEyeArtwork();
const EyeArtwork& catEyeArtwork();
const EyeArtwork& dragonEyeArtwork();
const EyeArtwork& customDragonEyeArtwork();
const EyeArtwork& customDragonEyeAArtwork();
const EyeArtwork& customDragonEyeBArtwork();
const EyeArtwork& coldBlueEyeArtwork();
const EyeArtwork& fireEyeArtwork();
const EyeArtwork& chalkBlueEyeArtwork();
std::vector<const EyeArtwork*> artworkList();
}  // namespace uncanny
