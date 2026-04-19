import subprocess
import tempfile
import shutil
import os
import re
from typing import Dict

_MIN_TIMEOUT_MS = 5000  # floor for any non-zero timeout

def _effective_timeout(timeout_ms: int) -> int:
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return 0


def _summarise_output(stdout: str, stderr: str) -> str:
    """Return a one-line 'N errors, N warnings' summary from Lean output."""
    combined = stdout + stderr
    errors = len(re.findall(r'\berror\b', combined, re.IGNORECASE))
    warnings = len(re.findall(r'\bwarning\b', combined, re.IGNORECASE))
    sorries = len(re.findall(r'\bsorry\b', combined))
    parts = [f"{errors} error{'s' if errors != 1 else ''}",
             f"{warnings} warning{'s' if warnings != 1 else ''}"]
    if sorries:
        parts.append(f"{sorries} sorry")
    return ", ".join(parts)


def run_lean(source: str, mode: str = "check", timeout_ms: int = 0) -> Dict:
    """
    Run Lean 4 on the provided source.
    - Prefer the fast in-process/boundary call (lean_bindings.run_lean_persistent) if available.
    - Otherwise writes source to a temporary main.lean and invokes the lean CLI via subprocess.
    Returns a dict: {status, stdout, stderr, exit_code, runtime_ms (optional)}
    """
    timeout_ms = _effective_timeout(timeout_ms)

    # Fast path: try pybind11 binding (also add build path if present)
    try:
        import importlib
        import sys
        import pathlib
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        candidate = repo_root / "build" / "src" / "Release"
        if candidate.exists():
            if str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
        lean_binding = importlib.import_module("lean_bindings")
        try:
            res = lean_binding.run_lean_persistent(source, mode, timeout_ms)
            if isinstance(res, dict):
                res.setdefault("summary", _summarise_output(
                    res.get("stdout", ""), res.get("stderr", "")))
                return res
        except Exception:
            pass
    except Exception:
        pass

    tmpdir = tempfile.mkdtemp(prefix="mcp_lean_")
    path = os.path.join(tmpdir, "main.lean")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(source)
        cmd = ["lean", "--run", path] if mode == "eval" else ["lean", path]
        timeout = None if not timeout_ms else (timeout_ms / 1000.0)
        try:
            proc = subprocess.run(cmd, input=None, capture_output=True, text=True, timeout=timeout)
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
                "timeout_ms": timeout_ms,
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