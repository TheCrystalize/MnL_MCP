#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <unordered_map>
#include <deque>
#include <mutex>
#include <string>

namespace py = pybind11;

static std::unordered_map<std::string, py::dict> result_cache;
static std::deque<std::string> result_order;
static std::mutex cache_mtx;
static const size_t MAX_CACHE = 256;

py::dict solve_smt_fast(const std::string &smt2, int timeout_ms = 0) {
    py::dict res;
    try {
        // Return cached result for identical queries (only when no timeout requested)
        if (timeout_ms == 0) {
            std::lock_guard<std::mutex> lk(cache_mtx);
            auto it = result_cache.find(smt2);
            if (it != result_cache.end()) {
                return it->second;
            }
        }

        py::module z3 = py::module::import("z3");
        py::object Solver = z3.attr("Solver");
        py::object s = Solver();

        try {
            py::object parsed = z3.attr("parse_smt2_string")(smt2);
            for (py::handle a : parsed) {
                s.attr("add")(a);
            }
        } catch (py::error_already_set &e) {
            // parse may fail for some inputs; clear and continue
            PyErr_Clear();
        }

        if (timeout_ms) {
            s.attr("set")(py::str("timeout"), timeout_ms);
        }

        py::object r = s.attr("check")();
        if (r.equal(z3.attr("sat"))) {
            py::object m = s.attr("model")();
            py::dict model;
            py::list decls = m.attr("decls")();
            for (py::handle d : decls) {
                py::object name = d.attr("name")();
                py::object val = m.attr("eval")(d);
                if (py::isinstance<py::int_>(val)) {
                    model[py::str(name)] = val.cast<long long>();
                } else if (py::isinstance<py::float_>(val)) {
                    model[py::str(name)] = val.cast<double>();
                } else if (py::isinstance<py::bool_>(val)) {
                    model[py::str(name)] = val.cast<bool>();
                } else {
                    model[py::str(name)] = py::str(val);
                }
            }
            res["status"] = "sat";
            res["model"] = model;
        } else if (r.equal(z3.attr("unsat"))) {
            res["status"] = "unsat";
            res["model"] = py::none();
        } else {
            res["status"] = "unknown";
            res["model"] = py::none();
        }
    } catch (py::error_already_set &e) {
        res["status"] = "error";
        res["error"] = std::string(e.what());
        PyErr_Clear();
    } catch (const std::exception &e) {
        res["status"] = "error";
        res["error"] = e.what();
    }

    // Cache the result for identical queries (only when no timeout)
    if (timeout_ms == 0) {
        std::lock_guard<std::mutex> lk(cache_mtx);
        result_cache.emplace(smt2, res);
        result_order.push_back(smt2);
        if (result_order.size() > MAX_CACHE) {
            auto rem = result_order.front();
            result_order.pop_front();
            result_cache.erase(rem);
        }
    }

    return res;
}

void clear_z3_cache() {
    std::lock_guard<std::mutex> lk(cache_mtx);
    result_cache.clear();
    result_order.clear();
}

size_t z3_cache_size() {
    std::lock_guard<std::mutex> lk(cache_mtx);
    return result_cache.size();
}

PYBIND11_MODULE(z3_bindings, m) {
    m.doc() = "Z3 helper bindings that serialize models with lower Python overhead and result caching";
    m.def("solve_smt_fast", &solve_smt_fast, py::arg("smt2"), py::arg("timeout_ms") = 0);
    m.def("clear_cache", &clear_z3_cache);
    m.def("cache_size", &z3_cache_size);
}