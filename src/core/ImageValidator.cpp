#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include "GameExplorerCore/ImageValidator.h"
#include "GameExplorerCore/Storage.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cmath>

namespace fs = std::filesystem;

namespace GameExplorer {

namespace {

void EnsureGdiplusInternal() {
    static std::once_flag s_once;
    static ULONG_PTR s_token = 0;
    std::call_once(s_once, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&s_token, &input, nullptr);
    });
}

bool GetPngEncoderClsid(CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return true;
        }
    }
    return false;
}

struct ContentBounds {
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    bool hasContent = false;
};

// Fast luminance gradient scan to detect bounding region of title text and focal subjects
ContentBounds DetectTextAndContentBounds(Gdiplus::Bitmap& bmp) {
    const int w = static_cast<int>(bmp.GetWidth());
    const int h = static_cast<int>(bmp.GetHeight());
    ContentBounds bounds{ 0, w, 0, h, false };
    if (w <= 0 || h <= 0) return bounds;

    int thumbW = 200;
    int thumbH = static_cast<int>(200.0 * h / w);
    if (thumbH <= 0) thumbH = 1;

    Gdiplus::Bitmap thumb(thumbW, thumbH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&thumb);
    g.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    g.DrawImage(&bmp, 0, 0, thumbW, thumbH);

    Gdiplus::Rect rect(0, 0, thumbW, thumbH);
    Gdiplus::BitmapData bmpData;
    if (thumb.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok) {
        return bounds;
    }

    std::vector<int> colGrad(thumbW, 0);
    std::vector<int> rowGrad(thumbH, 0);
    auto* pPixels = reinterpret_cast<const uint32_t*>(bmpData.Scan0);
    const int stride = bmpData.Stride / 4;

    for (int y = 1; y < thumbH - 1; ++y) {
        for (int x = 1; x < thumbW - 1; ++x) {
            auto getLum = [&](int px, int py) -> int {
                uint32_t c = pPixels[py * stride + px];
                int r = (c >> 16) & 0xFF;
                int gVal = (c >> 8) & 0xFF;
                int b = c & 0xFF;
                return (r * 299 + gVal * 587 + b * 114) / 1000;
            };
            int dx = std::abs(getLum(x + 1, y) - getLum(x - 1, y));
            int dy = std::abs(getLum(x, y + 1) - getLum(x, y - 1));
            int grad = dx + dy;
            colGrad[x] += grad;
            rowGrad[y] += grad;
        }
    }
    thumb.UnlockBits(&bmpData);

    int64_t totalGrad = 0;
    for (int v : colGrad) totalGrad += v;
    int avgColGrad = static_cast<int>(totalGrad / thumbW);

    int tLeft = 0, tRight = thumbW - 1;
    int tTop = 0, tBottom = thumbH - 1;

    const int threshCol = (avgColGrad * 6) / 10;
    for (int x = 0; x < thumbW; ++x) {
        if (colGrad[x] > threshCol) { tLeft = x; break; }
    }
    for (int x = thumbW - 1; x >= 0; --x) {
        if (colGrad[x] > threshCol) { tRight = x; break; }
    }

    int avgRowGrad = static_cast<int>(totalGrad / thumbH);
    const int threshRow = (avgRowGrad * 6) / 10;
    for (int y = 0; y < thumbH; ++y) {
        if (rowGrad[y] > threshRow) { tTop = y; break; }
    }
    for (int y = thumbH - 1; y >= 0; --y) {
        if (rowGrad[y] > threshRow) { tBottom = y; break; }
    }

    bounds.minX = static_cast<int>(static_cast<double>(tLeft) * w / thumbW);
    bounds.maxX = static_cast<int>(static_cast<double>(tRight) * w / thumbW);
    bounds.minY = static_cast<int>(static_cast<double>(tTop) * h / thumbH);
    bounds.maxY = static_cast<int>(static_cast<double>(tBottom) * h / thumbH);
    bounds.hasContent = (bounds.maxX > bounds.minX && bounds.maxY > bounds.minY);
    return bounds;
}

} // namespace

