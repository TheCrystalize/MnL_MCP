# Design

Purpose
- Detailed design notes: CPython embedding, Cython extension layout, Lean runner lifecycle, concurrency, job interface, and error mapping.

Embedding CPython (high level)
- Initialize once in the process: call `Py_Initialize()` and `PyEval_InitThreads()`, then release the GIL with `PyEval_SaveThread()` until needed.
- Acquire the GIL before calling Python APIs; prefer a small set of well-defined entry points exposed by Cython modules.
- Consider a dedicated Python worker thread or a request queue to serialize interpreter access and reduce GIL churn.

Example (embed + call a Cython module):

```cpp
// cpp: initialize once at startup
Py_Initialize();
PyEval_InitThreads();
PyThreadState* main_ts = PyEval_SaveThread(); // release GIL for other threads

// In a worker thread that needs to call Python:
PyGILState_STATE gstate = PyGILState_Ensure();
PyObject* module = PyImport_ImportModule("mcp_z3");
PyObject* func = PyObject_GetAttrString(module, "solve_smt");
PyObject* args = Py_BuildValue("(si)", smt2.c_str(), timeout_ms);
PyObject* res = PyObject_CallObject(func, args);
// convert `res` (dict) to JSON, handle errors
PyGILState_Release(gstate);
```

Cython module layout
- cython/
  - mcp_z3.pyx -> exposes `def solve_smt(smt2: str, timeout_ms: int) -> dict`
  - mcp_sympy.pyx -> exposes `def explore(expr: str, goals: list, timeout_ms: int) -> dict`
  - setup.py / pyproject.toml to build extensions in-place for development
- Each Cython function should accept plain Python types and return JSON-serializable dicts (or strings) to simplify conversion in C++.

Lean runner
- Job creates a per-request temp directory, writes the requested Lean source to a file, invokes the local `lean` binary with a subprocess, captures stdout/stderr, exit code, and runtime.
- Enforce timeouts and kill the process if exceeded; return structured diagnostics and Lean's output to the caller.
- Keep a `debug_preserve_temp` flag to retain artifacts for post-mortem when debugging.

Pseudocode: Lean run

```bash
# create temp file -> tmp/req-<id>/main.lean
# run lean and capture output
lean main.lean  # or `lean --run main.lean` depending on desired behavior
# capture stdout/stderr, exit code
```

Job / Handler interface
- Handler signature (C++):

```cpp
// input: validated request JSON
nlohmann::json handle_method(const nlohmann::json& request);
```

- Typical handler flow:
  1. Validate request against JSON Schema.
  2. Create job temp dir + instrumentation context.
  3. If backend is Python: enqueue call or acquire GIL and call Cython function.
  4. If backend is Lean/other: spawn subprocess with enforced timeout/resource caps.
  5. Collect results, map to response schema, delete temp artifacts (unless debug).
  6. Emit progress notifications as needed.

GIL / concurrency guidance
- Option A (simpler): Serialize Python calls via a single Python worker thread receiving jobs over a thread-safe queue; avoids complex GIL handling.
- Option B (higher throughput): Use per-call PyGILState_Ensure/Release; be careful if Python code uses blocking operations or creates new threads.
- Always ensure long-running tasks in Python release the GIL if they perform heavy CPU-bound C work.

Resource & lifecycle
- Per-job temp dir: std::filesystem::temp_directory_path()/mcp-<pid>-<jobid>.
- Artifacts: stdout, stderr, model.json (optional), lean.log.
- Clean up on success; optionally retain on failure or when debug flag set.

Error mapping (JSON-RPC)
- Map internal errors to JSON-RPC error codes:
  - -32001 Timeout — backend exceeded `timeout_ms`
  - -32002 Cancelled — cancellation requested
  - -32003 SandboxError — resource isolation failure
  - -32010 BackendError — solver/tool reported an error (include backend and raw output)
- Responses include: `runtime_ms`, `exit_code`, `stdout`, `stderr`, `stats` (optional).

Extensibility
- Handlers are pluggable: register `method -> handler` at startup.
- Add new backends by implementing a Cython module (for Python-backed tools) or a subprocess adapter that returns JSON-compatible results.

Build & packaging notes
- Use pyproject/setup.py for Cython extension builds; prefer building in-place for development.
- Use CMake for the C++ server; expose an option to locate the Python include/libs if embedding.