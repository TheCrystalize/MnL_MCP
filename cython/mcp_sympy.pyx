# Minimal Cython stub that uses SymPy.
# Build with: python cython/setup.py build_ext --inplace

def explore(expr: str, goals, timeout_ms: int = 0):
    # Fast path: try C++ binding
    try:
        import importlib, sys, pathlib
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        candidate = repo_root / "build" / "src" / "Release"
        if candidate.exists():
            if str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
        mod = importlib.import_module("sympy_bindings")
        try:
            return mod.explore_fast(expr, list(goals), timeout_ms)
        except Exception:
            pass
    except Exception:
        pass

    try:
        import sympy as sp
        parsed = sp.sympify(expr)
        results = {}
        for g in goals:
            if g == "simplify":
                results["simplify"] = str(sp.simplify(parsed))
            elif g == "factor":
                results["factor"] = str(sp.factor(parsed))
            elif g == "solve":
                results["solve"] = [str(s) for s in sp.solve(parsed)]
            elif g == "expand":
                results["expand"] = str(sp.expand(parsed))
            elif g == "differentiate":
                vars = list(parsed.free_symbols)
                if vars:
                    var = vars[0]
                    results["differentiate"] = str(sp.diff(parsed, var))
                else:
                    results["differentiate"] = None
            elif g == "integrate":
                vars = list(parsed.free_symbols)
                if vars:
                    var = vars[0]
                    results["integrate"] = str(sp.integrate(parsed, var))
                else:
                    results["integrate"] = None
            else:
                results[g] = None
        return {"status": "ok", "results": results}
    except Exception as e:
        return {"status": "error", "error": str(e)}
