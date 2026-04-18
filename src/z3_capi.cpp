#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace py = pybind11;

using char_ptr = const char *;

struct Z3Api {
#ifdef _WIN32
    HMODULE lib = NULL;
#else
    void *lib = nullptr;
#endif
    bool loaded = false;
    std::mutex mtx;

    // function pointers (use void* for opaque Z3 types)
    void* (*mk_config)() = nullptr;
    void  (*del_config)(void*) = nullptr;
    void* (*mk_context)(void*) = nullptr;
    void  (*del_context)(void*) = nullptr;
    void* (*mk_solver)(void*) = nullptr;
    void  (*solver_assert)(void*, void*, void*) = nullptr;
    int   (*solver_check)(void*, void*) = nullptr;
    void* (*solver_get_model)(void*, void*) = nullptr;
    char_ptr (*model_to_string)(void*, void*) = nullptr;
    void* (*parse_smtlib2_string)(void*, const char*, unsigned, void**, void**, unsigned, void**, void**) = nullptr;
    unsigned (*ast_vector_size)(void*, void*) = nullptr;
    void* (*ast_vector_get)(void*, void*, unsigned) = nullptr;

    bool load_library() {
        std::lock_guard<std::mutex> lk(mtx);
        if (loaded) return true;

#ifdef _WIN32
        const char *names[] = { "libz3.dll", "z3.dll" };
        for (auto n : names) {
            lib = LoadLibraryA(n);
            if (lib) break;
        }
        if (!lib) return false;

        auto get = [&](const char *name)->FARPROC { return GetProcAddress(lib, name); };
        mk_config = (void*(*)())get("Z3_mk_config");
        del_config = (void(*)(void*))get("Z3_del_config");
        mk_context = (void*(*)(void*))get("Z3_mk_context");
        del_context = (void(*)(void*))get("Z3_del_context");
        mk_solver = (void*(*)(void*))get("Z3_mk_solver");
        solver_assert = (void(*)(void*,void*,void*))get("Z3_solver_assert");
        solver_check = (int(*)(void*,void*))get("Z3_solver_check");
        solver_get_model = (void*(*)(void*,void*))get("Z3_solver_get_model");
        model_to_string = (char_ptr(*)(void*,void*))get("Z3_model_to_string");
        parse_smtlib2_string = (void*(*)(void*,const char*,unsigned,void**,void**,unsigned,void**,void**))get("Z3_parse_smtlib2_string");
        ast_vector_size = (unsigned(*)(void*,void*))get("Z3_ast_vector_size");
        ast_vector_get = (void*(*)(void*,void*,unsigned))get("Z3_ast_vector_get");
#else
        const char *names[] = { "libz3.so", "libz3.so.4", "libz3.so.4.8", "libz3.dylib" };
        for (auto n : names) {
            lib = dlopen(n, RTLD_NOW | RTLD_LOCAL);
            if (lib) break;
        }
        if (!lib) return false;

        auto get = [&](const char *name)->void* { return dlsym(lib, name); };
        mk_config = (void*(*)())get("Z3_mk_config");
        del_config = (void(*)(void*))get("Z3_del_config");
        mk_context = (void*(*)(void*))get("Z3_mk_context");
        del_context = (void(*)(void*))get("Z3_del_context");
        mk_solver = (void*(*)(void*))get("Z3_mk_solver");
        solver_assert = (void(*)(void*,void*,void*))get("Z3_solver_assert");
        solver_check = (int(*)(void*,void*))get("Z3_solver_check");
        solver_get_model = (void*(*)(void*,void*))get("Z3_solver_get_model");
        model_to_string = (char_ptr(*)(void*,void*))get("Z3_model_to_string");
        parse_smtlib2_string = (void*(*)(void*,const char*,unsigned,void**,void**,unsigned,void**,void**))get("Z3_parse_smtlib2_string");
        ast_vector_size = (unsigned(*)(void*,void*))get("Z3_ast_vector_size");
        ast_vector_get = (void*(*)(void*,void*,unsigned))get("Z3_ast_vector_get");
#endif

        // verify required symbols
        if (!mk_config || !mk_context || !del_context || !del_config || !mk_solver || !solver_check || !parse_smtlib2_string || !ast_vector_size || !ast_vector_get || !solver_assert) {
#ifdef _WIN32
            FreeLibrary(lib);
            lib = NULL;
#else
            dlclose(lib);
            lib = nullptr;
#endif
            return false;
        }

        loaded = true;
        return true;
    }

    ~Z3Api() {
#ifdef _WIN32
        if (lib) FreeLibrary(lib);
#else
        if (lib) dlclose(lib);
#endif
    }
};

static Z3Api api;

py::dict solve_smt_capi(const std::string &smt2, int timeout_ms = 0) {
    py::dict out;
    if (!api.load_library()) {
        out["status"] = "fallback";
        out["error"] = "Z3 C API not available, fallback recommended";
        return out;
    }

    // Prefix timeout option if provided
    std::string full = smt2;
    if (timeout_ms > 0) {
        full = "(set-option :timeout " + std::to_string(timeout_ms) + ")\n" + smt2;
    }

    void *cfg = api.mk_config();
    void *ctx = api.mk_context(cfg);
    if (!ctx) {
        out["status"] = "error";
        out["error"] = "failed to create Z3 context";
        if (api.del_config) api.del_config(cfg);
        return out;
    }

    // parse into AST vector
    void *ast_vec = api.parse_smtlib2_string(ctx, full.c_str(), 0, nullptr, nullptr, 0, nullptr, nullptr);
    if (!ast_vec) {
        // cleanup
        if (api.del_context) api.del_context(ctx);
        if (api.del_config) api.del_config(cfg);
        out["status"] = "error";
        out["error"] = "failed to parse SMT2 string";
        return out;
    }

    void *solver = api.mk_solver(ctx);
    if (!solver) {
        if (api.del_context) api.del_context(ctx);
        if (api.del_config) api.del_config(cfg);
        out["status"] = "error";
        out["error"] = "failed to create Z3 solver";
        return out;
    }

    unsigned n = api.ast_vector_size(ctx, ast_vec);
    for (unsigned i = 0; i < n; ++i) {
        void *ast = api.ast_vector_get(ctx, ast_vec, i);
        if (ast) api.solver_assert(ctx, solver, ast);
    }

    int chk = api.solver_check(ctx, solver);
    if (chk == 1) { // Z3_L_TRUE
        void *model = api.solver_get_model(ctx, solver);
        if (model && api.model_to_string) {
            const char *mstr = api.model_to_string(ctx, model);
            out["status"] = "sat";
            out["model"] = std::string(mstr ? mstr : "");
        } else {
            out["status"] = "sat";
            out["model"] = py::none();
        }
    } else if (chk == -1) {
        out["status"] = "unsat";
        out["model"] = py::none();
    } else {
        out["status"] = "unknown";
        out["model"] = py::none();
    }

    if (api.del_context) api.del_context(ctx);
    if (api.del_config) api.del_config(cfg);
    return out;
}

PYBIND11_MODULE(z3_capi, m) {
    m.doc() = "Z3 C API helper (dynamic-load fallback)";
    m.def("solve_smt_capi", &solve_smt_capi, py::arg("smt2"), py::arg("timeout_ms") = 0);
}