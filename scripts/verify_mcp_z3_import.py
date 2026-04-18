import sys
import os
import glob
import importlib
import traceback
import json

def main():
    print("CWD:", os.getcwd())
    print("sys.path:")
    for p in sys.path:
        print("  ", p)
    print("cython files:", glob.glob(os.path.join("cython", "*")))
    try:
        m = importlib.import_module("mcp_z3")
        print("imported mcp_z3 from", getattr(m, "__file__", None))
        res = m.solve_smt("(declare-const x Int) (assert (> x 0)) (check-sat)", timeout_ms=2000)
        print("result:", json.dumps(res))
    except Exception:
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()