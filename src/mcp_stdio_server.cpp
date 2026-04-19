#include <iostream>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Forward declarations for Python-embedded helpers (implemented in pyembed.cpp)
extern void init_python();
extern void finalize_python();
extern json call_python_solve(const std::string& smt2, int timeout_ms, const std::string& session_id);
extern json call_python_explore(const std::string& expr, const std::vector<std::string>& goals, int timeout_ms, const std::string& output_syntax);
extern json call_python_run_lean(const std::string& source, const std::string& mode, int timeout_ms);
extern json call_python_loogle_search(const std::string& query, int max_results, int timeout_ms);

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Welcome banner - output to stderr so it doesn't interfere with stdio protocol.
    // ASCII-only to avoid mojibake on Windows consoles using legacy code pages (CP437/CP1252).
    fprintf(stderr, "\n");
    fprintf(stderr, "+----------------------------------------------------------------+\n");
    fprintf(stderr, "|                    MnL MCP Server                              |\n");
    fprintf(stderr, "|         Math & Logic Model Context Protocol Server             |\n");
    fprintf(stderr, "+----------------------------------------------------------------+\n");
    fprintf(stderr, "|  [ok] Z3 Persistent Session  | [ok] SymPy Persistent Session  |\n");
    fprintf(stderr, "|  [ok] Lean Persistent LSP    | Stateful REPL Enabled          |\n");
    fprintf(stderr, "+----------------------------------------------------------------+\n");
    fprintf(stderr, "|  Running in stdio mode. Ready for MCP client connections.     |\n");
    fprintf(stderr, "|  Protocol: JSON-RPC 2.0 | NDJSON framing (one object per line)|\n");
    fprintf(stderr, "|                                                                |\n");
    fprintf(stderr, "|  MCP Server Running                                            |\n");
    fprintf(stderr, "|  Local endpoint: stdio transport (no TCP/HTTP port)            |\n");
    fprintf(stderr, "+----------------------------------------------------------------+\n");
    fprintf(stderr, "\n");
    fflush(stderr);

    init_python();

    // MCP stdio transport uses newline-delimited JSON (NDJSON), one JSON object per line.
    while (true) {
        std::string body;
        if (!std::getline(std::cin, body)) {
            fprintf(stderr, "[MCP DEBUG] stdin closed. eof=%d fail=%d bad=%d\n", (int)std::cin.eof(), (int)std::cin.fail(), (int)std::cin.bad());
            break;
        }
        // Strip possible trailing \r from CRLF line endings
        if (!body.empty() && body.back() == '\r') body.pop_back();
        // Skip blank lines
        if (body.empty()) continue;

        try {
            auto req = json::parse(body);
            json resp;
            resp["jsonrpc"] = "2.0";
            if (req.contains("id")) resp["id"] = req["id"];

            std::string method = req.value("method", "");
            bool handled = false;
            bool is_tool_call = false;
            json backend_res;

            if (method == "initialize") {
                resp["result"] = {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"tools", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", "mnl-math-logic-server"},
                        {"version", "1.0.0"}
                    }}
                };
            } else if (method == "notifications/initialized") {
                // notification — no id, no response needed
                continue;
            } else if (method == "tools/list") {
                resp["result"] = {
                    {"tools", json::array({
                        {
                            {"name", "z3.solve"},
                            {"description", "Solve SMT-LIB2 constraints using the Z3 solver"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"smt2", {{"type", "string"}, {"description", "SMT-LIB2 input string"}}},
                                    {"timeout_ms", {{"type", "integer"}, {"description", "Timeout in milliseconds (0 = default, minimum 5000 when set)"}}},
                                    {"session_id", {{"type", "string"}, {"description", "Session name to persist assertions across calls. Empty (default) = fresh isolated solver per call. Use \"persistent\" for the legacy accumulating REPL, or any custom name for a named session."}}}
                                }},
                                {"required", json::array({"smt2"})}
                            }}
                        },
                        {
                            {"name", "sympy.explore"},
                            {"description", "Explore a mathematical expression with SymPy"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"expr", {{"type", "string"}, {"description", "Mathematical expression"}}},
                                    {"goals", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Goals: simplify, factor, solve, expand, differentiate, integrate"}}},
                                    {"timeout_ms", {{"type", "integer"}, {"description", "Timeout in milliseconds (0 = default, minimum 5000 when set)"}}},
                                    {"output_syntax", {{"type", "string"}, {"description", "Output syntax for results: \"python\" (default), \"lean\" (Lean 4), or \"smtlib\" (SMT-LIB2)"}}}
                                }},
                                {"required", json::array({"expr"})}
                            }}
                        },
                        {
                            {"name", "lean.exec"},
                            {"description", "Execute or check Lean 4 source code. IMPORTANT: before writing any Mathlib lemma or tactic name, call loogle_search first to confirm the exact name exists in Mathlib4 — do not guess lemma names."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"source", {{"type", "string"}, {"description", "Lean 4 source code"}}},
                                    {"mode", {{"type", "string"}, {"description", "Mode: check or eval"}}},
                                    {"timeout_ms", {{"type", "integer"}, {"description", "Timeout in milliseconds (0 = default, minimum 5000 when set). Response includes a \"summary\" field: \"N errors, N warnings[, N sorry]\"."}}}
                                }},
                                {"required", json::array({"source"})}
                            }}
                        },
                        {
                            {"name", "loogle_search"},
                            {"description", "Search Mathlib4 for real lemma and theorem names via Loogle (https://loogle.lean-lang.org). Call this before writing any Mathlib lemma name in lean_exec to avoid unknown identifier errors. Supports name fragments, type signature patterns, and #find-style queries."},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"query", {{"type", "string"}, {"description", "Loogle search string — e.g. a lemma name fragment, a type signature like \"_ * _ ≤ _ * _\", or a #find pattern"}}},
                                    {"max_results", {{"type", "integer"}, {"description", "Maximum number of hits to return (1-20, default 5)"}}},
                                    {"timeout_ms", {{"type", "integer"}, {"description", "HTTP timeout in milliseconds (0 = default 10000, minimum 5000)"}}}
                                }},
                                {"required", json::array({"query"})}
                            }}
                        }
                    })}
                };
            } else if (method == "tools/call") {
                auto params = req.value("params", json::object());
                std::string tool_name = params.value("name", "");
                auto tool_args = params.value("arguments", json::object());
                is_tool_call = true;

                if (tool_name == "z3.solve") {
                    std::string smt2 = tool_args.value("smt2", "");
                    int timeout_ms = tool_args.value("timeout_ms", 0);
                    std::string session_id = tool_args.value("session_id", "");
                    backend_res = call_python_solve(smt2, timeout_ms, session_id);
                    handled = true;
                } else if (tool_name == "sympy.explore") {
                    std::string expr = tool_args.value("expr", "");
                    std::vector<std::string> goals;
                    if (tool_args.contains("goals") && tool_args["goals"].is_array()) {
                        for (auto &g : tool_args["goals"]) goals.push_back(g.get<std::string>());
                    }
                    int timeout_ms = tool_args.value("timeout_ms", 0);
                    std::string output_syntax = tool_args.value("output_syntax", "python");
                    backend_res = call_python_explore(expr, goals, timeout_ms, output_syntax);
                    handled = true;
                } else if (tool_name == "lean.exec") {
                    std::string source = tool_args.value("source", "");
                    std::string mode = tool_args.value("mode", "check");
                    int timeout_ms = tool_args.value("timeout_ms", 0);
                    backend_res = call_python_run_lean(source, mode, timeout_ms);
                    handled = true;
                } else if (tool_name == "loogle_search") {
                    std::string query = tool_args.value("query", "");
                    int max_results = tool_args.value("max_results", 5);
                    int timeout_ms = tool_args.value("timeout_ms", 0);
                    backend_res = call_python_loogle_search(query, max_results, timeout_ms);
                    handled = true;
                } else {
                    resp["error"] = { {"code", -32601}, {"message", "Unknown tool: " + tool_name} };
                }
            } else if (method == "z3.solve") {
                auto params = req.value("params", json::object());
                std::string smt2 = params.value("smt2", "");
                int timeout_ms = params.value("timeout_ms", 0);
                std::string session_id = params.value("session_id", "");
                backend_res = call_python_solve(smt2, timeout_ms, session_id);
                handled = true;
            } else if (method == "sympy.explore") {
                auto params = req.value("params", json::object());
                std::string expr = params.value("expr", "");
                std::vector<std::string> goals;
                if (params.contains("goals") && params["goals"].is_array()) {
                    for (auto &g : params["goals"]) goals.push_back(g.get<std::string>());
                }
                int timeout_ms = params.value("timeout_ms", 0);
                std::string output_syntax = params.value("output_syntax", "python");
                backend_res = call_python_explore(expr, goals, timeout_ms, output_syntax);
                handled = true;
            } else if (method == "lean.exec") {
                auto params = req.value("params", json::object());
                std::string source = params.value("source", "");
                std::string mode = params.value("mode", "check");
                int timeout_ms = params.value("timeout_ms", 0);
                backend_res = call_python_run_lean(source, mode, timeout_ms);
                handled = true;
            } else if (method == "loogle_search") {
                auto params = req.value("params", json::object());
                std::string query = params.value("query", "");
                int max_results = params.value("max_results", 5);
                int timeout_ms = params.value("timeout_ms", 0);
                backend_res = call_python_loogle_search(query, max_results, timeout_ms);
                handled = true;
            } else if (method == "mcp.cancel" || method == "$/cancelRequest") {
                resp["error"] = { {"code", -32002}, {"message", "Cancelled (no-op)"} };
            } else {
                // Skip notifications (no id field) silently
                if (!req.contains("id")) continue;
                resp["error"] = { {"code", -32601}, {"message", "Method not found"} };
            }

            if (handled) {
                if (backend_res.is_object() && backend_res.contains("error")) {
                    if (is_tool_call) {
                        // MCP tools/call errors are returned as isError:true in content
                        resp["result"] = {
                            {"content", json::array({{
                                {"type", "text"},
                                {"text", backend_res["error"].get<std::string>()}
                            }})},
                            {"isError", true}
                        };
                    } else {
                        resp["error"] = { {"code", -32010}, {"message", backend_res["error"]} };
                    }
                } else if (is_tool_call) {
                    // MCP tools/call success: wrap result in content array
                    resp["result"] = {
                        {"content", json::array({{
                            {"type", "text"},
                            {"text", backend_res.dump()}
                        }})}
                    };
                } else {
                    resp["result"] = backend_res;
                }
            }

            // Per JSON-RPC 2.0: do not send a response to notifications (no id field).
            if (!req.contains("id")) continue;

            auto resp_str = resp.dump();
            std::string id_str = resp["id"].dump();
            fprintf(stderr, "[MCP DEBUG] Sending response (method='%s', id=%s) len=%lld\n", method.c_str(), id_str.c_str(), (long long)resp_str.size());
            std::cout << resp_str << "\n";
            std::cout.flush();
        } catch (const std::exception& e) {
            fprintf(stderr, "[MCP DEBUG] JSON parse error: %s\n", e.what());
            fprintf(stderr, "[MCP DEBUG] Body (len=%lld):\n", (long long)body.size());
            fwrite(body.data(), 1, std::min(body.size(), (size_t)1024), stderr);
            fprintf(stderr, "\n");
            json err;
            err["jsonrpc"] = "2.0";
            err["id"] = nullptr;
            err["error"] = { {"code", -32700}, {"message", "Parse error"}, {"data", e.what()} };
            std::string err_s = err.dump();
            std::cout << err_s << "\n";
            std::cout.flush();
        }
    }
 
    fprintf(stderr, "[MCP DEBUG] Main loop exited; finalizing Python.\n");
    fflush(stderr);
    finalize_python();
    return 0;
}
