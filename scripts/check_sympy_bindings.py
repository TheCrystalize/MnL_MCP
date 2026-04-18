import time, importlib, sys, pathlib
repo_root = pathlib.Path(__file__).resolve().parents[1]
candidate = repo_root / "build" / "src" / "Release"
print("candidate exists:", candidate.exists(), "path:", candidate)
if str(candidate) not in sys.path:
    sys.path.insert(0, str(candidate))
try:
    import sympy_bindings  # type: ignore[import-not-found]
    print("imported sympy_bindings:", sympy_bindings)
except Exception as e:
    print("failed import sympy_bindings:", repr(e))

# load mcp_sympy (fallback to executing cython/mcp_sympy.pyx)
sys.path.insert(0, str(repo_root / "cython"))
import mcp_sympy  # type: ignore[import-not-found]
print("mcp_sympy module:", mcp_sympy)

expr = " + ".join(f"({i}*x**{i} + {i}*y**{i})" for i in range(1, 40))
start = time.time()
res = mcp_sympy.explore(expr, ["simplify"])
elapsed = time.time() - start
print("result status:", res.get("status") if isinstance(res, dict) else type(res))
print("elapsed:", elapsed)