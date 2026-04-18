# Installer README

This document describes the Windows packaging workflow for the MnL MCP server and the self-contained installer produced by the repository's packaging scripts.

Summary
- A single NSIS installer (mcp_server_installer.exe) bundles:
  - Single-file C# launcher (WinForms) used to start/stop the server and view logs
  - Native server executable (mcp_stdio_server.exe)
  - Python embeddable runtime
  - Compiled Python extension modules (.pyd)
  - Optional NSSM (service helper) to install the server as a Windows service
  - MnL icon (assets/icon/MnLLogo.ico) used for shortcuts and installer

Prerequisites (packaging machine)
- Windows (10/11)
- .NET SDK (for dotnet publish)
- PowerShell (packaging script uses it)
- CMake + native toolchain (to produce build artifacts beforehand)
- makensis (NSIS) — required to build the final installer. Installable via Chocolatey:
```powershell
choco install nsis -y
```

Quick packaging (automated)
1. Build the project and Python extensions first (see top-level README).
2. From repository root:
```powershell
# publishes launcher, stages artifacts, downloads embeddable and NSSM (if available),
# and runs makensis if NSIS is on PATH.
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1
```
- Parameters:
  - `-Runtime` (default: win-x64)
  - `-PythonVersion` (default: 3.14.0)
  - `-NssmVersion` (default: 2.24)
Example:
```powershell
powershell -File installer\build_installer.ps1 -PythonVersion "3.14.0" -NssmVersion "2.24"
```

Notes about NSSM
- The script tries to download NSSM; if the download fails (server unavailable), place an NSSM release zip at:
  - `%TEMP%\nssm-<version>.zip`
- The script will extract `nssm.exe` and place it under `installer/staging/nssm/nssm.exe` so NSIS can bundle it.
- The installer exposes a checkbox to "Install and start service after installation" — if checked, the installer runs `nssm.exe install MCPServer <path-to-exe>` and configures stdout/stderr to `<InstallDir>\logs\...`.

Manual NSIS build
- If the packaging script reported that makensis is missing, build manually after staging is prepared:
```powershell
# ensure NSIS installed
"C:\Program Files (x86)\NSIS\makensis.exe" -V4 installer\installer.nsi
```
- The produced EXE will be `installer\mcp_server_installer.exe`.

Where to find staged files
- The script prepares `installer/staging/` with the files NSIS will bundle (launcher, .pyd modules, mcp_stdio_server.exe, python embeddable, MnLLogo.ico, optional nssm/).

Installer behavior
- Installs files to `%ProgramFiles%\MCP Server` by default (user can change during install).
- Creates Start Menu and Desktop shortcuts using the embedded MnL icon.
- Creates an Uninstall entry and uninstaller.
- If service option selected, installs a service named `MCPServer` via NSSM and writes logs to `<InstallDir>\logs\`.

Signing & Release
- Sign the final installer before publishing:
```powershell
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 /v .\installer\mcp_server_installer.exe
```
- CI: produce artifacts (native exe, .pyd, installer) as separate release assets rather than committing staged binaries to the repo.

Troubleshooting
- makensis not found: install NSIS or run the installer build step on a packaging machine with NSIS in PATH.
- NSSM download error: place NSSM zip at `%TEMP%` and re-run script, or add `nssm.exe` manually to `installer/staging/nssm/`.
- If server fails to start under NSSM, check `<InstallDir>\logs\stdout.log` and `stderr.log` for errors and ensure working directory and PATH are correct.

Developer notes
- Staging artifacts are intentionally not committed. Use the packaging script in CI to produce release artifacts.
- Adjust `installer/installer.nsi` and `installer/build_installer.ps1` if the installer UX or service configuration needs to change (service name, log locations, shortcut names).
- For reproducible builds, pin `-Runtime`, `-PythonVersion`, and `-NssmVersion` when running the script.