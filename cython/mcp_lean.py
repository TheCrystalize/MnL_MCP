import subprocess
import tempfile
import shutil
import os
from typing import Dict

def run_lean(source: str, mode: str = "check", timeout_ms: int = 0) -> Dict:
    """
    Run Lean 4 on the provided source.
    - Prefer the fast in-process/boundary call (lean_bindings.run_lean_persistent) if available.
    - Otherwise writes source to a temporary main.lean and invokes the lean CLI via subprocess.
    Returns a dict: {status, stdout, stderr, exit_code, runtime_ms (optional)}
    """
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
                return res
        except Exception:
            # binding call failed; fall back to subprocess
            pass
    except Exception:
        # binding not available; fall back to subprocess
        pass

    tmpdir = tempfile.mkdtemp(prefix="mcp_lean_")
    path = os.path.join(tmpdir, "main.lean")
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(source)
        if mode == "eval":
            cmd = ["lean", "--run", path]
        else:
            cmd = ["lean", path]
        timeout = None if not timeout_ms else (timeout_ms / 1000.0)
        try:
            proc = subprocess.run(cmd, input=None, capture_output=True, text=True, timeout=timeout)
            status = "ok" if proc.returncode == 0 else "error"
            return {
                "status": status,
                "stdout": proc.stdout,
                "stderr": proc.stderr,
                "exit_code": proc.returncode
            }
        except subprocess.TimeoutExpired as e:
            return {
                "status": "timeout",
                "stdout": getattr(e, "stdout", ""),
                "stderr": getattr(e, "stderr", ""),
                "exit_code": -1,
                "timeout_ms": timeout_ms
            }
        except FileNotFoundError:
            return { "status": "error", "error": "lean binary not found on PATH", "exit_code": -1 }
        except Exception as e:
            return { "status": "error", "error": str(e), "exit_code": -1 }
    finally:
        try:
            shutil.rmtree(tmpdir)
        except Exception:
            pass