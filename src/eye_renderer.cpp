// Standalone eye renderer that outputs eye frames as raw RGBA files
// Usage: ./eye_renderer [iris_scale] [design] [output_prefix]
#include "eye.h"
#include "artwork.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace uncanny;

// Write rendered eye as raw RGBA file
void writeRGBA(const std::string& filename, const EyeRender& render) {
    FILE* f = fopen(filename.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename.c_str());
        return;
    }
    // Write header: "EYE" + width (4 bytes) + height (4 bytes) + channels (1 byte)
    char magic[4] = {'E', 'Y', 'E', '1'};
    fwrite(magic, 1, 4, f);
    int width = kEyeSize;
    int height = kEyeSize;
    fwrite(&width, sizeof(int), 1, f);
    fwrite(&height, sizeof(int), 1, f);
    fputc(4, f); // 4 channels (RGBA)
    fwrite(render.rgba.data(), 1, render.rgba.size(), f);
    fclose(f);
    printf("Wrote %s\n", filename.c_str());
}

int main(int argc, char** argv) {
    int irisScale = 500;
    std::string design = "default";
    std::string outputPrefix = "eye";
    
    if (argc > 1) irisScale = std::atoi(argv[1]);
    if (argc > 2) design = argv[2];
    if (argc > 3) outputPrefix = argv[3];
    
    printf("Rendering eyes with irisScale=%d, design=%s\n", irisScale, design.c_str());
    
    // Find artwork
    const EyeArtwork* artwork = nullptr;
    for (const EyeArtwork* art : artworkList()) {
        if (std::string(art->name) == design) {
            artwork = art;
            break;
        }
    }
    
    if (!artwork) {
        fprintf(stderr, "Unknown design: %s\n", design.c_str());
        return 1;
    }
    
    printf("Found artwork: sclera=%dx%d, iris=%dx%d, irisMin=%d, irisMax=%d\n",
           artwork->scleraWidth, artwork->scleraHeight,
           artwork->irisWidth, artwork->irisHeight, artwork->irisMin, artwork->irisMax);
    
    EyeRenderer renderer(*artwork);
    RenderSettings settings{true, false, artwork->irisMin, artwork->irisMax};
    
    EyeAnimator animator({false, false, false, 1});
    animator.setEyeTypes(IrisType::Mammalian, Realism::Realistic);
    animator.setMode(AnimationMode::Normal, 1'000'000);
    animator.setIrisScale(irisScale);
    
    // Render both eyes
    const std::uint64_t now = 1'000'000;
    EyeFrame leftFrame = animator.update(0, now, *artwork, renderer, settings);
    EyeFrame rightFrame = animator.update(1, now, *artwork, renderer, settings);
    
    // Save the renders
    writeRGBA(outputPrefix + "_left.eye", leftFrame.render);
    writeRGBA(outputPrefix + "_right.eye", rightFrame.render);
    
    printf("Left irisScale=%d, currentPupil=%.4f\n", leftFrame.irisScale, leftFrame.currentPupil);
    printf("Done!\n");
    
    return 0;
}
