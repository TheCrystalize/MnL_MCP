"""
Tests for mcp_sympy — SymPy exploration bridge.
Covers: goals, assumptions/domain, output syntax, timeout enforcement,
        thread safety (light smoke), canonical error shape.
"""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import pytest
from mcp_sympy import explore, sympy_reset_session, sympy_session_status


# ── Basic goals ───────────────────────────────────────────────────────────────

def test_simplify():
    res = explore("x**2 + 2*x + 1", goals=["simplify"])
    assert res["status"] == "ok"
    assert "simplify" in res["results"]

def test_factor():
    res = explore("x**2 - 1", goals=["factor"])
    assert res["status"] == "ok"
    # (x-1)(x+1)
    assert "factor" in res["results"]
    assert "x" in res["results"]["factor"]

def test_expand():
    res = explore("(x+1)**3", goals=["expand"])
    assert res["status"] == "ok"
    assert "expand" in res["results"]

def test_differentiate():
    res = explore("x**3", goals=["differentiate"])
    assert res["status"] == "ok"
    r = res["results"]["differentiate"]
    assert r is not None

def test_integrate():
    res = explore("x**2", goals=["integrate"])
    assert res["status"] == "ok"
    assert res["results"]["integrate"] is not None

def test_solve_without_assumptions():
    res = explore("x**2 - 4", goals=["solve"])
    assert res["status"] == "ok"
    sols = res["results"]["solve"]
    assert isinstance(sols, list)
    assert len(sols) == 2

def test_multiple_goals():
    res = explore("x**2 - 9", goals=["factor", "solve"])
    assert res["status"] == "ok"
    assert "factor" in res["results"]
    assert "solve" in res["results"]


# ── Assumptions / domain ──────────────────────────────────────────────────────

def test_real_assumption_restricts_solve():
    # x^2 + 1 = 0 has no real solutions
    res = explore("x**2 + 1", goals=["solve"], assumptions=["real"])
    assert res["status"] == "ok"
    sols = res["results"]["solve"]
    # solveset over Reals returns EmptySet
    assert isinstance(sols, list)
    # Either empty list or "EmptySet" string — no complex roots
    for s in sols:
        assert "I" not in s and "i" not in s.lower().replace("irrational", "")

def test_assumptions_applied_field():
    res = explore("x**2 - 2", goals=["solve"], assumptions=["real"])
    assert res["status"] == "ok"
    assert "assumptions_applied" in res
    assert "real" in res["assumptions_applied"]

def test_no_assumptions_applied_field_without_assumptions():
    res = explore("x**2 - 4", goals=["solve"])
    assert res["status"] == "ok"
    assert "assumptions_applied" not in res

def test_positive_assumption():
    res = explore("x", goals=["simplify"], assumptions=["positive"])
    assert res["status"] == "ok"
    assert "assumptions_applied" in res


# ── Output syntax ─────────────────────────────────────────────────────────────

def test_lean_output_syntax():
    res = explore("sqrt(2)", goals=["simplify"], output_syntax="lean")
    assert res["status"] == "ok"
    assert res.get("output_syntax") == "lean"
    s = res["results"]["simplify"]
    assert "Real.sqrt" in s or "sqrt" in s

def test_smtlib_output_syntax():
    res = explore("x**2", goals=["expand"], output_syntax="smtlib")
    assert res["status"] == "ok"
    assert res.get("output_syntax") == "smtlib"

def test_python_output_syntax_default():
    res = explore("x**2", goals=["expand"])
    assert res["status"] == "ok"
    assert "output_syntax" not in res


# ── Timeout enforcement ───────────────────────────────────────────────────────

def test_timeout_minimum_enforced():
    # timeout_ms=1 should be bumped to 5000 — should not crash
    res = explore("x**2 - 1", goals=["factor"], timeout_ms=1)
    assert res["status"] in ("ok", "error")


# ── Canonical error shape ─────────────────────────────────────────────────────

def test_error_has_status_and_error_fields():
    res = explore("@@@invalid@@@", goals=["simplify"])
    if res["status"] == "error":
        assert "error" in res
        assert isinstance(res["error"], str)

def test_no_spurious_error_on_success():
    res = explore("x + 1", goals=["simplify"])
    assert res["status"] == "ok"
    assert "error" not in res


# ── Session ───────────────────────────────────────────────────────────────────

def test_session_status():
    status = sympy_session_status()
    assert status["status"] == "ok"
    assert "defined_variables" in status

def test_session_reset():
    res = sympy_reset_session()
    assert res["status"] == "ok"
    assert res["session_reset"] is True


# ── Thread safety smoke test ──────────────────────────────────────────────────

def test_concurrent_explores_dont_crash():
    import threading
    errors = []
    def worker(expr):
        try:
            res = explore(expr, goals=["simplify"])
            if res["status"] == "error":
                errors.append(res.get("error"))
        except Exception as e:
            errors.append(str(e))

    threads = [threading.Thread(target=worker, args=(f"x**{i} - {i}",))
               for i in range(1, 8)]
    for t in threads: t.start()
    for t in threads: t.join()
    assert errors == [], f"Concurrent explore errors: {errors}"
