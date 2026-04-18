#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <unordered_map>
#include <deque>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <regex>
#include <optional>
#include <map>
#include <tuple>
#include <vector>

namespace py = pybind11;

static std::unordered_map<std::string, py::object> expr_cache_sympy;
static std::deque<std::string> expr_order_sympy;

static std::unordered_map<std::string, py::object> expr_cache_symengine;
static std::deque<std::string> expr_order_symengine;

static std::unordered_map<std::string, py::object> result_cache;
static std::deque<std::string> result_order;

static std::mutex cache_mtx;
static const size_t MAX_CACHE = 64;

static bool symengine_tried = false;
static bool symengine_ok = false;
static py::object symengine_mod;

static std::string make_cache_key(const std::vector<std::string> &goals, const std::string &expr) {
    std::ostringstream oss;
    for (size_t i = 0; i < goals.size(); ++i) {
        if (i) oss << ",";
        oss << goals[i];
    }
    oss << ":" << expr;
    return oss.str();
}

// Quick heuristic C++ polynomial simplifier for common numeric monomial patterns.
// Returns optional simplified expression string; empty => fallback to SymPy/SymEngine.
static std::optional<std::string> try_fast_poly_simplify(const std::string &expr) {
    // Match patterns like: 3*x**4 or -2*y**2 (with optional spaces)
    static const std::regex mono_re(R"(([+-]?\d+)\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\*\*\s*(\d+))");
    std::sregex_iterator it(expr.begin(), expr.end(), mono_re);
    std::sregex_iterator end;
    if (it == end) return std::nullopt;

    std::map<std::pair<std::string,int>, long long> coeffs;
    size_t matched_len = 0;
    for (; it != end; ++it) {
        const std::smatch &m = *it;
        std::string coeff_str = m[1].str();
        std::string var = m[2].str();
        int power = 0;
        try {
            power = std::stoi(m[3].str());
        } catch (...) { return std::nullopt; }
        long long coeff = 0;
        try { coeff = std::stoll(coeff_str); } catch (...) { return std::nullopt; }
        coeffs[{var, power}] += coeff;
        matched_len += m.length();
    }

    if (coeffs.empty()) return std::nullopt;
    // Ensure matches cover a reasonable fraction of the input to avoid false positives
    if (expr.size() > 0 && (double)matched_len / (double)expr.size() < 0.35) return std::nullopt;

    // Build canonical representation: ordered by variable then power ascending
    std::vector<std::tuple<std::string,int,long long>> terms;
    terms.reserve(coeffs.size());
    for (const auto &p : coeffs) terms.emplace_back(p.first.first, p.first.second, p.second);

    std::sort(terms.begin(), terms.end(), [](auto const &a, auto const &b){
        if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
        return std::get<1>(a) < std::get<1>(b);
    });

    std::ostringstream oss;
    bool first = true;
    for (auto &t : terms) {
        long long c = std::get<2>(t);
        if (c == 0) continue;
        const std::string &v = std::get<0>(t);
        int pwr = std::get<1>(t);
        if (!first) {
            if (c > 0) oss << " + ";
            else { oss << " - "; c = -c; }
        } else {
            if (c < 0) { oss << "-"; c = -c; }
        }
        oss << c << "*" << v << "**" << pwr;
        first = false;
    }
    if (first) return std::nullopt;
    return oss.str();
}

