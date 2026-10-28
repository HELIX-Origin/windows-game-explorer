#pragma once

#include "GameExplorerCore/Models.h"
#include <string>

namespace GameExplorer {

enum class ArtworkSlot {
    Cover,   // Box art (~2:3 ratio or custom storefront aspect). Animated GIFs supported.
    Hero,    // Widescreen banner (~16:9 ratio). Animated GIFs supported.
    Icon     // Square icon (~1:1 ratio, e.g. 256x256, .ico). MUST NOT be animated GIF.
};

struct ImageDimensions {
    unsigned int width = 0;
    unsigned int height = 0;
    float aspectRatio = 0.0f; // width / height
    bool isAnimated = false;  // True for multi-frame animated GIFs/WebP
};

class ImageValidator {
public:
    // Initialize GDI+ subsystem safely once
    static void EnsureGdiplus();

    // Query dimensions and animation status of an image file
    static bool GetImageInfo(const std::wstring& path, ImageDimensions& outInfo);

    // Checks if the image is an animated GIF or multi-frame animated image
    static bool IsAnimatedImage(const std::wstring& path);

    // Slot-specific strict validation
    static bool IsValidCover(const std::wstring& path);
    static bool IsValidHero(const std::wstring& path);
    static bool IsValidIcon(const std::wstring& path);

    // Smart-crop generators: creates fitting images without clipping title text
    static std::wstring GenerateFittingCover(const std::wstring& appId, const std::wstring& sourceImagePath);
    static std::wstring GenerateFittingHero(const std::wstring& appId, const std::wstring& sourceImagePath);
    static std::wstring GenerateFittingIcon(const std::wstring& appId, const std::wstring& sourceImagePath);

    // Validates all artwork slots on a GameItem and generates missing/ill-fitting slots
    static bool ValidateAndFixGameArtwork(GameItem& item);
};

} // namespace GameExplorer
