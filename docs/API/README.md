# API — JSON-RPC methods

All methods use JSON-RPC 2.0 over stdio (Content-Length framing). Schemas for each method live in docs/API_SCHEMA/*.schema.json and are the authoritative contract used to generate TypeScript types.

Summary of methods
- lean.exec — run Lean 4 source (modes: check, eval). Returns diagnostics, stdout/stderr, exit code.
- lean.verify — run a verification flow producing a Lean-checked artifact and proof status.
- z3.solve — run SMT-LIB input using z3 (via z3-solver). Returns sat/unsat/unknown, model, stats.
- sympy.explore — symbolic exploration (simplify, solve, expand, differentiate, etc.).
- loogle.search — optional web search adapter for retrieving references.
- mcp.cancel — cancellation notification for an outstanding request id.
- mcp.progress — progress notification for long-running work.

Common conventions
- All requests include `timeout_ms` where applicable; servers enforce global max time.
- Responses include diagnostic metadata: `runtime_ms`, `exit_code`, `stdout`, `stderr`, and optional `stats`.
- Handlers SHOULD emit `mcp.progress` notifications for long-running jobs.
- Validation: server validates request params against JSON Schema and returns -32602 on invalid params.

Example: lean.exec request

```json
{
  "jsonrpc": "2.0",
  "id": 101,
  "method": "lean.exec",
  "params": {
    "source": "def foo := 2 + 2\n#eval foo",
    "mode": "eval",
    "timeout_ms": 5000
  }
}
```

Example: z3.solve request

```json
{
  "jsonrpc": "2.0",
  "id": 102,
  "method": "z3.solve",
  "params": {
    "smt2": "(declare-const x Int) (assert (> x 0)) (check-sat)",
    "timeout_ms": 5000,
    "options": { "produce_model": true }
  }
}
```

Example: sympy.explore request

```json
{
  "jsonrpc": "2.0",
  "id": 103,
  "method": "sympy.explore",
  "params": {
    "expr": "x**2 + 2*x + 1",
    "goals": ["simplify", "factor", "solve"],
    "timeout_ms": 3000
  }
}
```

Progress notification example

```json
{
  "jsonrpc": "2.0",
  "method": "mcp.progress",
  "params": { "id": 102, "percent": 42, "message": "z3: 42% heuristics" }
}
```

Schema location and client generation
- Per-method JSON Schema files: docs/API_SCHEMA/lean.exec.schema.json, docs/API_SCHEMA/z3.solve.schema.json, etc.
- Generate TypeScript types using `json-schema-to-typescript` or `quicktype` and publish a small TS adapter that frames messages over stdio.

Notes
- Keep models and schemas minimal and stable; prefer adding optional fields over breaking changes.
- Provide example request/response pairs in docs/API/<method>.md for each method to illustrate edge cases and error responses.