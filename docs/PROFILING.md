# Local profiling instructions

This document shows how to record native flamegraphs and alternate HTML profiles on Windows so you can reproduce the profiling I ran and capture native / C-level stacks (recommended: py-spy).

Notes
- Run commands from the repository root: c:\dev\MnL_MCP
- Replace `Python314` with your installed minor version if different.
- You can record a single pytest benchmark or run the profiling script.

1) Install py-spy (user site)
```bash
python -m pip install --user py-spy
```

2) Locate Scripts path (if unsure)
```bash
python -m site --user-base
# append \Scripts to the printed path to get the scripts folder
```

3) Cmd.exe — add Scripts to PATH for this session and record a flamegraph
```bat
set PATH=%APPDATA%\Python\Python314\Scripts;%PATH%
py-spy record -o benchmarks\profiles\flame_sympy_simplify.svg -- python -m pytest tests/benchmarks::test_sympy_simplify -q
```

4) PowerShell — add Scripts to PATH for the session and record
```powershell
$env:PATH = "$env:APPDATA\Python\Python314\Scripts;" + $env:PATH
py-spy record -o .\benchmarks\profiles\flame_sympy_simplify.svg -- python -m pytest tests/benchmarks::test_sympy_simplify -q
```

5) Direct invocation without changing PATH (executable path)
```bat
"%APPDATA%\Python\Python314\Scripts\py-spy.exe" record -o "benchmarks\profiles\flame_sympy_simplify.svg" -- python -m pytest tests/benchmarks::test_sympy_simplify -q
```

6) Profile the profiling script (full workloads)
```bash
py-spy record -o benchmarks/profiles/flame_all.svg -- python scripts/run_profiling.py --iterations 10 --outdir benchmarks/profiles
```

Alternative: pyinstrument (HTML output)
1) Install:
```bash
python -m pip install --user pyinstrument
```
2) Run and save HTML:
```bash
pyinstrument -o benchmarks/profiles/pyinstrument_sympy_simplify.html -- python -m pytest tests/benchmarks::test_sympy_simplify -q
```

If py-spy/pyinstrument are not available system-wide, use the full path to the installed executable (see step 2).

If you want, run the commands above and upload these artifacts:
- benchmarks/profiles/flame_sympy_simplify.svg
- benchmarks/profiles/pyinstrument_sympy_simplify.html
- the .prof/.txt files already produced in benchmarks/profiles/

These will let me analyze native stacks and recommend concrete C/C++ targets (Lean embedding, SymPy polynomial internals, Z3 model serialization).