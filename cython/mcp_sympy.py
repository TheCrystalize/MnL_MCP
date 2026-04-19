# SymPy bindings with persistent REPL session.
# Pure Python; the C++ server imports this module via PYTHONPATH.

import sympy as sp

# Persistent global SymPy session namespace - lives for entire server lifetime
_sympy_namespace = {}
_sympy_history = []

def sympy_reset_session():
    """Reset persistent SymPy REPL session to clean state"""
    global _sympy_namespace, _sympy_history
    _sympy_namespace.clear()
    _sympy_history.clear()
    # Prepopulate common sympy functions
    exec("from sympy import *", _sympy_namespace)
    return {"status": "ok", "session_reset": True}

def sympy_session_status():
    """Get current persistent session status"""
    global _sympy_namespace, _sympy_history
    if not _sympy_namespace:
        sympy_reset_session()
    return {
        "status": "ok",
        "defined_variables": len([k for k in _sympy_namespace.keys() if not k.startswith('_')]),
        "history_length": len(_sympy_history),
        "is_repl_active": True
    }

def sympy_exec(code: str, timeout_ms: int = 0):
    """Execute arbitrary SymPy/Python code in persistent session namespace"""
    try:
        global _sympy_namespace, _sympy_history
        if not _sympy_namespace:
            sympy_reset_session()

        result = eval(code, _sympy_namespace)
        _sympy_history.append(code)

        return {
            "status": "ok",
            "result": str(result),
            "session_variables": len([k for k in _sympy_namespace.keys() if not k.startswith('_')])
        }
    except Exception as e:
        return {"status": "error", "error": str(e)}

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
        global _sympy_namespace
        if not _sympy_namespace:
            sympy_reset_session()

        parsed = sp.sympify(expr, locals=_sympy_namespace)
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
