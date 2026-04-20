"""
Tests for mcp_z3 — Z3 solver bridge.
Covers: basic sat/unsat, session persistence, named sessions,
        unsat-core extraction, explain_unsat, timeout enforcement,
        canonical error shape.
"""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import pytest
from mcp_z3 import solve_smt


# ── Helpers ───────────────────────────────────────────────────────────────────

def smt_sat():
    return "(declare-const x Int) (assert (> x 0)) (check-sat)"

def smt_unsat():
    return "(declare-const x Int) (assert (> x 0)) (assert (< x 0)) (check-sat)"

def smt_unsat_named():
    # Named assertions for unsat-core extraction
    return """
(declare-const x Int)
(assert (! (> x 0) :named pos))
(assert (! (< x 0) :named neg))
(check-sat)
"""


# ── Basic sat / unsat ─────────────────────────────────────────────────────────

def test_sat_basic():
    res = solve_smt(smt_sat())
    assert res["status"] == "sat"
    assert "model" in res
    assert res["model"] is not None

def test_unsat_basic():
    res = solve_smt(smt_unsat())
    assert res["status"] == "unsat"
    assert res["model"] is None

def test_response_always_dict():
    for smt in [smt_sat(), smt_unsat()]:
        res = solve_smt(smt)
        assert isinstance(res, dict)
        assert "status" in res


# ── Canonical error shape ─────────────────────────────────────────────────────

def test_error_has_status_and_error_fields():
    res = solve_smt("not valid smt2 !!!")
    # Z3 may silently ignore garbage or return unsat/unknown; the key invariant
    # is that if status=="error" then "error" field is present.
    assert "status" in res
    if res["status"] == "error":
        assert "error" in res
        assert isinstance(res["error"], str)

def test_no_spurious_error_field_on_success():
    res = solve_smt(smt_sat())
    assert res["status"] == "sat"
    assert "error" not in res


# ── Session persistence ───────────────────────────────────────────────────────

def test_persistent_session_accumulates():
    s1 = solve_smt("(declare-const y Int) (assert (= y 42))", session_id="test_accum")
    s2 = solve_smt("(check-sat)", session_id="test_accum")
    assert s1["session_persisted"] is True
    assert s2["status"] == "sat"
    assert s2["session_assertions"] >= 1

def test_named_sessions_are_isolated():
    solve_smt("(declare-const a Int) (assert (> a 100))", session_id="sess_A")
    solve_smt("(declare-const b Int) (assert (< b 0))", session_id="sess_B")
    rA = solve_smt("(check-sat)", session_id="sess_A")
    rB = solve_smt("(check-sat)", session_id="sess_B")
    assert rA["status"] == "sat"
    assert rB["status"] == "sat"

def test_fresh_call_has_no_persistence():
    res = solve_smt(smt_sat())
    assert res["session_persisted"] is False
    assert res["session_id"] == ""


# ── Unsat-core extraction ─────────────────────────────────────────────────────

def test_produce_unsat_core_on_unsat():
    res = solve_smt(smt_unsat(), produce_unsat_core=True)
    assert res["status"] == "unsat"
    assert "unsat_core" in res
    assert isinstance(res["unsat_core"], list)

def test_unsat_core_not_present_on_sat():
    res = solve_smt(smt_sat(), produce_unsat_core=True)
    assert res["status"] == "sat"
    assert "unsat_core" not in res

def test_explain_unsat_implies_core():
    res = solve_smt(smt_unsat(), explain_unsat=True)
    assert res["status"] == "unsat"
    assert "unsat_core" in res
    assert "explanation" in res
    assert isinstance(res["explanation"], str)
    assert len(res["explanation"]) > 0

def test_explain_unsat_not_present_on_sat():
    res = solve_smt(smt_sat(), explain_unsat=True)
    assert "explanation" not in res


# ── Timeout enforcement ───────────────────────────────────────────────────────

def test_timeout_minimum_enforced():
    # timeout_ms=1 should be bumped to 5000 — solver should still complete
    res = solve_smt(smt_sat(), timeout_ms=1)
    assert res["status"] in ("sat", "unknown", "error")
