#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <wincodec.h>
#include "GameExplorerCore/ImageValidator.h"
#include "GameExplorerCore/Storage.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstdio>

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

    std::wstring ext = fs::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    // 1. Check if .ico (Win32 icon resource)
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
            if (outInfo.width > 0 && outInfo.height > 0) return true;
        }
    }

    // 2. Check if .svg (Scalable Vector Graphics)
    if (ext == L".svg") {
        outInfo.isAnimated = false;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rt") == 0 && f) {
            char buf[8192] = { 0 };
            const size_t bytesRead = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            if (bytesRead > 0) {
                buf[bytesRead] = '\0';
                std::string content(buf);
                size_t vb = content.find("viewBox=\"");
                if (vb == std::string::npos) vb = content.find("viewBox=\'");
                if (vb != std::string::npos) {
                    float vx = 0, vy = 0, vw = 0, vh = 0;
                    if (sscanf_s(content.c_str() + vb + 9, "%f %f %f %f", &vx, &vy, &vw, &vh) == 4 ||
                        sscanf_s(content.c_str() + vb + 9, "%f, %f, %f, %f", &vx, &vy, &vw, &vh) == 4) {
                        if (vw > 0 && vh > 0) {
                            outInfo.width = static_cast<unsigned int>(vw);
                            outInfo.height = static_cast<unsigned int>(vh);
                            outInfo.aspectRatio = vw / vh;
                            return true;
                        }
                    }
                }
                // Fallback for scalable vector art without explicit viewBox
                outInfo.width = 512;
                outInfo.height = 512;
                outInfo.aspectRatio = 1.0f;
                return true;
            }
        }
    }

    // 3. Try GDI+ (PNG, JPEG, GIF, BMP, TIFF)
    EnsureGdiplusInternal();
    try {
        Gdiplus::Image img(path.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok) {
            outInfo.width = img.GetWidth();
            outInfo.height = img.GetHeight();
            if (outInfo.width > 0 && outInfo.height > 0) {
                outInfo.aspectRatio = static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height);
                GUID dimTime = Gdiplus::FrameDimensionTime;
                const UINT frameCount = img.GetFrameCount(&dimTime);
                outInfo.isAnimated = (frameCount > 1);
                if (!outInfo.isAnimated && (ext == L".gif")) {
                    FILE* f = nullptr;
                    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
                        char magic[6] = { 0 };
                        if (fread(magic, 1, 6, f) == 6) {
                            if (strncmp(magic, "GIF87a", 6) == 0 || strncmp(magic, "GIF89a", 6) == 0) {
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
            }
        }
    } catch (...) {}

    // 4. Try WIC (Windows Imaging Component) - supports WebP, AVIF, HEIC, TIFF, etc.
    try {
        IWICImagingFactory* pFactory = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pFactory)
        );
        if (SUCCEEDED(hr) && pFactory) {
            IWICBitmapDecoder* pDecoder = nullptr;
            hr = pFactory->CreateDecoderFromFilename(
                path.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &pDecoder
            );
            if (SUCCEEDED(hr) && pDecoder) {
                UINT frameCount = 0;
                pDecoder->GetFrameCount(&frameCount);
                outInfo.isAnimated = (frameCount > 1);

                IWICBitmapFrameDecode* pFrame = nullptr;
                hr = pDecoder->GetFrame(0, &pFrame);
                if (SUCCEEDED(hr) && pFrame) {
                    pFrame->GetSize(&outInfo.width, &outInfo.height);
                    if (outInfo.height > 0) {
                        outInfo.aspectRatio = static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height);
                    }
                    pFrame->Release();
                }
                pDecoder->Release();
            }
            pFactory->Release();
            if (outInfo.width > 0 && outInfo.height > 0) return true;
        }
    } catch (...) {}

    // 5. Binary WebP Header Parser (RIFF....WEBP: VP8, VP8L, VP8X animated)
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
        unsigned char hdr[32] = { 0 };
        if (fread(hdr, 1, sizeof(hdr), f) >= 30) {
            if (memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WEBP", 4) == 0) {
                if (memcmp(hdr + 12, "VP8X", 4) == 0) {
                    const unsigned char flags = hdr[20];
                    outInfo.isAnimated = (flags & 0x02) != 0;
                    outInfo.width = 1 + (hdr[24] | (hdr[25] << 8) | (hdr[26] << 16));
                    outInfo.height = 1 + (hdr[27] | (hdr[28] << 8) | (hdr[29] << 16));
                    outInfo.aspectRatio = (outInfo.height > 0) ? (static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height)) : 1.0f;
                    fclose(f);
                    return true;
                } else if (memcmp(hdr + 12, "VP8 ", 4) == 0) {
                    if (hdr[23] == 0x9D && hdr[24] == 0x01 && hdr[25] == 0x2A) {
                        outInfo.width = (hdr[26] | (hdr[27] << 8)) & 0x3FFF;
                        outInfo.height = (hdr[28] | (hdr[29] << 8)) & 0x3FFF;
                        outInfo.aspectRatio = (outInfo.height > 0) ? (static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height)) : 1.0f;
                        outInfo.isAnimated = false;
                        fclose(f);
                        return true;
                    }
                } else if (memcmp(hdr + 12, "VP8L", 4) == 0) {
                    if (hdr[20] == 0x2F) {
                        const uint32_t b0 = hdr[21];
                        const uint32_t b1 = hdr[22];
                        const uint32_t b2 = hdr[23];
                        const uint32_t b3 = hdr[24];
                        outInfo.width = 1 + (b0 | ((b1 & 0x3F) << 8));
                        outInfo.height = 1 + (((b1 >> 6) | (b2 << 2) | ((b3 & 0x0F) << 10)));
                        outInfo.aspectRatio = (outInfo.height > 0) ? (static_cast<float>(outInfo.width) / static_cast<float>(outInfo.height)) : 1.0f;
                        outInfo.isAnimated = false;
                        fclose(f);
                        return true;
                    }
                }
            }
        }
        fclose(f);
    }

    return false;
}

