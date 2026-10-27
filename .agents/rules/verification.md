# Verification Gate — windows-game-explorer (Windows Game Explorer)

> Authoritative verification gate for `windows-game-explorer` (Windows Game Explorer).
> All compilers and linters must pass with zero errors and zero warnings (`/W4 /WX`).

---

## 1. Primary Verification Commands

```powershell
# 1. Native C++ build — MSVC warning level /W4 with warnings treated as errors (/WX)
.\game-explorer.bat build Release

# 2. Native unit & integration test suite
.\game-explorer.bat test Release

# 3. PowerShell static analysis — zero PSScriptAnalyzer diagnostics
.\game-explorer.bat lint

# 4. Shell COM extension registration check (if testing live registration)
regsvr32 /s build\Release\GameExplorerShell.dll
regsvr32 /u /s build\Release\GameExplorerShell.dll
```

---

## 2. Zero-Warning Enforcement

- Every build step must produce **0 errors and 0 warnings**.
- Treat all MSVC compiler warnings as fatal (`/WX`).
- All unit tests in `GameExplorerTests.exe` must pass cleanly.

---

## 3. Failure Modes & Remediation

| Check | Failure Cause | Remediation |
|---|---|---|
| MSVC `/W4` | Unused variable / unreferenced parameter | Prefix with `[[maybe_unused]]` or remove |
| MSVC `/W4` | Type truncation / signed-unsigned mismatch | Use explicit `static_cast` or size types (`size_t`) |
| COM Registration | Missing DLL export or registry privilege | Check `DllRegisterServer` exports in `.def` file |
| Manifest Scanner | File lock or invalid JSON/VDF syntax | Use non-blocking read mode (`FILE_SHARE_READ`) |
