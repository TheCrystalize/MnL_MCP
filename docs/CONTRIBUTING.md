# Contributing

Thank you for contributing. This document describes the process for filing issues, proposing changes, and submitting pull requests.

Reporting issues
- Use the issues tracker. Provide a minimal reproduction, environment details (OS, Python version, Lean version), and steps to reproduce.
- Tag issues with an appropriate label (bug, enhancement, question).

Development workflow
- Fork the repository and create feature branches named like `feat/<short-desc>` or `fix/<short-desc>`.
- Rebase or merge from main frequently to keep changes small and reviewable.

Pull requests
- Open a PR against the `main` branch with a clear title and description of the change.
- Include tests for new behavior and update docs/examples when appropriate.
- Link related issues (e.g., `Fixes #123`).

Code style & linters
- Python: follow PEP 8. Use ruff/flake8 and black for formatting.
- C++: follow project clang-format profile. Run clang-tidy for static checks.
- Run linters locally before opening a PR.

Pre-commit hooks
- The repository uses pre-commit to run linters and tests. Install hooks with:

```bash
# bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

Testing
- Add unit tests for all new features.
- Run Python tests:

```bash
# bash
cd cython
pytest -q
```

- Run C++ tests:

```bash
# bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --config Debug
ctest --output-on-failure
```

Commit messages
- Use clear, imperative commit messages. Prefer one change per commit.
- Include a short body for non-trivial changes.

Review & CI
- PRs are validated by CI (see docs/CI.md). Fix CI failures before merging.
- Maintain backward compatibility; breaking changes require a major-version bump and clear migration notes.

Security disclosures
- For security issues, email the maintainers privately (see `SECURITY.md`). Do not open a public issue for unverified vulnerabilities.

Contributing documentation
- Update docs/ when adding features or changing behavior. Keep API schema files in docs/API_SCHEMA/ authoritative.

Templates
- Use the templates in docs/TEMPLATES/ for issues and PRs.

Acknowledgements
- All contributors agree to the project's license by submitting changes.