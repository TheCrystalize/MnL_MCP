# cython: language_level=3
# cython: boundscheck=False
# cython: wraparound=False
"""
Loogle search — queries https://loogle.lean-lang.org/json to find real
Mathlib4 lemma/theorem names before writing Lean proof terms.
"""

import urllib.request
import urllib.parse
import json as _json

_LOOGLE_API = "https://loogle.lean-lang.org/json"
_MIN_TIMEOUT_MS: int = 5000
_DEFAULT_TIMEOUT_MS: int = 10000
_MAX_RESULTS: int = 20


cpdef int _effective_timeout(int timeout_ms):
    if timeout_ms > 0:
        return max(timeout_ms, _MIN_TIMEOUT_MS)
    return _DEFAULT_TIMEOUT_MS


def search(str query, int max_results=5, int timeout_ms=0):
    """
    Search Mathlib4 via Loogle for lemmas/theorems matching query.

    query: Loogle search string — supports name fragments, type signatures,
           wildcards, and #find-style patterns (e.g. "_ * _ ≤ _ * _").
    max_results: cap on returned hits (1-20).
    timeout_ms: HTTP timeout; defaults to 10 000 ms, minimum 5 000 ms.

    Returns a dict:
      {
        "status": "ok" | "error",
        "count": <total matches found by Loogle>,
        "hits": [{"name": ..., "type": ..., "module": ..., "doc": ...}, ...],
        "header": "<human-readable summary from Loogle>"
      }
    """
    cdef int eff_timeout = _effective_timeout(timeout_ms)
    cdef int cap = min(max(max_results, 1), _MAX_RESULTS)
    cdef str url, raw
    cdef dict data, hit
    cdef list hits

    url = _LOOGLE_API + "?q=" + urllib.parse.quote(query)
    try:
        with urllib.request.urlopen(url, timeout=eff_timeout / 1000.0) as resp:
            raw = resp.read().decode("utf-8")
    except Exception as e:
        return {"status": "error", "error": str(e), "query": query}

    try:
        data = _json.loads(raw)
    except Exception as e:
        return {"status": "error", "error": f"JSON parse failed: {e}", "raw": raw[:500]}

    hits = data.get("hits") or []
    trimmed = []
    for hit in hits[:cap]:
        trimmed.append({
            "name":   hit.get("name", ""),
            "type":   hit.get("type", ""),
            "module": hit.get("module", ""),
            "doc":    hit.get("doc") or "",
        })

    return {
        "status":  "ok",
        "count":   data.get("count", len(hits)),
        "header":  data.get("header", ""),
        "hits":    trimmed,
        "query":   query,
    }
