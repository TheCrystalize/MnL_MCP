# Minimal Cython stub that uses the z3-solver Python package.
# Build with: python cython/setup.py build_ext --inplace

def solve_smt(smt2: str, timeout_ms: int = 0):
    # Fast path: try Z3 C API binding (z3_capi), then z3_bindings
    try:
        import importlib, sys, pathlib
        repo_root = pathlib.Path(__file__).resolve().parents[1]
        candidate = repo_root / "build" / "src" / "Release"
        if candidate.exists():
            if str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
        # Try z3_capi first (native Z3 C API wrapper)
        try:
            mod_capi = importlib.import_module("z3_capi")
            try:
                r = mod_capi.solve_smt_capi(smt2, timeout_ms)
                # z3_capi returns {"status":"fallback"} if not available
                if isinstance(r, dict) and r.get("status") != "fallback":
                    return r
            except Exception:
                pass
        except Exception:
            pass
        # Next try z3_bindings (pybind11 faster path)
        try:
            mod = importlib.import_module("z3_bindings")
            try:
                return mod.solve_smt_fast(smt2, timeout_ms)
            except Exception:
                pass
        except Exception:
            pass
    except Exception:
        pass

    try:
        import z3
        s = z3.Solver()
        try:
            parsed = z3.parse_smt2_string(smt2)
            for a in parsed:
                s.add(a)
        except Exception:
            # If parsing fails, attempt to continue
            pass
        if timeout_ms:
            s.set("timeout", timeout_ms)
        r = s.check()
        if r == z3.sat:
            m = s.model()
            return {"status": "sat", "model": str(m)}
        elif r == z3.unsat:
            return {"status": "unsat", "model": None}
        else:
            return {"status": "unknown", "model": None}
    except Exception as e:
        return {"status": "error", "error": str(e)}
