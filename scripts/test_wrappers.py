import sys
import traceback

# ensure cython/ modules on import path
sys.path.insert(0, r"c:\dev\MnL_MCP\cython")

def try_run(name, func, *args):
    print(f"== {name} ==")
    try:
        res = func(*args)
        print(res)
    except Exception:
        traceback.print_exc()

# z3 (Cython extension)
try:
    import mcp_z3
    try_run("z3.solve", mcp_z3.solve_smt, "(declare-const x Int) (assert (> x 0)) (check-sat)", 0)
except Exception:
    print("failed to import mcp_z3")
    traceback.print_exc()

# sympy
try:
    import mcp_sympy
    try_run("sympy.explore", mcp_sympy.explore, "x+1", ["solve"], 0)
except Exception:
    print("failed to import mcp_sympy")
    traceback.print_exc()

# lean (python wrapper)
try:
    import mcp_lean
    lean_src = '''def main : IO Unit := IO.println "hi"'''
    try_run("lean.exec", mcp_lean.run_lean, lean_src, "check", 0)
except Exception:
    print("failed to import mcp_lean")
    traceback.print_exc()