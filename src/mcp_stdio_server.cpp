#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Forward declarations for Python-embedded helpers (implemented in pyembed.cpp)
extern void init_python();
extern void finalize_python();
extern json call_python_solve(const std::string& smt2, int timeout_ms);
extern json call_python_explore(const std::string& expr, const std::vector<std::string>& goals, int timeout_ms);
extern json call_python_run_lean(const std::string& source, const std::string& mode, int timeout_ms);
extern json call_python_cvc5(const std::string& smt2, int timeout_ms);

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    init_python();

    while (true) {
        std::string line;
        int content_length = 0;

        // Read headers (Content-Length)
        while (std::getline(std::cin, line)) {
            if (line.empty() || line == "\r") break;
            const std::string prefix = "Content-Length:";
            if (line.rfind(prefix, 0) == 0) {
                std::string val = line.substr(prefix.size());
                val = trim(val);
                try { content_length = std::stoi(val); } catch (...) { content_length = 0; }
            }
        }

        if (content_length <= 0) {
            if (!std::cin || std::cin.eof()) break;
            continue;
        }

        std::string body(content_length, '\0');
        std::cin.read(&body[0], content_length);
        if (!std::cin) break;

        try {
            auto req = json::parse(body);
            json resp;
            resp["jsonrpc"] = "2.0";
            if (req.contains("id")) resp["id"] = req["id"];

            std::string method = req.value("method", "");
            bool handled = false;
            json backend_res;

            if (method == "z3.solve") {
                auto params = req.value("params", json::object());
                std::string smt2 = params.value("smt2", "");
                int timeout_ms = params.value("timeout_ms", 0);
                backend_res = call_python_solve(smt2, timeout_ms);
                handled = true;
            } else if (method == "sympy.explore") {
                auto params = req.value("params", json::object());
                std::string expr = params.value("expr", "");
                std::vector<std::string> goals;
                if (params.contains("goals") && params["goals"].is_array()) {
                    for (auto &g : params["goals"]) goals.push_back(g.get<std::string>());
                }
                int timeout_ms = params.value("timeout_ms", 0);
                backend_res = call_python_explore(expr, goals, timeout_ms);
                handled = true;
            } else if (method == "lean.exec") {
                auto params = req.value("params", json::object());
                std::string source = params.value("source", "");
                std::string mode = params.value("mode", "check");
                int timeout_ms = params.value("timeout_ms", 0);
                backend_res = call_python_run_lean(source, mode, timeout_ms);
                handled = true;
            } else if (method == "mcp.cancel") {
                // cancellation: not implemented in this stub
                resp["error"] = { {"code", -32002}, {"message", "Cancelled (no-op)"} };
            } else {
                resp["error"] = { {"code", -32601}, {"message", "Method not found"} };
            }

            if (handled) {
                // backend_res may include {"error": "..."} on failure
                if (backend_res.is_object() && backend_res.contains("error")) {
                    resp["error"] = { {"code", -32010}, {"message", backend_res["error"]} };
                } else {
                    resp["result"] = backend_res;
                }
            }

            auto resp_str = resp.dump();
            std::cout << "Content-Length: " << resp_str.size() << "\r\n\r\n" << resp_str;
            std::cout.flush();
        } catch (const std::exception& e) {
            json err;
            err["jsonrpc"] = "2.0";
            err["error"] = { {"code", -32700}, {"message", "Parse error"}, {"data", e.what()} };
            std::string err_s = err.dump();
            std::cout << "Content-Length: " << err_s.size() << "\r\n\r\n" << err_s;
            std::cout.flush();
        }
    }

    finalize_python();
    return 0;
}