#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace GameExplorer {

enum class LayoutMode : uint32_t {
    CoverArt = 0,    // Tall box art style (~2:3 or ~3:4 aspect ratio)
    HeroBanner = 1,  // Wide banner style (~16:9 or ~21:9 aspect ratio)
    DetailsView = 2  // Structured list with app icons, title, rating, etc.
};

[[nodiscard]] constexpr std::wstring_view LayoutModeToString(LayoutMode mode) noexcept {
    switch (mode) {
    case LayoutMode::CoverArt:
        return L"CoverArt";
    case LayoutMode::HeroBanner:
        return L"HeroBanner";
    case LayoutMode::DetailsView:
        return L"DetailsView";
    default:
        return L"CoverArt";
    }
}

[[nodiscard]] inline LayoutMode StringToLayoutMode(std::wstring_view str) noexcept {
    if (str == L"HeroBanner") {
        return LayoutMode::HeroBanner;
    }
    if (str == L"DetailsView") {
        return LayoutMode::DetailsView;
    }
    return LayoutMode::CoverArt;
}

enum class PlatformType : uint32_t {
    Unknown = 0,
    Steam = 1,
    Epic = 2,
    GOG = 3,
    BattleNet = 4,
    EAApp = 5,
    Xbox = 6,
    RetroArch = 7,
    Dolphin = 9,
    CustomExecutable = 10,
    Itch = 11,
    SquareEnix = 12,
    Ubisoft = 13,
    Rockstar = 14,
    Amazon = 15,
    LegacyGames = 16,
    Jagex = 17,
    XIVLauncher = 18,
    GuildWars2 = 19,
    Riot = 20,
    HoYoPlay = 21,
    Paradox = 22,
    ESDE = 23,
    Project64 = 24,
    PCSX2 = 25,
    DuckStation = 26,
    PPSSPP = 27,
    mGBA = 28,
    Snes9x = 29,
    melonDS = 30,
    Citra = 31,
    Flycast = 32,
    RPCS3 = 33,
    Xemu = 34,
    Vice = 35
};

[[nodiscard]] constexpr std::wstring_view PlatformToString(PlatformType platform) noexcept {
    switch (platform) {
    case PlatformType::Steam:
        return L"Steam";
    case PlatformType::Epic:
        return L"Epic Games";
    case PlatformType::GOG:
        return L"GOG";
    case PlatformType::BattleNet:
        return L"Battle.net";
    case PlatformType::EAApp:
        return L"EA App";
    case PlatformType::Xbox:
        return L"Xbox";
    case PlatformType::RetroArch:
        return L"RetroArch";
    case PlatformType::Dolphin:
        return L"Dolphin";
    case PlatformType::Project64:
        return L"Project64";
    case PlatformType::PCSX2:
        return L"PCSX2";
    case PlatformType::DuckStation:
        return L"DuckStation";
    case PlatformType::PPSSPP:
        return L"PPSSPP";
    case PlatformType::mGBA:
        return L"mGBA";
    case PlatformType::Snes9x:
        return L"Snes9x";
    case PlatformType::melonDS:
        return L"melonDS";
    case PlatformType::Citra:
        return L"Citra";
    case PlatformType::Flycast:
        return L"Flycast";
    case PlatformType::RPCS3:
        return L"RPCS3";
    case PlatformType::Xemu:
        return L"xemu";
    case PlatformType::Vice:
        return L"VICE";
    case PlatformType::Itch:
        return L"itch.io";
    case PlatformType::SquareEnix:
        return L"Square Enix";
    case PlatformType::Ubisoft:
        return L"Ubisoft";
    case PlatformType::Rockstar:
        return L"Rockstar Games";
    case PlatformType::Amazon:
        return L"Amazon Games";
    case PlatformType::LegacyGames:
        return L"Legacy Games";
    case PlatformType::Jagex:
        return L"Jagex Launcher";
    case PlatformType::XIVLauncher:
        return L"XIVLauncher";
    case PlatformType::GuildWars2:
        return L"Guild Wars 2";
    case PlatformType::Riot:
        return L"Riot Client";
    case PlatformType::HoYoPlay:
        return L"HoYoPlay";
    case PlatformType::Paradox:
        return L"Paradox Launcher";
    case PlatformType::ESDE:
        return L"ES-DE";
    case PlatformType::CustomExecutable:
        return L"Standalone";
    default:
        return L"PC";
    }
}

[[nodiscard]] inline PlatformType StringToPlatform(std::wstring_view str) noexcept {
    if (str == L"Steam") return PlatformType::Steam;
    if (str == L"Epic Games" || str == L"Epic") return PlatformType::Epic;
    if (str == L"GOG" || str == L"GOG Galaxy") return PlatformType::GOG;
    if (str == L"Battle.net") return PlatformType::BattleNet;
    if (str == L"EA App" || str == L"EA" || str == L"Origin") return PlatformType::EAApp;
    if (str == L"Xbox" || str == L"Microsoft Store") return PlatformType::Xbox;
    if (str == L"itch.io" || str == L"itch") return PlatformType::Itch;
    if (str == L"Square Enix") return PlatformType::SquareEnix;
    if (str == L"Ubisoft" || str == L"Uplay" || str == L"Ubisoft Connect") return PlatformType::Ubisoft;
    if (str == L"Rockstar Games" || str == L"Rockstar") return PlatformType::Rockstar;
    if (str == L"Amazon Games" || str == L"Amazon") return PlatformType::Amazon;
    if (str == L"Legacy Games") return PlatformType::LegacyGames;
    if (str == L"Jagex Launcher" || str == L"Jagex") return PlatformType::Jagex;
    if (str == L"XIVLauncher") return PlatformType::XIVLauncher;
    if (str == L"Guild Wars 2") return PlatformType::GuildWars2;
    if (str == L"Riot Client" || str == L"Riot Games") return PlatformType::Riot;
    if (str == L"HoYoPlay" || str == L"HoYoverse") return PlatformType::HoYoPlay;
    if (str == L"Paradox Launcher" || str == L"Paradox") return PlatformType::Paradox;
    if (str == L"ES-DE") return PlatformType::ESDE;
    if (str == L"RetroArch") return PlatformType::RetroArch;
    if (str == L"Dolphin") return PlatformType::Dolphin;
    if (str == L"Project64") return PlatformType::Project64;
    if (str == L"PCSX2") return PlatformType::PCSX2;
    if (str == L"DuckStation") return PlatformType::DuckStation;
    if (str == L"PPSSPP") return PlatformType::PPSSPP;
    if (str == L"mGBA") return PlatformType::mGBA;
    if (str == L"Snes9x") return PlatformType::Snes9x;
    if (str == L"melonDS") return PlatformType::melonDS;
    if (str == L"Citra") return PlatformType::Citra;
    if (str == L"Flycast") return PlatformType::Flycast;
    if (str == L"RPCS3") return PlatformType::RPCS3;
    if (str == L"xemu" || str == L"Xemu") return PlatformType::Xemu;
    if (str == L"VICE" || str == L"Vice" || str == L"vice") return PlatformType::Vice;
    if (str == L"Standalone" || str == L"Custom") return PlatformType::CustomExecutable;
    return PlatformType::Unknown;
}

} // namespace GameExplorer
