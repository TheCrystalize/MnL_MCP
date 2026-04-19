#include <Python.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

void init_python() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_InitThreads();

        // Add current working directory and build output directory to Python path
        PyRun_SimpleString(
            "import sys\n"
            "import os\n"
            "try:\n"
            "    import win32api\n"
            "    exe_path = win32api.GetModuleFileName(None)\n"
            "except ImportError:\n"
            "    exe_path = sys.executable\n"
            "exe_dir = os.path.dirname(os.path.abspath(exe_path))\n"
            "root_dir = os.path.abspath(os.path.join(exe_dir, '..', '..', '..'))\n"
            "sys.path.insert(0, root_dir)\n"
            "sys.path.insert(0, exe_dir)\n"
            "sys.path.insert(0, os.path.join(root_dir, 'cython'))\n"
            "sys.stdout = sys.stderr\n"
        );

        // Release GIL after initialization
        PyThreadState* ts = PyEval_SaveThread();
        (void)ts;
    }
}

void finalize_python() {
    // Skip explicit Py_Finalize on process exit: with a long-lived embedded
    // interpreter and pybind11 modules holding Python objects, finalization
    // can segfault as destructors run against a torn-down interpreter. The OS
    // reclaims everything on exit anyway.
}

static json pyobj_to_json(PyObject* obj);

// ── loogle_search ─────────────────────────────────────────────────────────────
// New params: filter_name, filter_module, filter_type, signature

json call_python_loogle_search(const std::string& query,
                               int max_results = 5,
                               int timeout_ms = 0,
                               const std::string& filter_name = "",
                               const std::string& filter_module = "",
                               const std::string& filter_type = "",
                               const std::string& signature = "") {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_loogle");
    if (!module) {
        PyErr_Print();
        result["error"] = "failed to import mcp_loogle";
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "search");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["error"] = "search not callable";
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }
    // Positional args: (query, max_results, timeout_ms)
    PyObject* args = PyTuple_Pack(3,
        PyUnicode_FromString(query.c_str()),
        PyLong_FromLong(max_results),
        PyLong_FromLong(timeout_ms));
    // Keyword args: filter_name, filter_module, filter_type, signature
    PyObject* kwargs = PyDict_New();
    PyDict_SetItemString(kwargs, "filter_name",   PyUnicode_FromString(filter_name.c_str()));
    PyDict_SetItemString(kwargs, "filter_module",  PyUnicode_FromString(filter_module.c_str()));
    PyDict_SetItemString(kwargs, "filter_type",    PyUnicode_FromString(filter_type.c_str()));
    PyDict_SetItemString(kwargs, "signature",      PyUnicode_FromString(signature.c_str()));

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_XDECREF(args);
    Py_XDECREF(kwargs);
    if (!pyres) {
        PyErr_Print();
        result["error"] = "call failed";
    } else {
        result = pyobj_to_json(pyres);
        Py_XDECREF(pyres);
    }
    Py_XDECREF(func);
    Py_XDECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── z3.solve ──────────────────────────────────────────────────────────────────
// New params: produce_unsat_core, explain_unsat

