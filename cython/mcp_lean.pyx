# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False

import subprocess
import tempfile
import shutil
import os
import re
import threading
from typing import Dict

_MIN_TIMEOUT_MS: int = 5000

# ── Persistent Lean server lifecycle ─────────────────────────────────────────
# start_lean_server / check_lean_server / stop_lean_server allow the caller to
# manage a long-lived `lake serve` or `lean --server` process and retrieve its
# accumulated stderr for diagnostics.

_server_proc = None          # subprocess.Popen or None
_server_stderr_lines = []    # accumulated stderr from the server process
_server_lock = threading.Lock()


def start_lean_server(str project_dir="", int timeout_ms=0):
    """
    Start a persistent Lean server process (lake serve).

    project_dir: path to the Lean project root (cwd for the server).
                 Defaults to CWD when empty.
    timeout_ms:  not used for startup; reserved for future handshake wait.

    Returns {"status": "started"|"already_running"|"error", "pid": <int>}.
    """
    global _server_proc, _server_stderr_lines
    with _server_lock:
        if _server_proc is not None and _server_proc.poll() is None:
            return {"status": "already_running", "pid": _server_proc.pid}

        cwd = project_dir if project_dir else None
        _server_stderr_lines = []
        try:
            proc = subprocess.Popen(
                ["lake", "serve"],
                cwd=cwd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            _server_proc = proc

            # Drain stderr in a background thread so it never blocks.
            def _drain():
                for line in proc.stderr:
                    with _server_lock:
                        _server_stderr_lines.append(line.rstrip())
            t = threading.Thread(target=_drain, daemon=True)
            t.start()

            return {"status": "started", "pid": proc.pid}
        except FileNotFoundError:
            return {"status": "error", "error": "lake binary not found on PATH"}
        except Exception as e:
            return {"status": "error", "error": str(e)}


def check_lean_server():
    """
    Check whether the persistent Lean server is running.

    Returns {
      "status": "running"|"stopped"|"not_started",
      "pid": <int> | None,
      "exit_code": <int> | None,
      "stderr_lines": ["..."],   ← recent server stderr for diagnostics
    }
    """
    global _server_proc, _server_stderr_lines
    with _server_lock:
        if _server_proc is None:
            return {"status": "not_started", "pid": None, "exit_code": None,
                    "stderr_lines": []}
        rc = _server_proc.poll()
        lines = list(_server_stderr_lines)
        if rc is None:
            return {"status": "running", "pid": _server_proc.pid,
                    "exit_code": None, "stderr_lines": lines}
        return {"status": "stopped", "pid": _server_proc.pid,
                "exit_code": rc, "stderr_lines": lines}


def stop_lean_server():
    """
    Terminate the persistent Lean server.

    Returns {"status": "stopped"|"not_running", "stderr_lines": [...]}.
    """
    global _server_proc, _server_stderr_lines
    with _server_lock:
        if _server_proc is None or _server_proc.poll() is not None:
            lines = list(_server_stderr_lines)
            _server_proc = None
            return {"status": "not_running", "stderr_lines": lines}
        _server_proc.terminate()
        try:
            _server_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _server_proc.kill()
        lines = list(_server_stderr_lines)
        rc = _server_proc.returncode
        _server_proc = None
        return {"status": "stopped", "exit_code": rc, "stderr_lines": lines}


# ── Helpers ───────────────────────────────────────────────────────────────────

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


# ── Main exec entry point ─────────────────────────────────────────────────────

def run_lean(str source, str mode="check", int timeout_ms=0):
    """
    Run Lean 4 on the provided source.

    mode: "check" (default) or "eval".
    timeout_ms: 0 = no timeout; minimum enforced at 5 000 ms when set.

    On failure the response includes a "diagnostics" field with any server
    stderr collected so far, useful for diagnosing missing Lake dependencies
    or server crashes.

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
            if res.get("status") == "error":
                res.setdefault("diagnostics", _collect_server_stderr())
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
            result = {
                "status":   status,
                "stdout":   proc.stdout,
                "stderr":   proc.stderr,
                "exit_code": proc.returncode,
                "summary":  _summarise_output(proc.stdout, proc.stderr),
            }
            if status == "error":
                result["diagnostics"] = _collect_server_stderr()
            return result
        except subprocess.TimeoutExpired as e:
            return {
                "status":     "timeout",
                "stdout":     getattr(e, "stdout", "") or "",
                "stderr":     getattr(e, "stderr", "") or "",
                "exit_code":  -1,
                "timeout_ms": eff_timeout,
                "summary":    "timed out",
                "diagnostics": _collect_server_stderr(),
            }
        except FileNotFoundError:
            return {
                "status":    "error",
                "error":     "lean binary not found on PATH",
                "exit_code": -1,
                "diagnostics": _collect_server_stderr(),
            }
        except Exception as e:
            return {
                "status":    "error",
                "error":     str(e),
                "exit_code": -1,
                "diagnostics": _collect_server_stderr(),
            }
    finally:
        try:
            shutil.rmtree(tmpdir)
        except Exception:
            pass


def _collect_server_stderr():
    """Return recent server stderr lines (last 50) as a single string."""
    with _server_lock:
        lines = _server_stderr_lines[-50:]
    return "\n".join(lines) if lines else ""
