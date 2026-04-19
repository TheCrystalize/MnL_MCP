# Tests

Overview
- Test types: unit, integration, end-to-end (E2E), and optional fuzzing.
- Python modules (Cython wrappers) use pytest. C++ core uses Catch2/GoogleTest with CMake/CTest.
- Integration/E2E tests exercise the stdio JSON-RPC framing and sample workflows (sympy → z3 → lean).

Python unit tests
- Location: cython/tests/
- Run:

```bash
# from repo root, with venv active
cd cython
pytest -q
```

- Use `pytest -q --maxfail=1` for quick iteration.
- Use `pytest -q --cov=mcp_z3 --cov-report=term-missing` for coverage.

C++ tests
- Location: tests/ (Catch2 or GoogleTest)
- CMake config should add tests and enable CTest:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
ctest --output-on-failure
```

Integration tests
- Integration tests spawn the built stdio server binary and communicate over stdin/stdout using NDJSON framing (one JSON object per line).
- Example small Python harness:

```python
# tests/integration/test_z3_integration.py
import subprocess, json
proc = subprocess.Popen(["./build/mcp_stdio_server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
req = json.dumps({"jsonrpc":"2.0","id":1,"method":"z3.solve","params":{"smt2":"(declare-const x Int) (assert (> x 0)) (check-sat)","timeout_ms":2000}})
proc.stdin.write((req + "\n").encode()); proc.stdin.flush()
# one response per line
resp = json.loads(proc.stdout.readline().decode())
```

E2E tests
- Use samples/ to run a full pipeline: sympy → z3 → lean.verify and assert expected outcomes.
- Keep one canonical E2E scenario in tests/e2e/ that runs quickly (<30s).

Fuzzing & robustness
- Fuzz parsers and handlers (message framing, JSON Schema edge-cases).
- Consider OSS-Fuzz or AFL for native components; fuzz Python handlers with hypothesis.

Test data & samples
- Place reproducible sample inputs in docs/SAMPLES/ and reference them from tests.
- Include known SAT/UNSAT SMT-LIB snippets and minimal Lean proofs for verification tests.

CI integration
- CI runs:
  - Python unit tests (pytest)
  - C++ unit tests (ctest)
  - Integration smoke tests with the built binary
  - Linting: flake8/ruff for Python, clang-tidy for C++
  - Collect coverage artifacts and upload to codecov (optional)

Best practices
- Keep unit tests fast and deterministic.
- Mark long-running E2E scenarios and run them in a separate CI stage.
- Use assertions on solver statuses, model shapes, and Lean verification outputs rather than brittle textual matches.