bool ImageValidator::IsAnimatedImage(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    return info.isAnimated;
}

bool ImageValidator::IsValidCover(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Animated GIFs are supported in cover view mode
    // Covers are tall (portrait) or square: ratio must respect the cover viewport (0.45f to 1.15f).
    // Wide (>1.15f) images do not fit the cover slot.
    if (info.aspectRatio < 0.45f || info.aspectRatio > 1.15f) return false;
    // Minimum resolution
    if (info.width < 32 || info.height < 32) return false;
    return true;
}

bool ImageValidator::IsValidHero(const std::wstring& path) {
    ImageDimensions info{};
    if (!GetImageInfo(path, info)) return false;
    // Animated GIFs are supported in hero view mode
    // Heroes are wide (landscape): ratio must respect the wide viewport (1.20f to 4.50f).
    // Tall (<1.0) and square (1.0) images do not fit the hero slot.
    if (info.aspectRatio < 1.20f || info.aspectRatio > 4.50f) return false;
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

    // Square icons must not be animated GIFs
    if (IsAnimatedImage(item.iconPath)) {
        item.iconPath = L"";
        modified = true;
    }

    // 1. Validate Cover Art (~2:3 vertical box art or custom storefront aspect, animated GIFs supported):
    if (!item.coverArtPath.empty() && !IsValidCover(item.coverArtPath)) {
        item.coverArtPath = L"";
        modified = true;
    }

    // 2. Validate Hero Art (~3.1:1 / 16:9 widescreen banner, animated GIFs supported):
    if (!item.heroArtPath.empty() && !IsValidHero(item.heroArtPath)) {
        item.heroArtPath = L"";
        modified = true;
    }

    // 3. Validate Icon Art (~1:1 square icon, MUST NOT be animated GIF):
    if (!item.iconPath.empty() && !IsValidIcon(item.iconPath)) {
        item.iconPath = L"";
        modified = true;
    }

    return modified;
}

} // namespace GameExplorer
