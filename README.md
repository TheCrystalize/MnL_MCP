# MnL_MCP

Minimal Native Language (MnL) — MCP stdio server, native helpers, and Python bindings.

This repository contains:
- A C++ stdio server (mcp_stdio_server) that embeds Python and exposes MCP JSON-RPC endpoints.
- Native pybind11 modules (lean_bindings, z3_bindings, z3_capi, sympy_bindings).
- Cython helpers (cython/).
- Packaging/installer tooling: a WinForms launcher and NSIS packaging scripts (installer/).

Status: ready for initial push. Build and packaging scripts are included; installer staging is prepared by installer/build_installer.ps1.

Prerequisites
- Windows 10/11 (developer workstation)
- CMake >= 3.15, Git, a C++20 toolchain (Visual Studio 2022 recommended)
- Python 3.14 (interpreter + development headers)
- .NET SDK (for the C# WinForms launcher)
- Optional: Ninja, NSIS (makensis) for building the installer; Chocolatey for convenience.

Quick build (recommended — Visual Studio generator)
```powershell
# configure (one-time)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTS=ON

# build (uses existing generator in build/)
cmake --build build --config Release -- /m
```

Alternative (reproducible single-config with Ninja)
```bash
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build-ninja --parallel
```

Python extensions and site-packages
- Built Python extension modules live in `build/src/Release/` (for MSVC multi-config builds) or `build-ninja/...` for single-config.
- For development, add that directory to PYTHONPATH:
```powershell
$env:PYTHONPATH = "$PWD\build\src\Release;$PWD\cython"
python scripts\verify_mcp_z3_import.py
python scripts\check_sympy_bindings.py
```
- To install into the active Python environment:
```powershell
python -c "import sysconfig; print(sysconfig.get_paths()['platlib'])"
Copy-Item .\build\src\Release\*.pyd -Destination "<site-packages-path>"
```

Run the server locally
```powershell
# run the built native server directly
.\build\src\Release\mcp_stdio_server.exe
```
Or use the provided WinForms launcher (produced by `installer/build_installer.ps1`) to start/stop the server and view logs.

Tests & verification
```powershell
cd build
ctest -C Release --output-on-failure

# verify Python bindings (example)
powershell -Command "$env:PYTHONPATH = \"$PWD\src\Release;$PWD\cython\"; python ..\scripts\verify_mcp_z3_import.py"
python scripts\check_sympy_bindings.py
```

Packaging / Installer
- A packaging helper is at `installer/build_installer.ps1`. It:
  - Publishes the C# WinForms launcher (single-file),
  - Copies built .pyd modules and server exe into a staging folder,
  - Downloads the Python embeddable and (optionally) NSSM,
  - Runs `makensis` (if available) to produce `installer\mcp_server_installer.exe`.
- Build the installer:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer\build_installer.ps1
# or, if NSIS is installed:
"C:\Program Files (x86)\NSIS\makensis.exe" -V4 installer\installer.nsi
```
- Installer features:
  - Embedded MnL icon (assets/icon/MnLLogo.ico),
  - Desktop and Start Menu shortcuts (with icon),
  - Optional service install via bundled NSSM (service name: `MCPServer`), logs written to `<InstallDir>\logs\`.

Release & signing
- Sign binaries before publishing:
```powershell
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 /v path\to\mcp_server_installer.exe
```
- CI recommendation:
  - Build native + extensions on Windows runner.
  - Run `installer/build_installer.ps1` in CI agent that has dotnet and makensis (or produce staging artifacts and run NSIS on a packaging runner).
  - Publish artifacts (mcp_stdio_server.exe, *.pyd, mcp_server_installer.exe) to release assets.

Repository housekeeping (first-push checklist)
- Update or confirm LICENSE file (not included by default).
- Confirm branch protection and CI pipeline.
- Verify that large binaries / staging artifacts are not committed (use release artifacts in GitHub releases instead).

Initial push example
```bash
git checkout -b main
git add .
git commit -m "Initial import: core + build and installer tooling"
git push -u origin main
```

Notes for reviewers / maintainers
- If the build directory already contains a CMake cache configured with a different generator, either remove `build/CMakeCache.txt` and `build/CMakeFiles/` or use a new binary dir (e.g., `build-ninja`).
- The installer build downloads network resources (Python embeddable, NSSM) — CI runners should have controlled caching or mirrors.
- If NSSM download fails, place the NSSM zip at the temp path reported by the packaging script or set `NssmVersion` when invoking the script.

.gitignore (recommended additions)
- The repo already ignores common files; before first push ensure the following are present (these are recommended and will be added/updated in `.gitignore`):
```
# Visual Studio / .NET
.vs/
**/bin/
**/obj/
*.user
*.suo
*.pdb

# CMake / build artifacts
/build/
/build-*/
CMakeCache.txt
CMakeFiles/
Makefile
cmake_install.cmake
install_manifest.txt
*.exe
*.dll
*.pyd
*.so
*.lib

# NSIS/installer staging
installer/staging/
installer/*.exe
installer/*.zip

# Python
.venv/
__pycache__/
*.pyc
```

Repository
Remote: https://github.com/TheCrystalize/MnL_MCP

The repository will be initialized locally and the initial import pushed to the above remote.

To push the initial import:
```bash
git init
git checkout -b main
git add .
git commit -m "Initial import: core + build and installer tooling"
git remote add origin https://github.com/TheCrystalize/MnL_MCP
git push -u origin main
```

Next actions (suggested)
- Confirm or add NOTICE (if required) and LICENSE contents.
- Verify packaging on a clean Windows packaging machine and sign the produced installer.
- Commit and push when ready.