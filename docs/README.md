# mcp-stdio-lean-z3 — Documentation

Purpose
- MCP stdio server that composes Lean 4 and SMT solvers (z3) plus symbolic tooling (SymPy) to produce verified, machine-checked reasoning workflows.
- Core in C/C++ (stdio JSON-RPC), CPython embedded via Cython wrappers for z3/sympy, Lean 4 invoked as a local runner. Optional sandboxing via Docker.

What this docs/ folder contains (primary files)
- ARCHITECTURE.md — component diagrams and sequence flows.
- PROTOCOL.md — JSON-RPC over stdio framing, error model, size/time limits.
- API/lean.md, API/z3.md, API/sympy.md, API/loogle.md — per-tool method docs and example messages.
- DESIGN.md — embedding CPython, Cython layout, concurrency/job model.
- SECURITY.md — sandboxing, timeouts, resource limits.
- DEVELOPER_GUIDE.md — build & debug instructions (Windows/*nix).
- TESTS.md, CI.md — test strategy and CI matrix.
- EXAMPLES.md, SAMPLES/ — end-to-end examples and sample files.
- API_SCHEMA/ — JSON Schema used to generate TypeScript types and clients.
- ROADMAP.md, CONTRIBUTING.md, TEMPLATES/, diagrams/

Quickstart (Windows + *nix)
- Create and activate a venv, install Python deps, build Cython and C++:

```bash
# create venv (cross-platform)
python -m venv .venv
# Windows (cmd)
.venv\Scripts\activate
# *nix
# source .venv/bin/activate

pip install -U pip setuptools wheel
pip install z3-solver sympy cython
```

```bash
# build Cython Python extensions (from repo root)
cd cython
python setup.py build_ext --inplace
cd ..
```

```bash
# build C++ server (requires cmake)
mkdir -p build && cd build
cmake .. 
cmake --build . --config Release
# run the stdio server binary (platform-specific)
# Windows: .\mcp_stdio_server.exe
# *nix: ./mcp_stdio_server
```

Notes
- Lean 4 must be installed locally and available on PATH for the Lean runner to work.
- z3 is recommended via the pip package `z3-solver`.
- Wire format: JSON-RPC 2.0 with Content-Length framing (LSP-style). TypeScript clients are generated from JSON Schema (in API_SCHEMA/).

Contributing
- See CONTRIBUTING.md for code style, pre-commit, and PR process.
- Use the provided templates/ for issues and PRs.

Next steps
- Populate ARCHITECTURE.md, PROTOCOL.md, API/* stubs and API_SCHEMA/*.schema.json, plus one end-to-end sample in SAMPLES/.