void ImageValidator::EnsureGdiplus() {
    EnsureGdiplusInternal();
}

bool ImageValidator::GetImageInfo(const std::wstring& path, ImageDimensions& outInfo) {
    outInfo = {};
    if (path.empty()) return false;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;

    // Check if .ico
    std::wstring ext = fs::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".ico") {
        HICON hIcon = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE));
        if (hIcon) {
            ICONINFO ii{};
            if (GetIconInfo(hIcon, &ii)) {
                BITMAP bm{};
                if (GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm)) {
                    outInfo.width = static_cast<unsigned int>(bm.bmWidth);
                    outInfo.height = static_cast<unsigned int>(ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2);
                    outInfo.aspectRatio = (outInfo.height > 0) ? (static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height)) : 1.0f;
                    outInfo.isAnimated = false;
                }
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
            }
            DestroyIcon(hIcon);
            return (outInfo.width > 0 && outInfo.height > 0);
        }
    }

    EnsureGdiplusInternal();
    try {
        Gdiplus::Image img(path.c_str());
        if (img.GetLastStatus() != Gdiplus::Ok) return false;

        outInfo.width = img.GetWidth();
        outInfo.height = img.GetHeight();
        if (outInfo.width == 0 || outInfo.height == 0) return false;
        outInfo.aspectRatio = static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height);

        // Check if multi-frame animated (GIF / WebP)
        GUID dimTime = Gdiplus::FrameDimensionTime;
        const UINT frameCount = img.GetFrameCount(&dimTime);
        outInfo.isAnimated = (frameCount > 1);
        if (!outInfo.isAnimated && (ext == L".gif")) {
            // Also inspect raw header for animated GIF blocks if GDI+ reports 1
            FILE* f = nullptr;
            if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
                char magic[6] = { 0 };
                if (fread(magic, 1, 6, f) == 6) {
                    if (strncmp(magic, "GIF87a", 6) == 0 || strncmp(magic, "GIF89a", 6) == 0) {
                        // Check for Graphic Control Extension (21 F9 04) indicating multiple frames
                        fseek(f, 0, SEEK_END);
                        long sz = ftell(f);
                        fseek(f, 0, SEEK_SET);
                        if (sz > 0 && sz < 10 * 1024 * 1024) {
                            std::vector<unsigned char> data(sz);
                            if (fread(data.data(), 1, sz, f) == static_cast<size_t>(sz)) {
                                int gceCount = 0;
                                for (size_t i = 0; i + 3 < data.size(); ++i) {
                                    if (data[i] == 0x21 && data[i + 1] == 0xF9 && data[i + 2] == 0x04) {
                                        gceCount++;
                                        if (gceCount > 1) {
                                            outInfo.isAnimated = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                fclose(f);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ImageValidator::IsAnimatedImage(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    return info.isAnimated;
}

bool ImageValidator::IsValidCover(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Cover MUST NOT be animated GIF (grid/list view strictly uses static images)
    if (info.isAnimated) return false;
    // Ratio enforcement is a loose requirement to fully support itch.io (e.g. 315x250, aspect ratio ~1.26)
    // and various custom storefront box art without artificial rejection (0.25f to 2.50f).
    if (info.aspectRatio < 0.25f || info.aspectRatio > 2.50f) return false;
    // Minimum resolution
    if (info.width < 32 || info.height < 32) return false;
    return true;
}

bool ImageValidator::IsValidHero(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Hero MUST NOT be animated GIF
    if (info.isAnimated) return false;
    // Loose landscape ratio requirement (0.80f to 4.50f)
    if (info.aspectRatio < 0.80f || info.aspectRatio > 4.50f) return false;
    // Minimum resolution
    if (info.width < 64 || info.height < 32) return false;
    return true;
}

bool ImageValidator::IsValidIcon(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Icon MUST NOT be animated GIF
    if (info.isAnimated) return false;
    // If .ico, valid
    std::wstring ext = fs::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".ico") return true;
    // Square ratio (0.75 - 1.33)
    if (info.aspectRatio < 0.75f || info.aspectRatio > 1.33f) return false;
    // Minimum resolution
    if (info.width < 16 || info.height < 16) return false;
    return true;
}

bool ImageValidator::IsValidDetails(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Details CAN be animated GIF!
    // Minimum resolution to avoid tiny 16x16 / 32x32 icons in 300px sidepane
    if (info.width < 100 && info.height < 100) return false;
    return true;
}

std::wstring ImageValidator::GenerateFittingCover(const std::wstring& /*appId*/, const std::wstring& /*sourceImagePath*/) {
    // Artificial cropping is intentionally disabled to avoid damaging resolution and cropping off key visual elements.
    // Authentic native 2:3 vertical covers are retrieved directly from metadata providers (SteamGridDB, Steam Store, IGDB).
    return L"";
}

std::wstring ImageValidator::GenerateFittingHero(const std::wstring& /*appId*/, const std::wstring& /*sourceImagePath*/) {
    // Artificial cropping is intentionally disabled to avoid damaging resolution and cropping off key visual elements.
    // Authentic native 3.1:1 / 16:9 widescreen heroes are retrieved directly from metadata providers.
    return L"";
}

std::wstring ImageValidator::GenerateFittingIcon(const std::wstring& /*appId*/, const std::wstring& /*sourceImagePath*/) {
    // Artificial cropping is intentionally disabled. Authentic square icons/logos are retrieved from providers.
    return L"";
}

bool ImageValidator::ValidateAndFixGameArtwork(GameItem& item) {
    bool modified = false;

    // 1. Process Details Pane Art:
    // Details pane CAN use animated GIFs, high-res static cards, or widescreen banners
    if (!item.detailsArtPath.empty()) {
        if (!IsValidDetails(item.detailsArtPath)) {
            item.detailsArtPath = L"";
            modified = true;
        }
    }

    // If an animated GIF was set on cover or hero, relocate it to detailsArtPath!
    if (IsAnimatedImage(item.coverArtPath)) {
        if (item.detailsArtPath.empty()) {
            item.detailsArtPath = item.coverArtPath;
        }
        item.coverArtPath = L"";
        modified = true;
    }
    if (IsAnimatedImage(item.heroArtPath)) {
        if (item.detailsArtPath.empty()) {
            item.detailsArtPath = item.heroArtPath;
        }
        item.heroArtPath = L"";
        modified = true;
    }
    if (IsAnimatedImage(item.iconPath)) {
        item.iconPath = L"";
        modified = true;
    }

    // 2. Validate Cover Art (~2:3 vertical box art, MUST NOT be animated GIF):
    if (!item.coverArtPath.empty() && !IsValidCover(item.coverArtPath)) {
        // If the image is ill-fitting for vertical cover, preserve original in detailsArtPath
        if (item.detailsArtPath.empty() && IsValidDetails(item.coverArtPath)) {
            item.detailsArtPath = item.coverArtPath;
        }
        item.coverArtPath = L"";
        modified = true;
    }

    // 3. Validate Hero Art (~3.1:1 / 16:9 widescreen banner, MUST NOT be animated GIF):
    if (!item.heroArtPath.empty() && !IsValidHero(item.heroArtPath)) {
        if (item.detailsArtPath.empty() && IsValidDetails(item.heroArtPath)) {
            item.detailsArtPath = item.heroArtPath;
        }
        item.heroArtPath = L"";
        modified = true;
    }

    // 4. Validate Icon Art (~1:1 square icon, MUST NOT be animated GIF):
    if (!item.iconPath.empty() && !IsValidIcon(item.iconPath)) {
        item.iconPath = L"";
        modified = true;
    }

    // 5. Ensure Details Pane has a fallback if still empty:
    if (item.detailsArtPath.empty()) {
        if (!item.heroArtPath.empty() && IsValidDetails(item.heroArtPath)) {
            item.detailsArtPath = item.heroArtPath;
            modified = true;
        } else if (!item.coverArtPath.empty() && IsValidDetails(item.coverArtPath)) {
            item.detailsArtPath = item.coverArtPath;
            modified = true;
        }
    }

    return modified;
}

} // namespace GameExplorer
