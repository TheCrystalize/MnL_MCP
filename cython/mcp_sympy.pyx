# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False

import re
import threading
import sympy as sp

# Persistent global SymPy session namespace - lives for entire server lifetime
_sympy_namespace = {}
_sympy_history = []
_sympy_lock = threading.Lock()

_MIN_TIMEOUT_MS: int = 5000


cpdef int _effective_timeout(int timeout_ms):
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return 0


def sympy_reset_session():
    global _sympy_namespace, _sympy_history
    with _sympy_lock:
        _sympy_namespace.clear()
        _sympy_history.clear()
        exec("from sympy import *", _sympy_namespace)
    return {"status": "ok", "session_reset": True}


def sympy_session_status():
    global _sympy_namespace, _sympy_history
    with _sympy_lock:
        if not _sympy_namespace:
            exec("from sympy import *", _sympy_namespace)
        nvars = len([k for k in _sympy_namespace if not k.startswith("_")])
        nhist = len(_sympy_history)
    return {
        "status": "ok",
        "defined_variables": nvars,
        "history_length": nhist,
        "is_repl_active": True,
    }


def sympy_exec(str code, int timeout_ms=0):
    global _sympy_namespace, _sympy_history
    with _sympy_lock:
        if not _sympy_namespace:
            exec("from sympy import *", _sympy_namespace)
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
    cdef dict merged = {}
    if not assumptions:
        return merged
    for a in assumptions:
        key = a.strip().lower()
        if key in _DOMAIN_ASSUMPTIONS:
            merged.update(_DOMAIN_ASSUMPTIONS[key])
    return merged


cpdef object _make_symbols(object expr_parsed, dict assumption_kwargs):
    if not assumption_kwargs:
        return expr_parsed
    subs = {}
    for sym in expr_parsed.free_symbols:
        typed = sp.Symbol(sym.name, **assumption_kwargs)
        subs[sym] = typed
    return expr_parsed.subs(subs)


# ── Output converters ─────────────────────────────────────────────────────────

cpdef str _sympy_to_lean(str expr_str):
    cdef str s = expr_str
    s = re.sub(r'\*\*', '^', s)
    s = re.sub(r'sqrt\(([^)]+)\)', r'Real.sqrt (\1)', s)
    s = re.sub(r'\blog\(([^)]+)\)', r'Real.log (\1)', s)
    return s


cpdef str _sympy_to_smtlib(str expr_str):
    try:
        e = sp.sympify(expr_str)
        result = sp.printing.smtlib.smtlib_code(e)
        if not result or result == expr_str:
            return expr_str
        return result
    except Exception:
        return expr_str


cpdef dict _reformat_results(dict results, str output_syntax):
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
    goals: list — simplify, factor, solve, expand, differentiate, integrate.
    output_syntax: "python" (default), "lean", or "smtlib".
    assumptions: list of domain strings applied to every free symbol, e.g.
                 ["real"], ["positive", "finite"].  When "real" is present,
                 solve() uses solveset(domain=Reals).
    timeout_ms: minimum 5 000 ms when > 0; 0 = no timeout.

    Canonical response fields:
      status               "ok" | "error"
      error                present only when status == "error"
      results              dict of goal → result string
      output_syntax        present when not "python"
      assumptions_applied  present when assumptions were active
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)

    # Fast path: C++ binding (no assumptions support → fall through when set)
    if not assumptions:
        try:
            import importlib, sys, pathlib
            repo_root = pathlib.Path(__file__).resolve().parents[1]
            candidate = repo_root / "build" / "src" / "Release"
            if candidate.exists() and str(candidate) not in sys.path:
                sys.path.insert(0, str(candidate))
            mod = importlib.import_module("sympy_bindings")
            res = mod.explore_fast(expr, list(goals), eff_timeout)
            if isinstance(res, dict) and "results" in res:
                if output_syntax != "python":
                    res["results"] = _reformat_results(res["results"], output_syntax)
                    res["output_syntax"] = output_syntax
                return res
        except Exception:
            pass

    cdef dict assumption_kwargs = _build_assumptions(assumptions)
    cdef dict results, resp
    cdef list syms

    with _sympy_lock:
        if not _sympy_namespace:
            exec("from sympy import *", _sympy_namespace)
        ns_snapshot = dict(_sympy_namespace)

    try:
        parsed_raw = sp.sympify(expr, locals=ns_snapshot)
        parsed = _make_symbols(parsed_raw, assumption_kwargs)

        results = {}
        for g in goals:
            if g == "simplify":
                results["simplify"] = str(sp.simplify(parsed))
            elif g == "factor":
                results["factor"] = str(sp.factor(parsed))
            elif g == "solve":
                domain = None
                if assumptions:
                    al = [a.strip().lower() for a in assumptions]
                    if "real" in al and "complex" not in al:
                        domain = sp.Reals
                if domain is not None:
                    sols = sp.solveset(parsed, domain=domain)
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
