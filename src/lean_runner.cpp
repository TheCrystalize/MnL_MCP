#include <string>
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
