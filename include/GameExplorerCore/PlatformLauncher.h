#pragma once

#include "Models.h"
#include <windows.h>
#include <string>

namespace GameExplorer {

class PlatformLauncher {
public:
    // Launches the given game enforcing platform-specific DRM client executable rules
    static bool Launch(HWND hwndOwner, const GameItem& game);

    // Platform launcher executable discovery
    static std::wstring GetSteamExecutablePath();
    static std::wstring GetEpicExecutablePath();
    static std::wstring GetGOGGalaxyExecutablePath();
    static std::wstring GetBattleNetExecutablePath();
    static std::wstring GetUbisoftExecutablePath();
    static std::wstring GetEAAppExecutablePath();
    static std::wstring GetItchExecutablePath();
    static std::wstring GetXboxExecutablePath();
    static std::wstring GetXboxIconPath();
    static std::wstring GetAmazonExecutablePath();
    static std::wstring GetLegacyGamesExecutablePath();
    static std::wstring GetJagexExecutablePath();
    static std::wstring GetXIVLauncherExecutablePath();
    static std::wstring GetGuildWars2ExecutablePath();
    static std::wstring GetRockstarExecutablePath();
    static std::wstring GetRiotExecutablePath();
    static std::wstring GetHoYoPlayExecutablePath();
    static std::wstring GetParadoxExecutablePath();
    static std::wstring GetESDEExecutablePath();
    static std::wstring GetDolphinExecutablePath();
    static std::wstring GetProject64ExecutablePath();
    static std::wstring GetPCSX2ExecutablePath();
    static std::wstring GetDuckStationExecutablePath();
    static std::wstring GetPPSSPPExecutablePath();
    static std::wstring GetmGBAExecutablePath();
    static std::wstring GetSnes9xExecutablePath();
    static std::wstring GetMelonDSExecutablePath();
    static std::wstring GetCitraExecutablePath();
    static std::wstring GetFlycastExecutablePath();
    static std::wstring GetRPCS3ExecutablePath();
    static std::wstring GetXemuExecutablePath();
    static std::wstring GetViceExecutablePath();
    static std::wstring GetRetroArchExecutablePath();
};

} // namespace GameExplorer
