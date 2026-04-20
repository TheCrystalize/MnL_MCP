# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False
"""
Loogle search — queries https://loogle.lean-lang.org/json to find real
Mathlib4 lemma/theorem names before writing Lean proof terms.
"""

import re as _re
import urllib.request
import urllib.parse
import json as _json
from concurrent.futures import ThreadPoolExecutor, as_completed

_LOOGLE_API = "https://loogle.lean-lang.org/json"
_MIN_TIMEOUT_MS: int = 5000
_DEFAULT_TIMEOUT_MS: int = 10000
_MAX_RESULTS: int = 20
# Timeout applied to each individual fuzzy-suggestion fetch (seconds)
_SUGGEST_TIMEOUT_S: float = 8.0


cpdef int _effective_timeout(int timeout_ms):
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return _DEFAULT_TIMEOUT_MS


def _fetch(str q, float timeout_s):
    """Single blocking Loogle HTTP call. Returns (q, data_dict | None)."""
    url = _LOOGLE_API + "?q=" + urllib.parse.quote(q)
    try:
        with urllib.request.urlopen(url, timeout=timeout_s) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
        return (q, _json.loads(raw))
    except _json.JSONDecodeError:
        return (q, None)
    except Exception:
        return (q, None)


def _candidate_queries(str query):
    """
    Generate fuzzy variant queries for a zero-hit Loogle result.

    Strategies:
      1. Bare name — strip leading namespace (e.g. "Nat.foo" → "foo")
      2. Each dot-separated segment individually
      3. Namespace suffix windows (A.B.C → B.C, C)
      4. CamelCase ↔ snake_case swap on the last segment
    """
    candidates = []
    seen = set()

    def add(q):
        q = q.strip()
        if q and q != query and q not in seen:
            seen.add(q)
            candidates.append(q)

    parts = query.split(".")
    if len(parts) > 1:
        add(parts[-1])
    for p in parts:
        add(p)
    for i in range(1, len(parts)):
        add(".".join(parts[i:]))

    last = parts[-1]
    if "_" in last:
        frags = [f for f in last.split("_") if f]
        for f in frags:
            add(f)
        add("".join(frags))
    else:
        snake = _re.sub(r"(?<=[a-z])(?=[A-Z])", "_", last).lower()
        if snake != last:
            add(snake)
            for f in snake.split("_"):
                add(f)

    return candidates


def _format_hits(list hits, int cap):
    trimmed = []
    for hit in hits[:cap]:
        trimmed.append({
            "name":   hit.get("name", ""),
            "type":   hit.get("type", ""),
            "module": hit.get("module", ""),
            "doc":    hit.get("doc") or "",
        })
    return trimmed


def _apply_filters(list hits, str filter_name, str filter_module, str filter_type):
    """Post-filter hit list; all non-empty filters must match (AND, case-insensitive)."""
    out = []
    for h in hits:
        if filter_name   and filter_name.lower()   not in h.get("name",   "").lower(): continue
        if filter_module and filter_module.lower()  not in h.get("module", "").lower(): continue
        if filter_type   and filter_type.lower()    not in h.get("type",   "").lower(): continue
        out.append(h)
    return out


def search(str query, int max_results=5, int timeout_ms=0,
           str filter_name="", str filter_module="", str filter_type="",
           str signature=""):
    """
    Search Mathlib4 via Loogle for lemmas/theorems matching query.

    query: name fragment, type signature "_ * _ ≤ _ * _", or #find pattern.
    max_results: 1-20 (default 5).
    timeout_ms: HTTP timeout; defaults to 10 000 ms, minimum 5 000 ms.
    filter_name:   keep hits whose name contains this substring.
    filter_module: keep hits from modules whose path contains this substring.
    filter_type:   keep hits whose type signature contains this substring.
    signature:     type-pattern clause appended as ": <sig>" to the query.

    Canonical response fields:
      status         "ok" | "error"
      error          present only when status == "error"
      count          total Loogle hits (before local filters)
      hits           [{name, type, module, doc}, ...]
      header         Loogle summary string
      query          actual query sent to Loogle
      filters_applied  present when any filter is active
      filtered_count   hit count after filtering
      suggestions    [{query, count, hits}] — present only when count == 0
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)
    cdef int cap = min(max(max_results, 1), _MAX_RESULTS)
    cdef float timeout_s = eff_timeout / 1000.0

    cdef str loogle_query = query
    if signature:
        loogle_query = (query + " : " + signature) if query else (": " + signature)

    _, data = _fetch(loogle_query, timeout_s)
    if data is None:
        return {"status": "error", "error": "Request failed or timed out",
                "query": loogle_query}

    try:
        hits = data.get("hits") or []
        count = data.get("count", len(hits))
    except Exception as e:
        return {"status": "error", "error": f"JSON parse failed: {e}",
                "query": loogle_query}

    cdef bint any_filter = bool(filter_name or filter_module or filter_type)
    if any_filter:
        hits = _apply_filters(hits, filter_name, filter_module, filter_type)

    result = {
        "status":  "ok",
        "count":   count,
        "header":  data.get("header", ""),
        "hits":    _format_hits(hits, cap),
        "query":   loogle_query,
    }

    if any_filter:
        result["filters_applied"] = {
            "name": filter_name, "module": filter_module, "type": filter_type,
        }
        result["filtered_count"] = len(hits)

    if count == 0:
        candidates = _candidate_queries(query)
        suggestions = []
        if candidates:
            # Cap per-future timeout to avoid stalling on a single slow query
            suggest_timeout = min(timeout_s, _SUGGEST_TIMEOUT_S)
            with ThreadPoolExecutor(max_workers=min(len(candidates), 6)) as pool:
                futures = {pool.submit(_fetch, c, suggest_timeout): c
                           for c in candidates}
                # as_completed timeout: don't wait longer than the outer timeout
                try:
                    for fut in as_completed(futures, timeout=timeout_s):
                        q, cdata = fut.result()
                        if cdata is None:
                            continue
                        chits = cdata.get("hits") or []
                        ccount = cdata.get("count", len(chits))
                        if ccount > 0:
                            suggestions.append({
                                "query": q,
                                "count": ccount,
                                "hits":  _format_hits(chits, min(cap, 3)),
                            })
                except Exception:
                    pass  # timeout on the futures pool — return whatever we have
            suggestions.sort(key=lambda s: s["count"], reverse=True)
        result["suggestions"] = suggestions

    return result
