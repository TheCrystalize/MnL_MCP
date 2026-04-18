#!/usr/bin/env python3
"""
Scaffold the initial C/C++ and Cython project layout for mcp-stdio-lean-z3.

Run (from repo root):
    python scripts/scaffold_project.py

This will create:
- src/ with minimal CMakeLists and C++ stubs
- cython/ with setup.py and .pyx stubs
- cython/tests/ with a basic pytest
- .gitignore

Files created are intentionally minimal scaffolds — implementers should expand and harden them.
"""
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parent.parent

FILES = {
    "src/CMakeLists.txt": r"""cmake_minimum_required(VERSION 3.15)
project(mcp_stdio_src LANGUAGES CXX)

add_executable(mcp_stdio_server
  mcp_stdio_server.cpp
  lean_runner.cpp
  pyembed.cpp
)

target_link_libraries(mcp_stdio_server PRIVATE nlohmann_json::nlohmann_json ${Python_LIBRARIES})
target_include_directories(mcp_stdio_server PRIVATE ${Python_INCLUDE_DIRS})
""",

    "src/mcp_stdio_server.cpp": r"""#include <iostream>
#include <string>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    while (true) {
        std::string line;
        int content_length = 0;
        // Read headers
        while (std::getline(std::cin, line)) {
            if (line == "\r" || line.empty())
                break;
            if (line.rfind("Content-Length:", 0) == 0) {
                std::string val = line.substr(strlen("Content-Length:"));
                try { content_length = std::stoi(val); } catch (...) { content_length = 0; }
            }
        }

        if (content_length <= 0) {
            if (!std::cin || std::cin.eof()) break;
            continue;
        }

        std::string body(content_length, '\\0');
        std::cin.read(&body[0], content_length);
        if (!std::cin) break;

        try {
            auto req = json::parse(body);
            json resp;
            resp["jsonrpc"] = "2.0";
            if (req.contains("id")) resp["id"] = req["id"];
            resp["error"] = { {"code", -32601}, {"message", "Method not implemented"} };
            auto resp_str = resp.dump();
            std::cout << "Content-Length: " << resp_str.size() << "\r\n\r\n" << resp_str;
            std::cout.flush();
        } catch (const std::exception& e) {
            json err;
            err["jsonrpc"] = "2.0";
            err["error"] = { {"code", -32700}, {"message", "Parse error"} };
            std::string err_s = err.dump();
            std::cout << "Content-Length: " << err_s.size() << "\r\n\r\n" << err_s;
            std::cout.flush();
        }
    }
    return 0;
}
""",

    "src/lean_runner.cpp": r"""#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Minimal stub for Lean runner. Replace with actual subprocess invocation.
json run_lean(const std::string& source, const std::string& mode, int timeout_ms) {
    json res;
    res["status"] = "not_implemented";
    res["stdout"] = "";
    res["stderr"] = "Lean runner stub";
    res["exit_code"] = -1;
    return res;
}
""",

    "src/pyembed.cpp": r"""#include <Python.h>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

void init_python() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_InitThreads();
        PyThreadState* ts = PyEval_SaveThread();
        (void)ts;
    }
}

void finalize_python() {
    if (Py_IsInitialized()) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        Py_FinalizeEx();
        (void)gstate;
    }
}

json call_python_solve(const std::string& smt2, int timeout_ms) {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_z3");
    if (!module) {
        PyErr_Print();
        result["status"] = "error";
        result["error"] = "failed to import mcp_z3";
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "solve_smt");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["status"] = "error";
        result["error"] = "solve_smt not callable";
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* args = Py_BuildValue("(si)", smt2.c_str(), timeout_ms);
    PyObject* res = PyObject_CallObject(func, args);
    Py_XDECREF(args);
    if (!res) {
        PyErr_Print();
        result["status"] = "error";
        result["error"] = "call failed";
    } else {
        PyObject* py_str = PyObject_Str(res);
        if (py_str) {
            const char* s = PyUnicode_AsUTF8(py_str);
            result["status"] = "ok";
            result["output"] = std::string(s ? s : "");
            Py_XDECREF(py_str);
        } else {
            result["status"] = "error";
            result["error"] = "failed to convert result to string";
        }
        Py_XDECREF(res);
    }
    Py_XDECREF(func);
    Py_XDECREF(module);
    PyGILState_Release(gstate);
    return result;
}
""",

    "cython/setup.py": r"""from setuptools import setup, Extension
from Cython.Build import cythonize

extensions = [
    Extension("mcp_z3", ["mcp_z3.pyx"]),
    Extension("mcp_sympy", ["mcp_sympy.pyx"]),
]

setup(
    name="mcp_cython",
    ext_modules=cythonize(extensions, language_level=3),
)
""",

    "cython/mcp_z3.pyx": r"""# Minimal Cython stub that uses the z3-solver Python package.
# Build with: python cython/setup.py build_ext --inplace

def solve_smt(smt2: str, timeout_ms: int = 0):
    try:
        import z3
        s = z3.Solver()
        try:
            parsed = z3.parse_smt2_string(smt2)
            for a in parsed:
                s.add(a)
        except Exception:
            # If parsing fails, attempt to continue
            pass
        if timeout_ms:
            s.set("timeout", timeout_ms)
        r = s.check()
        if r == z3.sat:
            m = s.model()
            return {"status": "sat", "model": str(m)}
        elif r == z3.unsat:
            return {"status": "unsat", "model": None}
        else:
            return {"status": "unknown", "model": None}
    except Exception as e:
        return {"status": "error", "error": str(e)}
""",

    "cython/mcp_sympy.pyx": r"""# Minimal Cython stub that uses SymPy.
# Build with: python cython/setup.py build_ext --inplace

def explore(expr: str, goals, timeout_ms: int = 0):
    try:
        import sympy as sp
        parsed = sp.sympify(expr)
        results = {}
        for g in goals:
            if g == "simplify":
                results["simplify"] = str(sp.simplify(parsed))
            elif g == "factor":
                results["factor"] = str(sp.factor(parsed))
            elif g == "solve":
                results["solve"] = [str(s) for s in sp.solve(parsed)]
            elif g == "expand":
                results["expand"] = str(sp.expand(parsed))
            elif g == "differentiate":
                vars = list(parsed.free_symbols)
                if vars:
                    var = vars[0]
                    results["differentiate"] = str(sp.diff(parsed, var))
                else:
                    results["differentiate"] = None
            elif g == "integrate":
                vars = list(parsed.free_symbols)
                if vars:
                    var = vars[0]
                    results["integrate"] = str(sp.integrate(parsed, var))
                else:
                    results["integrate"] = None
            else:
                results[g] = None
        return {"status": "ok", "results": results}
    except Exception as e:
        return {"status": "error", "error": str(e)}
""",

    "cython/tests/test_mcp_z3.py": r"""def test_solve_smt_basic():
    from mcp_z3 import solve_smt
    res = solve_smt("(declare-const x Int) (assert (> x 0)) (check-sat)", timeout_ms=1000)
    assert isinstance(res, dict)
    assert "status" in res
""",

    ".gitignore": r"""# Python
.venv/
__pycache__/
*.pyc
build/
dist/
*.egg-info/

# CMake
build/
CMakeFiles/
CMakeCache.txt
*.exe
*.dll
*.so
*.o
"""
}

def ensure_write(relpath, content):
    p = ROOT / relpath
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists():
        print(f"exists: {p}")
    else:
        p.write_text(content, encoding="utf-8")
        print(f"wrote: {p}")

def main():
    for rel, text in FILES.items():
        ensure_write(rel, text)
    print("\nScaffold complete. Next steps:")
    print(" - Create and activate a venv")
    print(" - pip install z3-solver sympy cython")
    print(" - build Cython extensions: python cython/setup.py build_ext --inplace")
    print(" - build C++ server with cmake (see README/docs/DEVELOPER_GUIDE.md)")

if __name__ == '__main__':
    main()