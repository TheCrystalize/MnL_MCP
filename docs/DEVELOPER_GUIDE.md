# Developer Guide

Purpose
- Setup, build, debug and test the project on Windows and *nix for active development.

Prerequisites
- C++ toolchain + CMake (3.15+). Windows: Visual Studio / MSVC. Linux/macOS: gcc/clang.
- Python 3.8+ with pip.
- Lean 4 installed and on PATH (`lean --version`).
- Recommended dev tools: git, ninja (optional), VSCode (C++ & Python extensions).

Quick setup

```bash
# create and activate venv (bash / *nix)
python -m venv .venv
source .venv/bin/activate
pip install -U pip setuptools wheel
pip install z3-solver sympy cython
```

```bat
:: Windows (cmd)
python -m venv .venv
.venv\Scripts\activate
pip install -U pip setuptools wheel
pip install z3-solver sympy cython
```

Build cycle

```bash
# Build Cython extensions (edit .pyx -> rebuild)
cd cython
python setup.py build_ext --inplace
cd ..
```

```bash
# Build C++ server
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
```

Run & environment
- Run stdio server binary from build folder. Example (Windows):

```bat
.\mcp_stdio_server.exe
```

- Use `MCP_LOG_LEVEL=debug` (or equivalent) to enable verbose logs. On Windows use `set MCP_LOG_LEVEL=debug` before launching.

Embedding Python (CMake)
- Use CMake's modern FindPython:

```cmake
find_package(Python COMPONENTS Interpreter Development REQUIRED)
target_include_directories(server PRIVATE ${Python_INCLUDE_DIRS})
target_link_libraries(server PRIVATE ${Python_LIBRARIES})
```

- If linking fails, ensure the venv Python has development headers or install the system Python dev package.

Cython dev tips
- Edit cython/*.pyx and run `python setup.py build_ext --inplace`.
- Keep Cython APIs small and JSON-serializable (dicts/lists/strings) to simplify the C++ bridge.
- For interactive iteration, import built extension from a Python REPL inside the same venv.

Testing
- Python unit tests (cython modules):

```bash
cd cython
pytest -q
```

- C++ tests (CTest):

```bash
cd build
ctest --output-on-failure
```

Debugging
- VSCode: add `.vscode/launch.json` to launch the C++ executable and use "Attach" to debug.
- To debug embedded Python calls, build server in Debug and set breakpoints in C++ around PyGILState hooks; use a Python REPL to exercise Cython functions separately.
- Preserve temp artifacts during debugging via environment flag `MCP_DEBUG_PRESERVE=1`.

Common issues
- Link errors for Python: ensure `FindPython` locates the same interpreter used for building Cython extensions.
- Lean not found: confirm `lean` is on PATH and `lean --version` succeeds.
- z3 import errors: ensure venv is active and `z3-solver` is installed.

Workflow summary
1. Activate venv, install deps.
2. Build Cython in-place while iterating on Python code.
3. Build C++ server (Debug).
4. Run tests (pytest, ctest).
5. Use VSCode or native debuggers to step through server and handler integration.