# Security & Sandbox Guide

Scope & threat model
- Inputs that may be untrusted: Lean source (`source`), SMT-LIB (`smt2`), SymPy expressions, and external search queries.
- Adversary goals: resource exhaustion, data exfiltration, arbitrary code execution via tool chains or interpreter misuse, filesystem tampering, privilege escalation.

Principles
- Fail closed: reject or sandbox anything that could execute arbitrary code.
- Least privilege: run backends with minimal permissions.
- Defense in depth: combine timeouts, resource limits, OS primitives, and optional container isolation.

Mitigations

1) Message & input validation
- Enforce JSON Schema validation and a maximum message size (default 4 MiB).
- Reject requests containing disallowed fields; never eval user strings in Python/Lean without parsing and sanitizing.

2) Timeouts & cancellation
- Per-job `timeout_ms` enforced by the server; kill processes that exceed time budget.
- Support explicit cancellation notifications (`mcp.cancel`).

3) Resource limits
- Linux: set rlimits (RLIMIT_AS, RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_NPROC) before spawning subprocesses; consider cgroups v2 to cap memory/CPU/io.
- Windows: use Job Objects to limit memory, CPU and ensure child-process tree is terminated on cancellation.
- Apply per-job ephemeral working directories (temp dirs) and delete on completion.

4) Process isolation
- Prefer running untrusted or externally-sourced code in isolated subprocesses.
- For stronger isolation use Docker containers (or OCI runtimes) configured with:
  - No network or restricted network
  - Read-only rootfs where possible, writable tmpfs for job artifacts
  - Resource limits (memory, CPU, pids)
  - seccomp / syscall filters (Linux) for high-risk deployments

5) Python/embedding risks
- Avoid executing arbitrary Python code strings with eval/exec.
- Prefer calling vetted Cython wrapper functions that accept structured inputs and return structured outputs.
- For maximum safety, run Python-backed handlers in a separate, sandboxed process rather than embedding the interpreter in the main server process.

6) Lean runner precautions
- Run `lean` as an unprivileged user; do not run Lean with elevated privileges.
- Create per-job temp directories and remove them after job completion; optionally retain for debugging only when debug flag set.
- Enforce time and memory caps for Lean subprocesses.

7) Network & external APIs
- Loogle or other search adapters must be opt-in and rate-limited.
- Sanitize and cache remote results; never expose secret credentials to third-party search adapters.

8) Filesystem & artifacts
- Store artifacts in per-job dirs under a controlled base path.
- Use safe permissions (owner-only) and automatic TTL cleanup.
- Avoid executing artifacts produced from untrusted inputs.

9) Supply-chain & dependencies
- Pin Python and native dependency versions and audit them.
- Use reproducible builds for C++ and Cython artifacts where possible.
- Run CI security scans and dependency audits.

10) Logging, auditing & alerts
- Log request metadata (method, id, runtime_ms, exit_code) but avoid logging raw untrusted payloads unless explicitly required and access-controlled.
- Emit security alerts on repeated failures/timeouts or suspicious patterns.
- Retain minimal audit logs for incident investigation.

Operational guidance
- For public-facing deployments use containerized workers and strict network isolation.
- For local or trusted environments, embedding Python may be acceptable with conservative rlimits and monitoring.
- Regularly review and test sandbox boundaries with fuzzing and adversarial inputs.

Appendix: Quick hardening checklist
- [ ] Validate all requests with JSON Schema
- [ ] Enforce `timeout_ms` and global max
- [ ] Configure RLIMITs / Job Objects
- [ ] Disable network for untrusted jobs (or use Docker)
- [ ] Run Lean and subprocesses as unprivileged user
- [ ] Pin and audit dependencies
- [ ] Enable artifact TTL cleanup and audit logging