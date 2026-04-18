# Benchmarks for identifying hotspots suitable for C/C++ optimization.
# Uses pytest-benchmark. Skips tests if required external deps are missing.
import importlib
import importlib.util
import sys
import pathlib
import types

import pytest
pytest.importorskip("pytest_benchmark")

def _load_module_with_fallback(module_basename: str, rel_path: str):
    """
    Try to import module by name; if unavailable load the source file at rel_path
    relative to repository root.
    """
    try:
        return importlib.import_module(module_basename)
    except Exception:
        repo_root = pathlib.Path(__file__).resolve().parents[2]
        path = repo_root / rel_path
        if not path.exists():
            pytest.skip(f"module source not found: {path}")
        name = f"bench_{module_basename}"
        spec = importlib.util.spec_from_file_location(name, str(path))
        # If spec is None (e.g. .pyx/.c source) or loader is missing, fallback to executing source.
        if spec is None or spec.loader is None:
            module = types.ModuleType(name)
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

def test_sympy_simplify(benchmark):
    pytest.importorskip("sympy")
    mod = _load_module_with_fallback("mcp_sympy", "cython/mcp_sympy.pyx")
    expr = _poly_expr(40)

    def fn():
        res = mod.explore(expr, ["simplify"])
        assert isinstance(res, dict) and res.get("status") in ("ok", "error")

    benchmark(fn)

def test_sympy_factor(benchmark):
    pytest.importorskip("sympy")
    mod = _load_module_with_fallback("mcp_sympy", "cython/mcp_sympy.pyx")
    factors = " * ".join(f"(x - {i})" for i in range(1, 10))
    expr = f"{factors}"

    def fn():
        res = mod.explore(expr, ["factor"])
        assert isinstance(res, dict)

    benchmark(fn)

def test_sympy_solve(benchmark):
    pytest.importorskip("sympy")
    mod = _load_module_with_fallback("mcp_sympy", "cython/mcp_sympy.pyx")
    expr = "x**3 - 6*x**2 + 11*x - 6"

    def fn():
        res = mod.explore(expr, ["solve"])
        assert isinstance(res, dict)

    benchmark(fn)

def test_z3_solve_scaling(benchmark):
    pytest.importorskip("z3")
    mod = _load_module_with_fallback("mcp_z3", "cython/mcp_z3.pyx")

    def make_smt(n: int) -> str:
        parts = []
        for i in range(n):
            parts.append(f"(declare-const x{i} Int)")
        for i in range(n):
            parts.append(f"(assert (> x{i} {i}))")
        return " ".join(parts)

    smt = make_smt(20)

    def fn():
        res = mod.solve_smt(smt, timeout_ms=1000)
        assert isinstance(res, dict)

    benchmark(fn)

def test_mcp_lean_run_overhead(benchmark):
    mod = _load_module_with_fallback("mcp_lean", "cython/mcp_lean.py")
    src = "def main():\n    return 0\n"

    def fn():
        res = mod.run_lean(src, mode="check", timeout_ms=1000)
        assert isinstance(res, dict)

    benchmark(fn)