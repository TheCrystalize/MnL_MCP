#include <Python.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

// Helper: set a string value in a kwargs dict without leaking the value ref.
static inline void dict_set_str(PyObject* d, const char* key, const char* val) {
    PyObject* v = PyUnicode_FromString(val);
    PyDict_SetItemString(d, key, v);
    Py_DECREF(v);
}

// Helper: set a bool value in a kwargs dict without leaking the value ref.
static inline void dict_set_bool(PyObject* d, const char* key, bool val) {
    PyObject* v = PyBool_FromLong(val ? 1 : 0);
    PyDict_SetItemString(d, key, v);
    Py_DECREF(v);
}

// Helper: set a PyObject* value in a kwargs dict without leaking the value ref.
static inline void dict_set_obj(PyObject* d, const char* key, PyObject* val) {
    PyDict_SetItemString(d, key, val);
    Py_DECREF(val);
}

void init_python() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_InitThreads();

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

        PyThreadState* ts = PyEval_SaveThread();
        (void)ts;
    }
}

void finalize_python() {
    // Skip Py_Finalize: pybind11 destructors can segfault against a torn-down
    // interpreter; the OS reclaims everything on exit anyway.
}

static json pyobj_to_json(PyObject* obj);

// ── loogle_search ─────────────────────────────────────────────────────────────

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
        result["status"] = "error"; result["error"] = "failed to import mcp_loogle";
        PyGILState_Release(gstate); return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "search");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["status"] = "error"; result["error"] = "search not callable";
        Py_XDECREF(module); PyGILState_Release(gstate); return result;
    }
    PyObject* args = PyTuple_Pack(3,
        PyUnicode_FromString(query.c_str()),
        PyLong_FromLong(max_results),
        PyLong_FromLong(timeout_ms));
    PyObject* kwargs = PyDict_New();
    dict_set_str(kwargs, "filter_name",   filter_name.c_str());
    dict_set_str(kwargs, "filter_module",  filter_module.c_str());
    dict_set_str(kwargs, "filter_type",    filter_type.c_str());
    dict_set_str(kwargs, "signature",      signature.c_str());

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_DECREF(args); Py_DECREF(kwargs);
    if (!pyres) { PyErr_Print(); result["status"] = "error"; result["error"] = "call failed"; }
    else { result = pyobj_to_json(pyres); Py_DECREF(pyres); }
    Py_DECREF(func); Py_DECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── z3.solve ──────────────────────────────────────────────────────────────────

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
        result["status"] = "error"; result["error"] = "failed to import mcp_z3";
        PyGILState_Release(gstate); return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "solve_smt");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["status"] = "error"; result["error"] = "solve_smt not callable";
        Py_XDECREF(module); PyGILState_Release(gstate); return result;
    }
    PyObject* args = PyTuple_Pack(3,
        PyUnicode_FromString(smt2.c_str()),
        PyLong_FromLong(timeout_ms),
        PyUnicode_FromString(session_id.c_str()));
    PyObject* kwargs = PyDict_New();
    dict_set_bool(kwargs, "produce_unsat_core", produce_unsat_core);
    dict_set_bool(kwargs, "explain_unsat",      explain_unsat);

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_DECREF(args); Py_DECREF(kwargs);
    if (!pyres) { PyErr_Print(); result["status"] = "error"; result["error"] = "call failed"; }
    else { result = pyobj_to_json(pyres); Py_DECREF(pyres); }
    Py_DECREF(func); Py_DECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── sympy.explore ─────────────────────────────────────────────────────────────

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
        result["status"] = "error"; result["error"] = "failed to import mcp_sympy";
        PyGILState_Release(gstate); return result;
    }
    PyObject* func = PyObject_GetAttrString(module, "explore");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["status"] = "error"; result["error"] = "explore not callable";
        Py_XDECREF(module); PyGILState_Release(gstate); return result;
    }

    PyObject* pyGoals = PyList_New((Py_ssize_t)goals.size());
    for (Py_ssize_t i = 0; i < (Py_ssize_t)goals.size(); ++i)
        PyList_SET_ITEM(pyGoals, i, PyUnicode_FromString(goals[(size_t)i].c_str()));

    PyObject* pyAssumptions = PyList_New((Py_ssize_t)assumptions.size());
    for (Py_ssize_t i = 0; i < (Py_ssize_t)assumptions.size(); ++i)
        PyList_SET_ITEM(pyAssumptions, i, PyUnicode_FromString(assumptions[(size_t)i].c_str()));

    PyObject* args = PyTuple_Pack(4,
        PyUnicode_FromString(expr.c_str()),
        pyGoals,
        PyLong_FromLong(timeout_ms),
        PyUnicode_FromString(output_syntax.c_str()));
    PyObject* kwargs = PyDict_New();
    // dict_set_obj steals our reference to pyAssumptions
    dict_set_obj(kwargs, "assumptions", pyAssumptions);

    PyObject* pyres = PyObject_Call(func, args, kwargs);
    Py_DECREF(args); Py_DECREF(kwargs); Py_DECREF(pyGoals);
    if (!pyres) { PyErr_Print(); result["status"] = "error"; result["error"] = "call failed"; }
    else { result = pyobj_to_json(pyres); Py_DECREF(pyres); }
    Py_DECREF(func); Py_DECREF(module);
    PyGILState_Release(gstate);
    return result;
}

