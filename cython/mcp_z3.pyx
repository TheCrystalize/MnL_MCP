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


# ── Unsat-core / proof helpers ────────────────────────────────────────────────

def _extract_unsat_core(object solver, str smt2):
    """
    Re-run the solver with unsat-core tracking enabled and return the core as a
    list of labelled assertion strings.

    Z3's Python API requires assertions to be named (via :named attributes or
    track_unsat_core=True) before check() to produce a core.  We re-parse the
    SMT2 with (set-option :produce-unsat-cores true) prepended, then ask for
    the core after an unsat result.
    """
    core_smt2 = "(set-option :produce-unsat-cores true)\n" + smt2
    try:
        ctx = z3.Context()
        core_solver = z3.Solver(ctx=ctx)
        core_solver.set("produce-unsat-cores", True)
        # parse_smt2_string can handle named assertions
        parsed = z3.parse_smt2_string(core_smt2, ctx=ctx)
        for i, a in enumerate(parsed):
            core_solver.assert_and_track(a, f"a{i}")
        r = core_solver.check()
        if r == z3.unsat:
            core = core_solver.unsat_core()
            return [str(c) for c in core]
    except Exception:
        pass
    return []


def _explain_unsat(object solver, str smt2):
    """
    Produce a human-readable explanation for why the constraints are
    unsatisfiable by walking the unsat core and describing each labelled
    assertion.  Falls back to the raw SMT2 assertions when no names are found.
    """
    core_labels = _extract_unsat_core(solver, smt2)
    if not core_labels:
        # Fall back: list all assertions as the explanation
        try:
            asserts = [str(a) for a in solver.assertions()]
            return "All assertions jointly unsatisfiable:\n" + "\n".join(
                f"  [{i}] {a}" for i, a in enumerate(asserts))
        except Exception:
            return "unsat (no further explanation available)"

    lines = ["Unsatisfiable core (minimal conflicting subset):"]
    for label in core_labels:
        lines.append(f"  {label}")
    return "\n".join(lines)


# ── Main entry point ──────────────────────────────────────────────────────────

def solve_smt(str smt2, int timeout_ms=0, str session_id="",
              bint produce_unsat_core=False, bint explain_unsat=False):
    """
    Solve an SMT-LIB2 problem.

    session_id: empty (default) = fresh isolated solver per call.
                "persistent" = legacy accumulating REPL.
                Any other non-empty string = named session shared across calls.

    produce_unsat_core: when True and result is unsat, include an
                        "unsat_core" field listing the conflicting named
                        assertions.  Assertions must carry :named attributes
                        in the SMT2 input, or core labels are generated
                        automatically (a0, a1, …).

    explain_unsat: when True and result is unsat, include an "explanation"
                   field with a human-readable description of the conflict,
                   derived from the unsat core.  Implies produce_unsat_core.

    Response fields:
      status          "sat" | "unsat" | "unknown" | "error"
      model           model string (sat only)
      unsat_core      list of core labels (unsat + produce_unsat_core)
      explanation     human-readable conflict summary (unsat + explain_unsat)
      session_*       session bookkeeping fields
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)
    cdef bint use_persistent = len(session_id) > 0
    cdef bint want_core = produce_unsat_core or explain_unsat

    # Fast path: stateless C++ paths (only when no session persistence or core needed)
    if not use_persistent and not want_core:
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
            if want_core:
                core = _extract_unsat_core(solver, smt2)
                result["unsat_core"] = core
            if explain_unsat:
                result["explanation"] = _explain_unsat(solver, smt2)
        else:
            result = {"status": "unknown", "model": None}

        result["session_assertions"] = len(solver.assertions())
        result["session_id"] = session_id
        result["session_persisted"] = use_persistent
        return result
    except Exception as e:
        return {"status": "error", "error": str(e)}
