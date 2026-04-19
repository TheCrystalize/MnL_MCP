# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False

import subprocess
import tempfile
import shutil
import os
import re
from typing import Dict

_MIN_TIMEOUT_MS: int = 5000


cpdef int _effective_timeout(int timeout_ms):
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return 0


cpdef str _summarise_output(str stdout, str stderr):
    """Return a one-line 'N errors, N warnings[, N sorry]' summary from Lean output."""
    cdef str combined = stdout + stderr
    cdef Py_ssize_t errors = len(re.findall(r'\berror\b', combined, re.IGNORECASE))
    cdef Py_ssize_t warnings = len(re.findall(r'\bwarning\b', combined, re.IGNORECASE))
    cdef Py_ssize_t sorries = len(re.findall(r'\bsorry\b', combined))
    parts = [
        f"{errors} error{'s' if errors != 1 else ''}",
        f"{warnings} warning{'s' if warnings != 1 else ''}",
    ]
    if sorries:
        parts.append(f"{sorries} sorry")
    return ", ".join(parts)


def run_lean(str source, str mode="check", int timeout_ms=0):
    """
    Run Lean 4 on the provided source.
    Prefers the pybind11 lean_bindings fast path; falls back to subprocess.
    Response always includes a "summary" field: "N errors, N warnings[, N sorry]".
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)

    # Fast path: try pybind11 binding
    try:
        import importlib, sys, pathlib
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        candidate = repo_root / "build" / "src" / "Release"
        if candidate.exists() and str(candidate) not in sys.path:
            sys.path.insert(0, str(candidate))
        lean_binding = importlib.import_module("lean_bindings")
        res = lean_binding.run_lean_persistent(source, mode, eff_timeout)
        if isinstance(res, dict):
            res.setdefault("summary", _summarise_output(
                res.get("stdout", ""), res.get("stderr", "")))
            return res
    except Exception:
        pass

    cdef str path, status
    tmpdir = tempfile.mkdtemp(prefix="mcp_lean_")
    path = os.path.join(tmpdir, "main.lean")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(source)
        cmd = ["lean", "--run", path] if mode == "eval" else ["lean", path]
        timeout = None if not eff_timeout else (eff_timeout / 1000.0)
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
            status = "ok" if proc.returncode == 0 else "error"
            return {
                "status": status,
                "stdout": proc.stdout,
                "stderr": proc.stderr,
                "exit_code": proc.returncode,
                "summary": _summarise_output(proc.stdout, proc.stderr),
            }
        except subprocess.TimeoutExpired as e:
            return {
                "status": "timeout",
                "stdout": getattr(e, "stdout", "") or "",
                "stderr": getattr(e, "stderr", "") or "",
                "exit_code": -1,
                "timeout_ms": eff_timeout,
                "summary": "timed out",
            }
        except FileNotFoundError:
            return {"status": "error", "error": "lean binary not found on PATH", "exit_code": -1}
        except Exception as e:
            return {"status": "error", "error": str(e), "exit_code": -1}
    finally:
        try:
            shutil.rmtree(tmpdir)
        except Exception:
            pass
