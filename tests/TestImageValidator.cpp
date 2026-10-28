#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include "TestHarness.h"
#include "GameExplorerCore/ImageValidator.h"
#include "GameExplorerCore/Storage.h"
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace GameExplorer;

namespace {

bool SaveDummyImage(const std::wstring& path, int width, int height, Gdiplus::Color color) {
    ImageValidator::EnsureGdiplus();
    Gdiplus::Bitmap bmp(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&bmp);
    Gdiplus::SolidBrush brush(color);
    g.FillRectangle(&brush, 0, 0, width, height);

    // Draw some high-contrast simulated title text
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&textBrush, width / 4, height / 3, width / 2, height / 6);

    CLSID pngClsid{};
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* pInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, pInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pInfo[j].MimeType, L"image/png") == 0) {
            pngClsid = pInfo[j].Clsid;
            break;
        }
    }
    return (bmp.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok);
}

} // namespace

TEST_CASE(ImageValidatorSlotDimensionsAndAspectRatios) {
    Storage::EnsureDirectoriesCreated();
    const fs::path tempDir = Storage::GetCachePath() / L"test_validator";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    const std::wstring coverImg = (tempDir / L"test_cover_600x900.png").wstring();
    const std::wstring posterImg = (tempDir / L"test_poster_600x800.png").wstring();
    const std::wstring heroImg  = (tempDir / L"test_hero_960x310.png").wstring();
    const std::wstring iconImg  = (tempDir / L"test_icon_256x256.png").wstring();
    const std::wstring cardImg  = (tempDir / L"test_card_315x250.png").wstring();

    ASSERT_TRUE(SaveDummyImage(coverImg, 600, 900, Gdiplus::Color(255, 40, 80, 160)));
    ASSERT_TRUE(SaveDummyImage(posterImg, 600, 800, Gdiplus::Color(255, 45, 85, 170)));
    ASSERT_TRUE(SaveDummyImage(heroImg, 960, 310, Gdiplus::Color(255, 160, 40, 80)));
    ASSERT_TRUE(SaveDummyImage(iconImg, 256, 256, Gdiplus::Color(255, 50, 180, 70)));
    ASSERT_TRUE(SaveDummyImage(cardImg, 315, 250, Gdiplus::Color(255, 120, 120, 120)));

    // 1. Cover validation (tall portrait and square covers, ratio 0.45 - 1.15)
    ASSERT_TRUE(ImageValidator::IsValidCover(coverImg));
    ASSERT_TRUE(ImageValidator::IsValidCover(posterImg));
    ASSERT_TRUE(ImageValidator::IsValidCover(iconImg));  // 256x256 (1.00) square image is valid for cover
    ASSERT_FALSE(ImageValidator::IsValidCover(cardImg)); // 315x250 (1.26) wide banner is not a cover

    // 2. Hero validation (strictly wide landscape heroes, ratio 1.20 - 4.50)
    ASSERT_TRUE(ImageValidator::IsValidHero(heroImg));
    ASSERT_TRUE(ImageValidator::IsValidHero(cardImg));
    ASSERT_FALSE(ImageValidator::IsValidHero(coverImg)); // 2:3 (0.67) tall cannot be hero
    ASSERT_FALSE(ImageValidator::IsValidHero(iconImg));  // 1:1 square cannot be hero

    // 3. Icon validation (strictly square, 0.75 - 1.33)
    ASSERT_TRUE(ImageValidator::IsValidIcon(iconImg));
    ASSERT_FALSE(ImageValidator::IsValidIcon(coverImg)); // 2:3 cannot be icon
    ASSERT_FALSE(ImageValidator::IsValidIcon(heroImg));  // 16:9 cannot be icon
}

TEST_CASE(ImageValidatorAssetPreservationAndNonDestructiveValidation) {
    Storage::EnsureDirectoriesCreated();
    const fs::path tempDir = Storage::GetCachePath() / L"test_validator";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    // Source images: a 600x900 tall cover and 960x310 wide hero
    const std::wstring validCover = (tempDir / L"valid_cover_600x900.png").wstring();
    const std::wstring validHero = (tempDir / L"valid_hero_960x310.png").wstring();
    ASSERT_TRUE(SaveDummyImage(validCover, 600, 900, Gdiplus::Color(255, 70, 90, 150)));
    ASSERT_TRUE(SaveDummyImage(validHero, 960, 310, Gdiplus::Color(255, 150, 90, 70)));

    // 1. Artificial cropping is intentionally disabled to prevent image resolution degradation
    std::wstring genCover = ImageValidator::GenerateFittingCover(L"test_crop_game", validCover);
    ASSERT_TRUE(genCover.empty());

    // 2. Authentic tall covers and wide heroes are preserved without unnecessary mutation
    GameItem item{};
    item.appId = L"test_fix_game";
    item.title = L"Test Artwork Fixing Game";
    item.coverArtPath = validCover;
    item.heroArtPath = validHero;

    // Verify that valid artwork is preserved intact without unnecessary mutation
    ASSERT_FALSE(ImageValidator::ValidateAndFixGameArtwork(item));

    ASSERT_EQ(item.coverArtPath, validCover);
    ASSERT_TRUE(ImageValidator::IsValidCover(item.coverArtPath));
    ASSERT_EQ(item.heroArtPath, validHero);
    ASSERT_TRUE(ImageValidator::IsValidHero(item.heroArtPath));

    // 3. Verify that ill-fitting or non-existent slots are cleared
    item.iconPath = L"non_existent_fake_icon.ico";
    ASSERT_TRUE(ImageValidator::ValidateAndFixGameArtwork(item));
    ASSERT_TRUE(item.iconPath.empty());
}
