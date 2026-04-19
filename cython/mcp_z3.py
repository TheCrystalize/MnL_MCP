# Z3 bindings with persistent REPL session.
# Pure Python; the C++ server imports this module via PYTHONPATH.

import z3

# Persistent global Z3 session - lives for entire server lifetime
_z3_solver_instance = None
_z3_goal_stack = []

def z3_reset_session():
    """Reset persistent Z3 solver session to clean state"""
    global _z3_solver_instance, _z3_goal_stack
    _z3_solver_instance = z3.Solver()
    _z3_goal_stack = []
    return {"status": "ok", "session_reset": True}

def z3_session_status():
    """Get current persistent session status"""
    global _z3_solver_instance
    if _z3_solver_instance is None:
        z3_reset_session()
    return {
        "status": "ok",
        "assertions_count": len(_z3_solver_instance.assertions()),
        "stack_depth": len(_z3_solver_instance),
        "is_repl_active": True
    }

def solve_smt(smt2: str, timeout_ms: int = 0, persist: bool = True):
    """
    Solve SMT2 expression.
    If persist=True (default), state is kept between calls in persistent REPL session
    """
    # Fast path: try Z3 C API binding (z3_capi), then z3_bindings
    try:
        import importlib, sys, pathlib
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        candidate = repo_root / "build" / "src" / "Release"
        if candidate.exists():
            if str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
        # Try z3_capi first (native Z3 C API wrapper)
        try:
            mod_capi = importlib.import_module("z3_capi")
            try:
                r = mod_capi.solve_smt_capi(smt2, timeout_ms)
                # z3_capi returns {"status":"fallback"} if not available
                if isinstance(r, dict) and r.get("status") != "fallback":
                    return r
            except Exception:
                pass
        except Exception:
            pass
        # Next try z3_bindings (pybind11 faster path)
        try:
            mod = importlib.import_module("z3_bindings")
            try:
                return mod.solve_smt_fast(smt2, timeout_ms)
            except Exception:
                pass
        except Exception:
            pass
    except Exception:
        pass

    try:
        global _z3_solver_instance
        if _z3_solver_instance is None:
            z3_reset_session()

        try:
            parsed = z3.parse_smt2_string(smt2)
            for a in parsed:
                _z3_solver_instance.add(a)
        except Exception:
            # If parsing fails, attempt to continue
            pass

        if timeout_ms:
            _z3_solver_instance.set("timeout", timeout_ms)

        r = _z3_solver_instance.check()

        result = {}
        if r == z3.sat:
            m = _z3_solver_instance.model()
            result = {"status": "sat", "model": str(m)}
        elif r == z3.unsat:
            result = {"status": "unsat", "model": None}
        else:
            result = {"status": "unknown", "model": None}

        result["session_assertions"] = len(_z3_solver_instance.assertions())
        result["session_persisted"] = persist

        return result
    except Exception as e:
        return {"status": "error", "error": str(e)}