py::dict explore_fast(const std::string &expr, const std::vector<std::string> &goals, int timeout_ms = 0) {
    py::dict out;
    try {
        // Early fast-path: heuristic polynomial simplifier for "simplify" goal
        for (const auto &g : goals) {
            if (g == "simplify") {
                auto opt = try_fast_poly_simplify(expr);
                if (opt.has_value()) {
                    py::dict results;
                    results[py::str("simplify")] = py::str(opt.value());

                    // cache result
                    std::string full_key = make_cache_key(goals, expr);
                    {
                        std::lock_guard<std::mutex> lk(cache_mtx);
                        result_cache.emplace(full_key, results);
                        result_order.push_back(full_key);
                        if (result_order.size() > MAX_CACHE) {
                            auto rem = result_order.front();
                            result_order.pop_front();
                            result_cache.erase(rem);
                        }
                    }

                    out["status"] = "ok";
                    out["results"] = results;
                    return out;
                }
                break;
            }
        }

        std::string full_key = make_cache_key(goals, expr);
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            auto it = result_cache.find(full_key);
            if (it != result_cache.end()) {
                out["status"] = "ok";
                out["results"] = it->second;
                return out;
            }
        }

        // Try to load symengine once; fall back to sympy if not available.
        if (!symengine_tried) {
            try {
                symengine_mod = py::module::import("symengine");
                symengine_ok = true;
            } catch (py::error_already_set &e) {
                symengine_ok = false;
                PyErr_Clear();
            }
            symengine_tried = true;
        }

        if (symengine_ok) {
            // Use symengine path with its own parse cache
            py::object parsed;
            {
                std::lock_guard<std::mutex> lk(cache_mtx);
                auto it = expr_cache_symengine.find(expr);
                if (it != expr_cache_symengine.end()) {
                    parsed = it->second;
                    auto pos = std::find(expr_order_symengine.begin(), expr_order_symengine.end(), expr);
                    if (pos != expr_order_symengine.end()) {
                        expr_order_symengine.erase(pos);
                        expr_order_symengine.push_back(expr);
                    }
                }
            }
            if (!parsed) {
                py::object parsed_local = symengine_mod.attr("sympify")(expr);
                {
                    std::lock_guard<std::mutex> lk(cache_mtx);
                    expr_cache_symengine.emplace(expr, parsed_local);
                    expr_order_symengine.push_back(expr);
                    if (expr_order_symengine.size() > MAX_CACHE) {
                        auto rem = expr_order_symengine.front();
                        expr_order_symengine.pop_front();
                        expr_cache_symengine.erase(rem);
                    }
                }
                parsed = parsed_local;
            }

            py::dict results;
            for (const auto &g : goals) {
                try {
                    if (g == "simplify") {
                        py::object r = symengine_mod.attr("simplify")(parsed);
                        results[py::str(g)] = py::str(r);
                    } else if (g == "factor") {
                        py::object r = symengine_mod.attr("factor")(parsed);
                        results[py::str(g)] = py::str(r);
                    } else if (g == "solve") {
                        try {
                            py::object r = symengine_mod.attr("solve")(parsed);
                            py::list outlist;
                            for (py::handle v : r) outlist.append(py::str(v));
                            results[py::str(g)] = outlist;
                        } catch (...) {
                            py::module sp = py::module::import("sympy");
                            py::object r = sp.attr("solve")(sp.attr("sympify")(expr));
                            py::list outlist;
                            for (py::handle v : r) outlist.append(py::str(v));
                            results[py::str(g)] = outlist;
                        }
                    } else if (g == "expand") {
                        py::object r = symengine_mod.attr("expand")(parsed);
                        results[py::str(g)] = py::str(r);
                    } else if (g == "differentiate") {
                        try {
                            py::object free_syms = parsed.attr("free_symbols");
                            py::list syms = py::list(free_syms);
                            if (syms.size() > 0) {
                                py::object var = syms[0];
                                py::object r = symengine_mod.attr("diff")(parsed, var);
                                results[py::str(g)] = py::str(r);
                            } else {
                                results[py::str(g)] = py::none();
                            }
                        } catch (...) {
                            results[py::str(g)] = py::none();
                        }
                    } else if (g == "integrate") {
                        try {
                            py::object free_syms = parsed.attr("free_symbols");
                            py::list syms = py::list(free_syms);
                            if (syms.size() > 0) {
                                py::object var = syms[0];
                                py::object r = symengine_mod.attr("integrate")(parsed, var);
                                results[py::str(g)] = py::str(r);
                            } else {
                                results[py::str(g)] = py::none();
                            }
                        } catch (...) {
                            results[py::str(g)] = py::none();
                        }
                    } else {
                        results[py::str(g)] = py::none();
                    }
                } catch (py::error_already_set &e) {
                    results[py::str(g)] = py::str(std::string("error: ") + e.what());
                    PyErr_Clear();
                }
            }

            {
                std::lock_guard<std::mutex> lk(cache_mtx);
                result_cache.emplace(full_key, results);
                result_order.push_back(full_key);
                if (result_order.size() > MAX_CACHE) {
                    auto rem = result_order.front();
                    result_order.pop_front();
                    result_cache.erase(rem);
                }
            }

            out["status"] = "ok";
            out["results"] = results;
            return out;
        }

        // Fallback: sympy path with parse caching
        py::module sp = py::module::import("sympy");

        py::object parsed;
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            auto it = expr_cache_sympy.find(expr);
            if (it != expr_cache_sympy.end()) {
                parsed = it->second;
                auto pos = std::find(expr_order_sympy.begin(), expr_order_sympy.end(), expr);
                if (pos != expr_order_sympy.end()) {
                    expr_order_sympy.erase(pos);
                    expr_order_sympy.push_back(expr);
                }
            }
        }
        if (!parsed) {
            py::object parsed_local = sp.attr("sympify")(expr);
            {
                std::lock_guard<std::mutex> lk(cache_mtx);
                expr_cache_sympy.emplace(expr, parsed_local);
                expr_order_sympy.push_back(expr);
                if (expr_order_sympy.size() > MAX_CACHE) {
                    auto rem = expr_order_sympy.front();
                    expr_order_sympy.pop_front();
                    expr_cache_sympy.erase(rem);
                }
            }
            parsed = parsed_local;
        }

        py::dict results;
        for (const auto &g : goals) {
            try {
                if (g == "simplify") {
                    py::object r = sp.attr("simplify")(parsed);
                    results[py::str(g)] = py::str(r);
                } else if (g == "factor") {
                    py::object r = sp.attr("factor")(parsed);
                    results[py::str(g)] = py::str(r);
                } else if (g == "solve") {
                    py::object r = sp.attr("solve")(parsed);
                    py::list outlist;
                    for (py::handle v : r) outlist.append(py::str(v));
                    results[py::str(g)] = outlist;
                } else if (g == "expand") {
                    py::object r = sp.attr("expand")(parsed);
                    results[py::str(g)] = py::str(r);
                } else if (g == "differentiate") {
                    py::object free_syms = parsed.attr("free_symbols");
                    py::list syms = py::list(free_syms);
                    if (syms.size() > 0) {
                        py::object var = syms[0];
                        py::object r = sp.attr("diff")(parsed, var);
                        results[py::str(g)] = py::str(r);
                    } else {
                        results[py::str(g)] = py::none();
                    }
                } else if (g == "integrate") {
                    py::object free_syms = parsed.attr("free_symbols");
                    py::list syms = py::list(free_syms);
                    if (syms.size() > 0) {
                        py::object var = syms[0];
                        py::object r = sp.attr("integrate")(parsed, var);
                        results[py::str(g)] = py::str(r);
                    } else {
                        results[py::str(g)] = py::none();
                    }
                } else {
                    results[py::str(g)] = py::none();
                }
            } catch (py::error_already_set &e) {
                results[py::str(g)] = py::str(std::string("error: ") + e.what());
                PyErr_Clear();
            }
        }

        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            result_cache.emplace(full_key, results);
            result_order.push_back(full_key);
            if (result_order.size() > MAX_CACHE) {
                auto rem = result_order.front();
                result_order.pop_front();
                result_cache.erase(rem);
            }
        }

        out["status"] = "ok";
        out["results"] = results;
        return out;
    } catch (py::error_already_set &e) {
        out["status"] = "error";
        out["error"] = std::string(e.what());
        PyErr_Clear();
    } catch (const std::exception &e) {
        out["status"] = "error";
        out["error"] = e.what();
    }
    return out;
}

void clear_sympy_cache() {
    std::lock_guard<std::mutex> lk(cache_mtx);
    expr_cache_sympy.clear();
    expr_order_sympy.clear();
    expr_cache_symengine.clear();
    expr_order_symengine.clear();
    result_cache.clear();
    result_order.clear();
}

size_t sympy_cache_size() {
    std::lock_guard<std::mutex> lk(cache_mtx);
    return expr_cache_sympy.size() + expr_cache_symengine.size() + result_cache.size();
}

PYBIND11_MODULE(sympy_bindings, m) {
    m.doc() = "SymPy helper bindings that minimize Python-side overhead for common operations (with parse & result caching, SymEngine fallback, and a small C++ polynomial fast-path)";
    m.def("explore_fast", &explore_fast, py::arg("expr"), py::arg("goals"), py::arg("timeout_ms") = 0);
    m.def("clear_cache", &clear_sympy_cache);
    m.def("cache_size", &sympy_cache_size);
}