#pragma once

#include <guiddef.h>

// CLSID_GameExplorer: {1666B3DF-60A5-4EF3-8B80-962C82401CD1}
// Class ID for the Windows Game Explorer Virtual Shell Namespace Extension
inline constexpr GUID CLSID_GameExplorer = {
    0x1666b3df,
    0x60a5,
    0x4ef3,
    { 0x8b, 0x80, 0x96, 0x2c, 0x82, 0x40, 0x1c, 0xd1 }
};

// FOLDERID_GameExplorer: {E1A11BB3-192F-4917-B2FC-5F0832341709}
// Known folder definition ID for the user-scoped Game Explorer root
inline constexpr GUID FOLDERID_GameExplorer = {
    0xe1a11bb3,
    0x192f,
    0x4917,
    { 0xb2, 0xfc, 0x5f, 0x08, 0x32, 0x34, 0x17, 0x09 }
};

// Canonical Windows SDK FOLDERID_Games: {CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}
// Used by Windows Shell for the 'shell:Games' known folder alias
inline constexpr GUID FOLDERID_Games_Canonical = {
    0xcac52c1a,
    0xb53d,
    0x4edc,
    { 0x92, 0xd7, 0x6b, 0x2e, 0x8a, 0xc1, 0x94, 0x34 }
};

// CLSID_GameCategorizer: {C74A2736-2B1A-4F3D-9E8B-7A2E5B1D3C4F}
// Categorizer for Game Providers vs Games grouping
inline constexpr GUID CLSID_GameCategorizer = {
    0xc74a2736,
    0x2b1a,
    0x4f3d,
    { 0x9e, 0x8b, 0x7a, 0x2e, 0x5b, 0x1d, 0x3c, 0x4f }
};

inline constexpr const wchar_t* SZ_CLSID_GAME_EXPLORER = L"{1666B3DF-60A5-4EF3-8B80-962C82401CD1}";
inline constexpr const wchar_t* SZ_CLSID_GAME_CATEGORIZER = L"{C74A2736-2B1A-4F3D-9E8B-7A2E5B1D3C4F}";
inline constexpr const wchar_t* SZ_FOLDERID_GAME_EXPLORER = L"{E1A11BB3-192F-4917-B2FC-5F0832341709}";
inline constexpr const wchar_t* SZ_FOLDERID_GAMES_CANONICAL = L"{CAC52C1A-B53D-4EDC-92D7-6B2E8AC19434}";
inline constexpr const wchar_t* SZ_FOLDERTYPEID_GENERIC = L"{5c4f28b5-f869-4e84-8e60-f11db97c5cc7}";
inline constexpr const wchar_t* SZ_GAME_EXPLORER_TITLE = L"Games";
inline constexpr const wchar_t* SZ_GAME_EXPLORER_NAME = L"Games";
