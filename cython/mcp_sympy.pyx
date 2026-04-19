# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False

import re
import sympy as sp

# Persistent global SymPy session namespace - lives for entire server lifetime
_sympy_namespace = {}
_sympy_history = []


def sympy_reset_session():
    global _sympy_namespace, _sympy_history
    _sympy_namespace.clear()
    _sympy_history.clear()
    exec("from sympy import *", _sympy_namespace)
    return {"status": "ok", "session_reset": True}


def sympy_session_status():
    global _sympy_namespace, _sympy_history
    if not _sympy_namespace:
        sympy_reset_session()
    return {
        "status": "ok",
        "defined_variables": len([k for k in _sympy_namespace if not k.startswith("_")]),
        "history_length": len(_sympy_history),
        "is_repl_active": True,
    }


def sympy_exec(str code, int timeout_ms=0):
    global _sympy_namespace, _sympy_history
    if not _sympy_namespace:
        sympy_reset_session()
    try:
        result = eval(code, _sympy_namespace)
        _sympy_history.append(code)
        return {
            "status": "ok",
            "result": str(result),
            "session_variables": len([k for k in _sympy_namespace if not k.startswith("_")]),
        }
    except Exception as e:
        return {"status": "error", "error": str(e)}


# ── Assumption / domain helpers ───────────────────────────────────────────────

# Recognised domain strings → SymPy assumption kwargs
_DOMAIN_ASSUMPTIONS = {
    "real":        {"real": True},
    "positive":    {"positive": True},
    "negative":    {"negative": True},
    "nonnegative": {"nonnegative": True},
    "integer":     {"integer": True},
    "rational":    {"rational": True},
    "complex":     {"complex": True},
    "finite":      {"finite": True},
}


cpdef dict _build_assumptions(assumptions):
    """
    Convert a list of assumption strings (e.g. ["real", "positive"]) into a
    merged dict of SymPy Symbol keyword arguments.
    """
    cdef dict merged = {}
    if not assumptions:
        return merged
    for a in assumptions:
        key = a.strip().lower()
        if key in _DOMAIN_ASSUMPTIONS:
            merged.update(_DOMAIN_ASSUMPTIONS[key])
    return merged


cpdef object _make_symbols(object expr_parsed, dict assumption_kwargs):
    """
    Re-create every free symbol in expr_parsed with the given assumptions,
    then substitute them back so SymPy uses the typed versions during solving.
    """
    if not assumption_kwargs:
        return expr_parsed
    subs = {}
    for sym in expr_parsed.free_symbols:
        typed = sp.Symbol(sym.name, **assumption_kwargs)
        subs[sym] = typed
    return expr_parsed.subs(subs)


# ── Output converters ─────────────────────────────────────────────────────────

cpdef str _sympy_to_lean(str expr_str):
    """Best-effort conversion of a SymPy expression string to Lean 4 syntax."""
    cdef str s = expr_str
    s = re.sub(r'\*\*', '^', s)
    s = re.sub(r'sqrt\(([^)]+)\)', r'Real.sqrt (\1)', s)
    s = re.sub(r'\blog\(([^)]+)\)', r'Real.log (\1)', s)
    return s


cpdef str _sympy_to_smtlib(str expr_str):
    """Best-effort conversion of a SymPy expression string to SMT-LIB2 via SymPy printer."""
    try:
        e = sp.sympify(expr_str)
        return sp.printing.smtlib.smtlib_code(e)
    except Exception:
        pass
    return expr_str


cpdef dict _reformat_results(dict results, str output_syntax):
    """Reformat string values in results dict according to output_syntax."""
    if output_syntax not in ("lean", "smtlib"):
        return results
    cdef dict out = {}
    cdef str k
    for k, v in results.items():
        if isinstance(v, str):
            out[k] = _sympy_to_lean(v) if output_syntax == "lean" else _sympy_to_smtlib(v)
        elif isinstance(v, list):
            if output_syntax == "lean":
                out[k] = [_sympy_to_lean(x) if isinstance(x, str) else x for x in v]
            else:
                out[k] = [_sympy_to_smtlib(x) if isinstance(x, str) else x for x in v]
        else:
            out[k] = v
    return out


# ── Main entry point ──────────────────────────────────────────────────────────

def explore(str expr, goals, int timeout_ms=0, str output_syntax="python",
            assumptions=None):
    """
    Explore a mathematical expression.

    expr: SymPy-parseable expression string.
    goals: list of goal strings — simplify, factor, solve, expand,
           differentiate, integrate.
    output_syntax: "python" (default), "lean" (Lean 4), or "smtlib" (SMT-LIB2).
    assumptions: list of domain/assumption strings applied to every free symbol,
                 e.g. ["real"], ["positive"], ["real", "finite"].
                 Supported values: real, positive, negative, nonnegative,
                 integer, rational, complex, finite.
                 When provided, solve() restricts its search to that domain,
                 eliminating spurious complex solutions.

    Returns {"status": "ok", "results": {...}, "assumptions_applied": [...]}
    """
    # Fast path: try C++ binding (no assumptions support there; fall through)
    if not assumptions:
        try:
            import importlib, sys, pathlib
            repo_root = pathlib.Path(__file__).resolve().parents[1]
            candidate = repo_root / "build" / "src" / "Release"
            if candidate.exists() and str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
            mod = importlib.import_module("sympy_bindings")
            res = mod.explore_fast(expr, list(goals), timeout_ms)
            if isinstance(res, dict) and "results" in res and output_syntax != "python":
                res["results"] = _reformat_results(res["results"], output_syntax)
                res["output_syntax"] = output_syntax
            return res
        except Exception:
            pass

    global _sympy_namespace
    if not _sympy_namespace:
        sympy_reset_session()

    cdef dict assumption_kwargs = _build_assumptions(assumptions)
    cdef dict results, resp
    cdef list syms

    try:
        parsed_raw = sp.sympify(expr, locals=_sympy_namespace)
        # Apply assumptions to symbols so domain constraints propagate
        parsed = _make_symbols(parsed_raw, assumption_kwargs)

        results = {}
        for g in goals:
            if g == "simplify":
                results["simplify"] = str(sp.simplify(parsed))
            elif g == "factor":
                results["factor"] = str(sp.factor(parsed))
            elif g == "solve":
                # Pass domain= kwarg when a single clean domain is requested
                domain = None
                if assumptions:
                    al = [a.strip().lower() for a in assumptions]
                    if "real" in al and "complex" not in al:
                        domain = sp.Reals
                if domain is not None:
                    sols = sp.solveset(parsed, domain=domain)
                    # solveset returns a Set; convert to list of strings
                    try:
                        results["solve"] = [str(s) for s in sols]
                    except Exception:
                        results["solve"] = [str(sols)]
                else:
                    results["solve"] = [str(s) for s in sp.solve(parsed)]
            elif g == "expand":
                results["expand"] = str(sp.expand(parsed))
            elif g == "differentiate":
                syms = list(parsed.free_symbols)
                results["differentiate"] = str(sp.diff(parsed, syms[0])) if syms else None
            elif g == "integrate":
                syms = list(parsed.free_symbols)
                results["integrate"] = str(sp.integrate(parsed, syms[0])) if syms else None
            else:
                results[g] = None

        if output_syntax != "python":
            results = _reformat_results(results, output_syntax)

        resp = {"status": "ok", "results": results}
        if output_syntax != "python":
            resp["output_syntax"] = output_syntax
        if assumption_kwargs:
            resp["assumptions_applied"] = list(assumption_kwargs.keys())
        return resp
    except Exception as e:
        return {"status": "error", "error": str(e)}
