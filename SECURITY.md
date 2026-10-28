# Security Policy

We take the security of **Windows Game Explorer** seriously — for the shell extension and the Game Explorer Studio GUI, and for the integrity of your Windows environment.

## 🛡️ Supported Versions

| Version | Supported |
| --- | --- |
| 2.7.x | ✅ Active |
| < 2.7 | ❌ No longer supported |

Only the latest release receives security fixes. See the [Releases page](https://github.com/HELIX-Origin/windows-game-explorer/releases) for the current version.

## 🚨 Reporting a Vulnerability

**Do not open a public issue** for a security vulnerability.

Report privately using GitHub's **private vulnerability reporting**:

- Repository → **Security** → **Report a vulnerability**, or
- Directly: `https://github.com/HELIX-Origin/windows-game-explorer/security`

Please include:

1. The affected version(s) and Windows build number.
2. A clear description of the vulnerability.
3. Minimal steps to reproduce.
4. Impact assessment and — if you have one — a suggested fix.

We will try to review and respond to reports as time and capacity permit, but we make no assumptions or promises regarding response times or fixed turnaround windows. Confirmed security fixes will be documented in [CHANGELOG.md](./CHANGELOG.md).

## 🎯 Scope

In scope:
- Buffer overflows, memory corruption, or unhandled exceptions inside `GameExplorerShell.dll` or `GameExplorerStudio.exe`.
- Code execution, path traversal, or arbitrary file writes via manifest parsing or metadata JSON loading.
- Privilege escalation via COM registration (`HKCU` registry keys).

Out of scope:
- Issues requiring physical machine access or pre-existing administrator root compromise.
- Denial of service via local process termination.
- Upstream vulnerabilities in third-party storefront clients.

## 🔒 Data Hygiene

Windows Game Explorer operates on a **100% local-only, privacy-first model**:
- No telemetry, analytics, or tracking data is sent to any external servers.
- The extension registers strictly under per-user registry hives (`HKCU\Software\Classes`) without requiring administrator privileges for standard operation.
- The application never modifies external launcher proprietary configuration files.
