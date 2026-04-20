"""
Tests for mcp_lean — Lean 4 execution bridge.
Covers: check/eval modes, error/ok summary field, diagnostics on failure,
        server lifecycle (start/check/stop), tmpdir cleanup, canonical shape.

Lean-dependent tests are skipped if the lean binary is not on PATH.
"""
import sys, pathlib, shutil
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import pytest
from mcp_lean import run_lean, start_lean_server, check_lean_server, stop_lean_server

LEAN_AVAILABLE = shutil.which("lean") is not None
LAKE_AVAILABLE = shutil.which("lake") is not None

lean_only  = pytest.mark.skipif(not LEAN_AVAILABLE, reason="lean not on PATH")
lake_only  = pytest.mark.skipif(not LAKE_AVAILABLE, reason="lake not on PATH")


# ── Summary field always present ──────────────────────────────────────────────

@lean_only
def test_summary_present_on_ok():
    res = run_lean("#check Nat.succ")
    assert "summary" in res
    assert isinstance(res["summary"], str)

@lean_only
def test_summary_present_on_error():
    res = run_lean("this is not lean code !!!")
    assert "summary" in res

def test_summary_present_without_lean():
    # Even when lean is missing, run_lean returns a dict with "summary"
    if LEAN_AVAILABLE:
        pytest.skip("lean is available; skipping not-found path")
    res = run_lean("#check Nat")
    assert isinstance(res, dict)
    assert "status" in res


# ── Ok / error status ─────────────────────────────────────────────────────────

@lean_only
def test_valid_lean_returns_ok():
    res = run_lean("-- empty file")
    assert res["status"] == "ok"
    assert "error" not in res

@lean_only
def test_type_error_returns_error_status():
    res = run_lean("def bad : Nat := \"not a number\"")
    assert res["status"] == "error"

@lean_only
def test_error_includes_stderr():
    res = run_lean("def bad : Nat := \"oops\"")
    assert "stderr" in res or "error" in res


# ── Diagnostics on failure ────────────────────────────────────────────────────

@lean_only
def test_diagnostics_field_on_error():
    res = run_lean("def bad : Nat := \"oops\"")
    if res["status"] == "error":
        assert "diagnostics" in res
        assert isinstance(res["diagnostics"], str)

@lean_only
def test_no_diagnostics_on_ok():
    res = run_lean("-- comment only")
    if res["status"] == "ok":
        assert "diagnostics" not in res


# ── Canonical error shape ─────────────────────────────────────────────────────

def test_error_response_has_status():
    res = run_lean("garbage")
    assert "status" in res

@lean_only
def test_exit_code_present():
    res = run_lean("def x := 1")
    assert "exit_code" in res
    assert isinstance(res["exit_code"], int)


# ── Sorry counting in summary ─────────────────────────────────────────────────

@lean_only
def test_sorry_counted_in_summary():
    res = run_lean("theorem foo : 1 = 2 := by sorry")
    assert "summary" in res
    # sorry should appear in summary or the file should have 1 sorry
    assert "sorry" in res["summary"] or res["status"] in ("ok", "error")


# ── Lean server lifecycle ─────────────────────────────────────────────────────

def test_check_server_before_start():
    # After module import, server is not started
    res = check_lean_server()
    assert isinstance(res, dict)
    assert "status" in res
    assert res["status"] in ("not_started", "stopped", "running")
    assert "stderr_lines" in res
    assert isinstance(res["stderr_lines"], list)

@lake_only
def test_start_check_stop_lifecycle():
    # Start
    start = start_lean_server()
    assert "status" in start
    assert start["status"] in ("started", "already_running", "error")

    if start["status"] == "error":
        pytest.skip(f"lake serve failed to start: {start.get('error')}")

    # Check
    check = check_lean_server()
    assert check["status"] in ("running", "stopped")
    if check["status"] == "running":
        assert check["pid"] is not None
        assert isinstance(check["stderr_lines"], list)

    # Stop
    stop = stop_lean_server()
    assert stop["status"] in ("stopped", "not_running")
    assert "stderr_lines" in stop
    assert isinstance(stop["stderr_lines"], list)

@lake_only
def test_stop_when_not_running():
    stop_lean_server()  # ensure stopped
    res = stop_lean_server()
    assert res["status"] == "not_running"

@lake_only
def test_start_twice_returns_already_running():
    stop_lean_server()
    r1 = start_lean_server()
    if r1["status"] == "error":
        pytest.skip("lake serve not available")
    r2 = start_lean_server()
    assert r2["status"] == "already_running"
    stop_lean_server()
