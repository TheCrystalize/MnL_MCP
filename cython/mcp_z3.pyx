# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False

import z3

# Persistent global Z3 session - lives for entire server lifetime
_z3_solver_instance = None
_z3_goal_stack = []
_z3_named_sessions = {}

_MIN_TIMEOUT_MS: int = 5000


cpdef int _effective_timeout(int timeout_ms):
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return 0


def z3_reset_session():
    global _z3_solver_instance, _z3_goal_stack
    _z3_solver_instance = z3.Solver()
    _z3_goal_stack = []
    return {"status": "ok", "session_reset": True}


def z3_session_status():
    global _z3_solver_instance
    if _z3_solver_instance is None:
        z3_reset_session()
    return {
        "status": "ok",
        "assertions_count": len(_z3_solver_instance.assertions()),
        "stack_depth": len(_z3_solver_instance),
        "is_repl_active": True,
    }


def solve_smt(str smt2, int timeout_ms=0, str session_id=""):
    """
    Solve an SMT-LIB2 problem.

    session_id: empty (default) = fresh isolated solver per call.
    "persistent" = legacy accumulating REPL.
    Any other non-empty string = named session shared across calls.
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)
    cdef bint use_persistent = len(session_id) > 0

    # Fast path: stateless C++ paths (only when no session persistence needed)
    if not use_persistent:
        try:
            import importlib, sys, pathlib
            repo_root = pathlib.Path(__file__).resolve().parents[1]
            candidate = repo_root / "build" / "src" / "Release"
            if candidate.exists() and str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
            try:
                mod_capi = importlib.import_module("z3_capi")
                r = mod_capi.solve_smt_capi(smt2, eff_timeout)
                if isinstance(r, dict) and r.get("status") != "fallback":
                    r["session_id"] = ""
                    return r
            except Exception:
                pass
            try:
                mod = importlib.import_module("z3_bindings")
                r = mod.solve_smt_fast(smt2, eff_timeout)
                r["session_id"] = ""
                return r
            except Exception:
                pass
        except Exception:
            pass

    # Choose solver
    cdef object solver
    global _z3_solver_instance, _z3_named_sessions

    if use_persistent:
        if session_id == "persistent":
            if _z3_solver_instance is None:
                z3_reset_session()
            solver = _z3_solver_instance
        else:
            if session_id not in _z3_named_sessions:
                _z3_named_sessions[session_id] = z3.Solver()
            solver = _z3_named_sessions[session_id]
    else:
        solver = z3.Solver()

    cdef dict result
    try:
        try:
            parsed = z3.parse_smt2_string(smt2)
            for a in parsed:
                solver.add(a)
        except Exception:
            pass

        if eff_timeout:
            solver.set("timeout", eff_timeout)

        r = solver.check()

        if r == z3.sat:
            result = {"status": "sat", "model": str(solver.model())}
        elif r == z3.unsat:
            result = {"status": "unsat", "model": None}
        else:
            result = {"status": "unknown", "model": None}

        result["session_assertions"] = len(solver.assertions())
        result["session_id"] = session_id
        result["session_persisted"] = use_persistent
        return result
    except Exception as e:
        return {"status": "error", "error": str(e)}
