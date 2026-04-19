# Examples

This file contains minimal examples to exercise the core workflows (SymPy → SMT solver → Lean).

1) Call z3 via Python (Cython) wrapper

```python
# python
from mcp_z3 import solve_smt   # built from cython/mcp_z3.pyx

smt2 = "(declare-const x Int) (assert (> x 0)) (check-sat)"
res = solve_smt(smt2, timeout_ms=2000)
# res is a JSON-serializable dict: { "status": "sat", "model": "...", "stats": {...} }
print(res)
```

2) SymPy exploration then SMT check (Python harness)

```python
# python
from mcp_sympy import explore   # exposes explore(expr, goals, timeout_ms)
from mcp_z3 import solve_smt

expr = "x**2 + 2*x + 1"
sym_res = explore(expr, ["factor", "solve"], timeout_ms=2000)
# Use the symbolic result to produce an SMT-LIB query (example)
smt2 = "(declare-const x Int) (assert (= x -1)) (check-sat)"
z3_res = solve_smt(smt2, timeout_ms=2000)
print(sym_res, z3_res)
```

3) JSON-RPC over stdio — example integration test harness (NDJSON framing)

```python
# python
import subprocess, json

def send_request(proc, req):
    # NDJSON: one JSON object per line, terminated by '\n'.
    line = json.dumps(req) + "\n"
    proc.stdin.write(line.encode())
    proc.stdin.flush()

def read_response(proc):
    # One response line per message.
    raw = proc.stdout.readline()
    return json.loads(raw.decode())

proc = subprocess.Popen(["./build/mcp_stdio_server"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
req = {
  "jsonrpc": "2.0",
  "id": 1,
  "method": "z3.solve",
  "params": {"smt2": "(declare-const x Int) (assert (> x 0)) (check-sat)", "timeout_ms": 2000}
}
send_request(proc, req)
resp = read_response(proc)
print(resp)
proc.kill()
```

Sample files (place in docs/SAMPLES/)
- sample1.smt2 — small SMT-LIB example (SAT/UNSAT cases).
- sample1.lean — minimal Lean 4 snippet used for verification examples.
- example_end_to_end.md — step-by-step reproduction of a short pipeline.

Notes
- The Python examples assume the Cython extensions were built in-place.
- The stdio harness is a simple test helper; production clients should implement robust framing and schema validation.