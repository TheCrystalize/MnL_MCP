# Roadmap

Vision
- Provide a small, secure MCP stdio server that composes symbolic math, SMT solving, and Lean 4 verification into reproducible reasoning pipelines.

Milestones

MVP (0–4 weeks)
- JSON-RPC 2.0 stdio core (NDJSON framing, per MCP spec).
- C++ core (dispatch + workers) with nlohmann::json and structured logging.
- Cython Python modules for z3 (z3-solver) and sympy.
- Lean runner invoking local `lean` on temp files with timeout handling.
- Basic JSON Schemas, TypeScript type generation guide, and a fast E2E example.
- Unit tests (pytest for Python, Catch2/CTest or GoogleTest for C++), CI matrix (Windows/Linux).

Near term (4–8 weeks)
- Docker-based sandbox runner as optional stronger isolation.
- Progress notifications, cancellation semantics, and result metadata.
- TypeScript client generator and example Node adapter.
- Integration tests and sample pipelines (sympy → solver → lean.verify).
- Metrics export (Prometheus) and basic observability.

Medium term (8–16 weeks)
- VSCode extension / Language Client for interactive use.
- Result caching, reproducible artifact storage, and job queue for async jobs.
- Hardened sandboxing (seccomp, cgroups, Windows Job Objects).
- Performance tuning and concurrency enhancements; optional distributed workers.

Long term (>16 weeks)
- Loogle adapter integration and citation tooling.
- Automated proof-search and tactic suggestion pipelines driven by combined tooling.
- Multi-node deployment patterns, persistent proof artifact registry, provenance tracking.
- Formalization libraries and community-driven proof bundles.

Timeline & KPIs
- Week 0–2: Finish docs + JSON schemas and TS types.
- Week 2–6: Implement MVP (Cython wrappers, Lean runner, C++ stdio dispatcher) and CI.
- KPIs: CI green on Windows/Linux, E2E sample passes, test coverage targets, resource-safety tests pass.

Next steps (immediate)
- Complete API JSON Schemas in docs/API_SCHEMA/.
- Scaffold cython/ and src/ with minimal example modules and handlers.
- Implement a quick E2E smoke: sympy → z3 → lean.verify.