// ── lean.exec ─────────────────────────────────────────────────────────────────

json call_python_run_lean(const std::string& source,
                          const std::string& mode,
                          int timeout_ms = 0,
                          const std::string& project_dir = "") {
    json result;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* module = PyImport_ImportModule("mcp_lean");
    if (!module) {
        PyErr_Print();
        result["status"] = "error"; result["error"] = "failed to import mcp_lean";
        PyGILState_Release(gstate); return result;
    }

    const char* lifecycle_fn = nullptr;
    if (mode == "start_server")  lifecycle_fn = "start_lean_server";
    if (mode == "check_server")  lifecycle_fn = "check_lean_server";
    if (mode == "stop_server")   lifecycle_fn = "stop_lean_server";

    if (lifecycle_fn) {
        PyObject* func = PyObject_GetAttrString(module, lifecycle_fn);
        if (!func || !PyCallable_Check(func)) {
            PyErr_Print();
            result["status"] = "error";
            result["error"] = std::string(lifecycle_fn) + " not callable";
            Py_DECREF(module); PyGILState_Release(gstate); return result;
        }
        PyObject* pyres = nullptr;
        if (mode == "start_server") {
            PyObject* args = PyTuple_Pack(2,
                PyUnicode_FromString(project_dir.c_str()),
                PyLong_FromLong(timeout_ms));
            pyres = PyObject_CallObject(func, args);
            Py_DECREF(args);
        } else {
            PyObject* empty = PyTuple_New(0);
            pyres = PyObject_CallObject(func, empty);
            Py_DECREF(empty);
        }
        if (!pyres) { PyErr_Print(); result["status"] = "error"; result["error"] = "call failed"; }
        else { result = pyobj_to_json(pyres); Py_DECREF(pyres); }
        Py_DECREF(func); Py_DECREF(module);
        PyGILState_Release(gstate); return result;
    }

    PyObject* func = PyObject_GetAttrString(module, "run_lean");
    if (!func || !PyCallable_Check(func)) {
        PyErr_Print();
        result["status"] = "error"; result["error"] = "run_lean not callable";
        Py_DECREF(module); PyGILState_Release(gstate); return result;
    }
    PyObject* args = Py_BuildValue("(ssi)", source.c_str(), mode.c_str(), timeout_ms);
    PyObject* pyres = PyObject_CallObject(func, args);
    Py_DECREF(args);
    if (!pyres) { PyErr_Print(); result["status"] = "error"; result["error"] = "call failed"; }
    else { result = pyobj_to_json(pyres); Py_DECREF(pyres); }
    Py_DECREF(func); Py_DECREF(module);
    PyGILState_Release(gstate);
    return result;
}


static json pyobj_to_json(PyObject* obj) {
    if (!obj || obj == Py_None) return nullptr;
    if (PyBool_Check(obj))   return PyObject_IsTrue(obj) ? true : false;
    if (PyLong_Check(obj))   return PyLong_AsLongLong(obj);
    if (PyFloat_Check(obj))  return PyFloat_AsDouble(obj);
    if (PyUnicode_Check(obj)) {
        const char* s = PyUnicode_AsUTF8(obj);
        return s ? std::string(s) : std::string();
    }
    if (PyDict_Check(obj)) {
        json j = json::object();
        PyObject *key, *value; Py_ssize_t pos = 0;
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
        Py_DECREF(s);
        return res;
    }
    return nullptr;
}
