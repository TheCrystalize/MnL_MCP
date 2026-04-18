# CI — Continuous Integration

Purpose
- Describe CI matrix, build, test, and artifact steps used in CI (GitHub Actions example). Aim: test Python Cython modules, build C++ server on Windows/Linux, run integration smoke tests.

Recommended GitHub Actions workflow (example)

```yaml
name: CI
on: [push, pull_request]

jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest]
        python-version: [3.10]

    steps:
      - uses: actions/checkout@v4

      - name: Setup Python
        uses: actions/setup-python@v4
        with:
          python-version: ${{ matrix.python-version }}

      - name: Cache pip
        uses: actions/cache@v4
        with:
          path: ~/.cache/pip
          key: ${{ runner.os }}-pip-${{ hashFiles('**/requirements.txt') }}

      - name: Install Python deps
        run: |
          python -m pip install --upgrade pip setuptools wheel
          pip install z3-solver sympy cython pytest

      - name: Build Cython extensions
        run: |
          cd cython
          python setup.py build_ext --inplace
          cd ..

      - name: Configure & Build C++ (CMake)
        run: |
          mkdir -p build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release
          cmake --build . --config Release

      - name: Run Python tests
        run: |
          cd cython
          pytest -q

      - name: Run C++ tests
        run: |
          cd build
          ctest --output-on-failure

      - name: Upload build artifacts
        if: success()
        uses: actions/upload-artifact@v4
        with:
          name: mcp-artifacts-${{ matrix.os }}
          path: build/
```

CI best practices
- Matrix: at minimum Ubuntu and Windows to validate native builds.
- Cache pip and large build artifacts between runs.
- Separate quick unit tests from longer E2E/integration tests; run E2E in a scheduled or gated job.
- Fail fast on linting and unit test failures.
- Publish artifacts for debug (build logs, preserved temp output when debug flag enabled).

Security & credentials
- Store secrets (API keys for optional adapters) in GitHub secrets and avoid exposing them in logs.
- Limit public CI runners access to sensitive artifacts.

Additional CI steps (optional)
- Run static analysis: clang-tidy / cppcheck for C++, ruff/flake8 for Python.
- Run dependency scanning and SCA tools.
- Publish test coverage artifacts (codecov) and binary release artifacts for tagged builds.