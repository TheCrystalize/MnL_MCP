#!/usr/bin/env python3
"""
Run cProfile on representative workloads (sympy, z3, lean) and save .prof + text summaries.

Usage:
    python scripts/run_profiling.py --iterations 20 --outdir benchmarks/profiles
"""
from __future__ import annotations
import importlib
import importlib.util
import sys
import pathlib
import argparse
import cProfile
import pstats
import io
import types
import shutil

def _load_module_with_fallback(module_basename: str, rel_path: str):
    try:
        return importlib.import_module(module_basename)
    except Exception:
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        path = repo_root / rel_path
        if not path.exists():
            print(f"source not found: {path}; skipping {module_basename}")
            return None
        name = f"prof_{module_basename}"
        spec = importlib.util.spec_from_file_location(name, str(path))
        # If spec is None (e.g. .pyx/.c source) or loader is missing, fallback to executing source.
        if spec is None or spec.loader is None:
            import types as _types
            module = _types.ModuleType(name)
            module.__file__ = str(path)
            sys.modules[name] = module
            with open(path, "r", encoding="utf-8") as f:
                src = f.read()
            exec(compile(src, str(path), "exec"), module.__dict__)
            return module
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
        return module

def _poly_expr(n: int) -> str:
    return " + ".join(f"({i}*x**{i} + {i}*y**{i})" for i in range(1, n))

def _make_smt(n: int) -> str:
    parts = []
    for i in range(n):
        parts.append(f"(declare-const x{i} Int)")
    for i in range(n):
        parts.append(f"(assert (> x{i} {i}))")
    return " ".join(parts)

def _run_profile(callable_fn, iterations: int, out_prof: pathlib.Path, summary_lines: int = 50):
    out_prof.parent.mkdir(parents=True, exist_ok=True)
    pr = cProfile.Profile()
    pr.enable()
    for _ in range(iterations):
        callable_fn()
    pr.disable()
    pr.dump_stats(str(out_prof))
    s = io.StringIO()
    ps = pstats.Stats(pr, stream=s).sort_stats("cumtime")
    ps.print_stats(summary_lines)
    with open(str(out_prof.with_suffix(".txt")), "w", encoding="utf-8") as f:
        f.write(s.getvalue())
    print(f"Wrote {out_prof} and {out_prof.with_suffix('.txt')}")

def profile_sympy(iterations: int, outdir: pathlib.Path):
    try:
        import sympy  # noqa: F401
    except Exception:
        print("sympy not installed; skipping sympy profiling")
        return
    mod = _load_module_with_fallback("mcp_sympy", "cython/mcp_sympy.pyx")
    if mod is None:
        return
    expr = _poly_expr(40)

    def make_call(goal):
        def call():
            mod.explore(expr, [goal])
        return call

    for goal in ("simplify", "factor", "solve"):
        fn = make_call(goal)
        out_prof = outdir / f"sympy_{goal}.prof"
        _run_profile(fn, iterations, out_prof)

def profile_z3(iterations: int, outdir: pathlib.Path):
    try:
        import z3  # noqa: F401
    except Exception:
        print("z3 not installed; skipping z3 profiling")
        return
    mod = _load_module_with_fallback("mcp_z3", "cython/mcp_z3.pyx")
    if mod is None:
        return
    smt = _make_smt(20)

    def call():
        mod.solve_smt(smt, timeout_ms=1000)

    out_prof = outdir / "z3_solve.prof"
    _run_profile(call, iterations, out_prof)

def profile_lean(iterations: int, outdir: pathlib.Path):
    mod = _load_module_with_fallback("mcp_lean", "cython/mcp_lean.py")
    if mod is None:
        return
    src = "def main():\n    return 0\n"

    def call():
        mod.run_lean(src, mode="check", timeout_ms=1000)

    out_prof = outdir / "lean_run.prof"
    _run_profile(call, iterations, out_prof)

def main():
    parser = argparse.ArgumentParser(description="Run cProfile on representative workloads")
    parser.add_argument("--iterations", type=int, default=20, help="Iterations per workload")
    parser.add_argument("--outdir", type=str, default="benchmarks/profiles", help="Output directory for .prof and summaries")
    args = parser.parse_args()

    outdir = pathlib.Path(args.outdir)
    # remove old outputs to avoid confusion
    if outdir.exists():
        try:
            shutil.rmtree(outdir)
        except Exception:
            pass

    profile_sympy(args.iterations, outdir)
    profile_z3(args.iterations, outdir)
    profile_lean(args.iterations, outdir)
    print("Profiling complete.")

if __name__ == "__main__":
    main()