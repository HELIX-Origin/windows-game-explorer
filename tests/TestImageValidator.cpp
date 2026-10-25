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

    // 1. Cover validation (loose aspect ratio 0.25 - 2.50 to support itch.io and custom covers)
    ASSERT_TRUE(ImageValidator::IsValidCover(coverImg));
    ASSERT_TRUE(ImageValidator::IsValidCover(posterImg));
    ASSERT_TRUE(ImageValidator::IsValidCover(cardImg)); // 315x250 (1.26) itch.io cover is valid
    ASSERT_TRUE(ImageValidator::IsValidCover(iconImg));

    // 2. Hero validation (landscape widescreen, ratio 0.80 - 4.50)
    ASSERT_TRUE(ImageValidator::IsValidHero(heroImg));
    ASSERT_TRUE(ImageValidator::IsValidHero(cardImg));
    ASSERT_FALSE(ImageValidator::IsValidHero(coverImg)); // 2:3 (0.67) cannot be hero

    // 3. Icon validation (strictly square, 0.75 - 1.33)
    ASSERT_TRUE(ImageValidator::IsValidIcon(iconImg));
    ASSERT_FALSE(ImageValidator::IsValidIcon(coverImg)); // 2:3 cannot be icon
    ASSERT_FALSE(ImageValidator::IsValidIcon(heroImg));  // 16:9 cannot be icon

    // 4. Details Pane validation (accepts all high-res showcase layouts)
    ASSERT_TRUE(ImageValidator::IsValidDetails(coverImg));
    ASSERT_TRUE(ImageValidator::IsValidDetails(heroImg));
    ASSERT_TRUE(ImageValidator::IsValidDetails(cardImg));
}

TEST_CASE(ImageValidatorAssetPreservationAndNonDestructiveValidation) {
    Storage::EnsureDirectoriesCreated();
    const fs::path tempDir = Storage::GetCachePath() / L"test_validator";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    // Source image: a 315x250 card with centered title text
    const std::wstring cardImg = (tempDir / L"crop_source_315x250.png").wstring();
    ASSERT_TRUE(SaveDummyImage(cardImg, 315, 250, Gdiplus::Color(255, 70, 90, 150)));

    // 1. Artificial cropping is intentionally disabled to prevent image resolution degradation
    std::wstring genCover = ImageValidator::GenerateFittingCover(L"test_crop_game", cardImg);
    ASSERT_TRUE(genCover.empty());

    // 2. Authentic itch.io covers (315x250) are preserved in coverArtPath under the loose ratio requirement
    GameItem item{};
    item.appId = L"test_fix_game";
    item.title = L"Test Artwork Fixing Game";
    item.coverArtPath = cardImg; // 315x250 static cover
    item.heroArtPath = cardImg;  // Shared hero/card
    item.detailsArtPath = L"";

    ASSERT_TRUE(ImageValidator::ValidateAndFixGameArtwork(item));

    // Verify authentic static cover was preserved in coverArtPath and detailsArtPath
    ASSERT_EQ(item.coverArtPath, cardImg);
    ASSERT_TRUE(ImageValidator::IsValidCover(item.coverArtPath));
    ASSERT_EQ(item.detailsArtPath, cardImg);
    ASSERT_TRUE(ImageValidator::IsValidDetails(item.detailsArtPath));
}