json call_python_solve(const std::string& smt2,
                       int timeout_ms = 0,
                       const std::string& session_id = "",
                       bool produce_unsat_core = false,
                       bool explain_unsat = false) {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_z3");
    if (!module) {
        PyErr_Print();
        result["error"] = "failed to import mcp_z3";
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "solve_smt");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["error"] = "solve_smt not callable";
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* args = PyTuple_Pack(3,
        PyUnicode_FromString(smt2.c_str()),
        PyLong_FromLong(timeout_ms),
        PyUnicode_FromString(session_id.c_str()));
    PyObject* kwargs = PyDict_New();
    PyDict_SetItemString(kwargs, "produce_unsat_core", PyBool_FromLong(produce_unsat_core ? 1 : 0));
    PyDict_SetItemString(kwargs, "explain_unsat",      PyBool_FromLong(explain_unsat      ? 1 : 0));

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_XDECREF(args);
    Py_XDECREF(kwargs);
    if (!pyres) {
        PyErr_Print();
        result["error"] = "call failed";
    } else {
        result = pyobj_to_json(pyres);
        Py_XDECREF(pyres);
    }
    Py_XDECREF(func);
    Py_XDECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── sympy.explore ─────────────────────────────────────────────────────────────
// New param: assumptions (list of strings)

json call_python_explore(const std::string& expr,
                         const std::vector<std::string>& goals,
                         int timeout_ms = 0,
                         const std::string& output_syntax = "python",
                         const std::vector<std::string>& assumptions = {}) {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_sympy");
    if (!module) {
        PyErr_Print();
        result["error"] = "failed to import mcp_sympy";
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "explore");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["error"] = "explore not callable";
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* pyGoals = PyList_New((Py_ssize_t)goals.size());
    for (Py_ssize_t i = 0; i < (Py_ssize_t)goals.size(); ++i) {
        PyObject* s = PyUnicode_FromString(goals[(size_t)i].c_str());
        PyList_SET_ITEM(pyGoals, i, s);
    }
    PyObject* args = PyTuple_Pack(4,
        PyUnicode_FromString(expr.c_str()),
        pyGoals,
        PyLong_FromLong(timeout_ms),
        PyUnicode_FromString(output_syntax.c_str()));

    // assumptions keyword arg
    PyObject* pyAssumptions = PyList_New((Py_ssize_t)assumptions.size());
    for (Py_ssize_t i = 0; i < (Py_ssize_t)assumptions.size(); ++i) {
        PyObject* s = PyUnicode_FromString(assumptions[(size_t)i].c_str());
        PyList_SET_ITEM(pyAssumptions, i, s);
    }
    PyObject* kwargs = PyDict_New();
    PyDict_SetItemString(kwargs, "assumptions", pyAssumptions);

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_XDECREF(args);
    Py_XDECREF(kwargs);
    Py_XDECREF(pyGoals);
    Py_XDECREF(pyAssumptions);
    if (!pyres) {
        PyErr_Print();
        result["error"] = "call failed";
    } else {
        result = pyobj_to_json(pyres);
        Py_XDECREF(pyres);
    }
    Py_XDECREF(func);
    Py_XDECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── lean.exec ─────────────────────────────────────────────────────────────────
// New sub-actions: start_server, check_server, stop_server (via mode param)

json call_python_run_lean(const std::string& source,
                          const std::string& mode,
                          int timeout_ms = 0,
                          const std::string& project_dir = "") {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_lean");
    if (!module) {
        PyErr_Print();
        result["error"] = "failed to import mcp_lean";
        PyGILState_Release(gstate);
        return result;
    }

    // Lifecycle actions bypass run_lean and call the lifecycle functions directly
    const char* lifecycle_fn = nullptr;
    if (mode == "start_server")  lifecycle_fn = "start_lean_server";
    if (mode == "check_server")  lifecycle_fn = "check_lean_server";
    if (mode == "stop_server")   lifecycle_fn = "stop_lean_server";

    if (lifecycle_fn) {
        PyObject* func = PyObject_GetAttrString(module, lifecycle_fn);
        if (!func || !PyCallable_Check(func)) {
            PyErr_Print();
            result["error"] = std::string(lifecycle_fn) + " not callable";
            Py_XDECREF(module);
            PyGILState_Release(gstate);
            return result;
        }
        PyObject* pyres = nullptr;
        if (mode == "start_server") {
            // start_lean_server(project_dir, timeout_ms)
            PyObject* args = PyTuple_Pack(2,
                PyUnicode_FromString(project_dir.c_str()),
                PyLong_FromLong(timeout_ms));
            pyres = PyObject_CallObject(func, args);
            Py_XDECREF(args);
        } else {
            // check/stop take no arguments
            pyres = PyObject_CallObject(func, PyTuple_New(0));
        }
        if (!pyres) { PyErr_Print(); result["error"] = "call failed"; }
        else { result = pyobj_to_json(pyres); Py_XDECREF(pyres); }
        Py_XDECREF(func);
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }

    // Normal exec path
    PyObject* func = PyObject_GetAttrString(module, "run_lean");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["error"] = "run_lean not callable";
        Py_XDECREF(module);
        PyGILState_Release(gstate);
        return result;
    }
    PyObject* args = Py_BuildValue("(ssi)", source.c_str(), mode.c_str(), timeout_ms);
    PyObject* pyres = PyObject_CallObject(func, args);
    Py_XDECREF(args);
    if (!pyres) {
        PyErr_Print();
        result["error"] = "call failed";
    } else {
        result = pyobj_to_json(pyres);
        Py_XDECREF(pyres);
    }
    Py_XDECREF(func);
    Py_XDECREF(module);
    PyGILState_Release(gstate);
    return result;
}


static json pyobj_to_json(PyObject* obj) {
    if (!obj) return nullptr;
    if (obj == Py_None) return nullptr;
    if (PyBool_Check(obj)) return PyObject_IsTrue(obj) ? true : false;
    if (PyLong_Check(obj)) {
        long long v = PyLong_AsLongLong(obj);
        return v;
    }
    if (PyFloat_Check(obj)) {
        double d = PyFloat_AsDouble(obj);
        return d;
    }
    if (PyUnicode_Check(obj)) {
        const char* s = PyUnicode_AsUTF8(obj);
        return s ? std::string(s) : std::string();
    }
    if (PyDict_Check(obj)) {
        json j = json::object();
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &value)) {
            std::string k;
            if (PyUnicode_Check(key)) {
                const char* ks = PyUnicode_AsUTF8(key);
                k = ks ? ks : "";
            } else {
                PyObject* ks = PyObject_Str(key);
                const char* kcs = ks ? PyUnicode_AsUTF8(ks) : nullptr;
                k = kcs ? kcs : "";
                Py_XDECREF(ks);
            }
            j[k] = pyobj_to_json(value);
        }
        return j;
    }
    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        json arr = json::array();
        Py_ssize_t len = PySequence_Size(obj);
        for (Py_ssize_t i = 0; i < len; ++i) {
            PyObject* item = PySequence_GetItem(obj, i);
            arr.push_back(pyobj_to_json(item));
            Py_XDECREF(item);
        }
        return arr;
    }
    PyObject* s = PyObject_Str(obj);
    if (s) {
        const char* cs = PyUnicode_AsUTF8(s);
        std::string res = cs ? cs : "";
        Py_XDECREF(s);
        return res;
    }
    return nullptr;
}
