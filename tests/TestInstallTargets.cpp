#include "TestHarness.h"

#include "GameExplorerCore/InstallTargets.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace GameExplorer;

namespace {

std::wstring MakeTempBase(const wchar_t* label) {
    std::filesystem::path root = std::filesystem::temp_directory_path();
    root /= L"GameExplorerInstallTests";
    root /= std::wstring(label) + L"_" + std::to_wstring(GetCurrentProcessId());
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return root.wstring();
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return {};
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

} // namespace

TEST_CASE(InstallTargets_StoreLayoutsAreVerifiedOnly) {
    ASSERT_EQ(StoreInstallFolderName(L"steam"), std::wstring(L"Steam"));
    ASSERT_EQ(StoreInstallFolderName(L"gog"), std::wstring(L"GOG Games"));
    ASSERT_EQ(StoreInstallFolderName(L"unknownstore"), std::wstring(L"unknownstore"));

    const StoreInstallLayout steam = DescribeStoreInstallLayout(L"steam");
    ASSERT_EQ(steam.markerFile, std::wstring(L"libraryfolder.vdf"));
    ASSERT_EQ(steam.requiredDirectories.size(), static_cast<size_t>(3));
    ASSERT_FALSE(steam.note.empty());

    // Epic tracks installs in its own manifests, so the folder needs no marker of ours.
    const StoreInstallLayout epic = DescribeStoreInstallLayout(L"epic");
    ASSERT_TRUE(epic.markerFile.empty());
    ASSERT_TRUE(epic.requiredDirectories.empty());
    ASSERT_FALSE(epic.note.empty());

    // Nothing is invented for stores with no verified library format.
    const StoreInstallLayout other = DescribeStoreInstallLayout(L"battlenet");
    ASSERT_TRUE(other.markerFile.empty());
    ASSERT_TRUE(other.requiredDirectories.empty());
}

TEST_CASE(InstallTargets_SteamFolderIsScaffoldedOnce) {
    const std::wstring base = MakeTempBase(L"steam");
    std::wstring folder;
    std::wstring error;

    ASSERT_TRUE(EnsureStoreLibraryFolder(base, L"steam", folder, error));
    ASSERT_TRUE(error.empty());
    ASSERT_EQ(folder, (std::filesystem::path(base) / L"Steam").wstring());

    const std::filesystem::path steamFolder(folder);
    ASSERT_TRUE(std::filesystem::is_directory(steamFolder / L"steamapps" / L"common"));
    ASSERT_TRUE(std::filesystem::is_directory(steamFolder / L"steamapps" / L"downloading"));
    ASSERT_TRUE(std::filesystem::is_directory(steamFolder / L"steamapps" / L"temp"));
    ASSERT_TRUE(std::filesystem::is_regular_file(steamFolder / L"libraryfolder.vdf"));

    const std::string marker = ReadTextFile(steamFolder / L"libraryfolder.vdf");
    ASSERT_TRUE(marker.find("\"libraryfolder\"") != std::string::npos);
    ASSERT_TRUE(marker.find("\"contentid\"") != std::string::npos);
    ASSERT_TRUE(marker.find("\"label\"") != std::string::npos);

    // A second call must not rewrite an existing library's identity.
    ASSERT_TRUE(EnsureStoreLibraryFolder(base, L"steam", folder, error));
    ASSERT_EQ(ReadTextFile(steamFolder / L"libraryfolder.vdf"), marker);

    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE(InstallTargets_OtherStoreGetsFolderOnly) {
    const std::wstring base = MakeTempBase(L"epic");
    std::wstring folder;
    std::wstring error;

    ASSERT_TRUE(EnsureStoreLibraryFolder(base, L"epic", folder, error));
    const std::filesystem::path epicFolder(folder);
    ASSERT_TRUE(std::filesystem::is_directory(epicFolder));
    ASSERT_FALSE(std::filesystem::exists(epicFolder / L"libraryfolder.vdf"));

    std::error_code ec;
    std::filesystem::remove_all(base, ec);
}

TEST_CASE(InstallTargets_EmptyBaseIsRejected) {
    std::wstring folder;
    std::wstring error;
    ASSERT_FALSE(EnsureStoreLibraryFolder(L"", L"steam", folder, error));
    ASSERT_TRUE(folder.empty());
    ASSERT_FALSE(error.empty());
}

TEST_CASE(InstallTargets_SteamMarkerFormat) {
    const std::string marker = BuildSteamLibraryFolderVdf("76561198000000000");
    ASSERT_EQ(marker,
              std::string("\"libraryfolder\"\n{\n\t\"contentid\"\t\t\"76561198000000000\"\n"
                          "\t\"label\"\t\t\"\"\n}\n"));

    const std::string id = NewSteamLibraryContentId();
    ASSERT_TRUE(id.size() >= 17 && id.size() <= 19);
    ASSERT_TRUE(id.find_first_not_of("0123456789") == std::string::npos);
}
