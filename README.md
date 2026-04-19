# MnL_MCP

Math n Logic (MnL) — MCP stdio server, native helpers, and Python bindings.

This repository contains:
- A C++ stdio server (`mcp_stdio_server`) that embeds Python and exposes three MCP tools over JSON-RPC 2.0 / stdio transport.
- Native pybind11 modules (`lean_bindings`, `z3_bindings`, `z3_capi`, `sympy_bindings`) — the hot-path fast routes.
- Cython extension modules (`cython/`) — compiled `.pyx` sources that replace plain Python in the fallback path.
- Packaging/installer tooling: a WinForms launcher and NSIS packaging scripts (`installer/`).

## Tools

| Tool | Description |
|------|-------------|
| `z3_solve` | Solve SMT-LIB2 constraints via Z3. Supports `session_id` for persistent REPL sessions; default is a fresh isolated solver per call. |
| `sympy_explore` | Explore a mathematical expression: simplify, factor, solve, expand, differentiate, integrate. Accepts `output_syntax` (`"python"` / `"lean"` / `"smtlib"`) to emit results ready to paste into the target tool. |
| `lean_exec` | Type-check or run Lean 4 source. Response always includes a `"summary"` field (`"N errors, N warnings[, N sorry]"`). |

All tools enforce a minimum 5 000 ms floor on any non-zero `timeout_ms` value.

Status: production-ready. Build and packaging scripts are included; installer staging is prepared by `installer/build_installer.ps1`.

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

Cython extensions
The `cython/` directory contains `.pyx` sources for `mcp_z3`, `mcp_sympy`, and `mcp_lean`. Build them in-place before running the server so the C++ host loads compiled `.pyd` modules instead of the plain-Python fallbacks:
```powershell
cd cython
python setup.py build_ext --inplace
cd ..
```
This requires Cython (`pip install cython`) and the same MSVC toolchain used for the C++ build.

pybind11 extensions and site-packages
- Built pybind11 extension modules live in `build/src/Release/` (MSVC multi-config) or `build-ninja/...` (Ninja single-config).
- For development, add both directories to PYTHONPATH:
```powershell
$env:PYTHONPATH = "$PWD\build\src\Release;$PWD\cython"
python scripts\verify_mcp_z3_import.py
python scripts\check_sympy_bindings.py
```
- To install into the active Python environment:
```powershell
python -c "import sysconfig; print(sysconfig.get_paths()['platlib'])"
Copy-Item .\build\src\Release\*.pyd -Destination "<site-packages-path>"
Copy-Item .\cython\*.pyd       -Destination "<site-packages-path>"
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