#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <nlohmann/json.hpp>

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

namespace py = pybind11;
using json = nlohmann::json;

class LeanServer {
public:
    LeanServer() = default;
    ~LeanServer() {
#ifdef _WIN32
        // attempt graceful shutdown
        running = false;
        if (hChildStd_IN_Wr) { CloseHandle(hChildStd_IN_Wr); hChildStd_IN_Wr = NULL; }
        if (hChildStd_OUT_Rd) { CloseHandle(hChildStd_OUT_Rd); hChildStd_OUT_Rd = NULL; }
        if (procInfo.hProcess) {
            DWORD code = 0;
            if (GetExitCodeProcess(procInfo.hProcess, &code) && code == STILL_ACTIVE) {
                WaitForSingleObject(procInfo.hProcess, 100);
            }
            CloseHandle(procInfo.hProcess);
            CloseHandle(procInfo.hThread);
        }
        if (reader.joinable()) reader.join();
#else
        // no-op for POSIX fallback
#endif
    }

    // Start the lean --server process (with simple restart/backoff)
    bool start() {
        // quick path if already running
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (running) return true;
        }

#ifdef _WIN32
        int attempts = 0;
        const int max_attempts = 5;
        int backoff_ms = 200;
        while (attempts < max_attempts) {
            // create pipes
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            HANDLE hChildStd_OUT_Rd_local = NULL;
            HANDLE hChildStd_OUT_Wr = NULL;
            HANDLE hChildStd_IN_Rd = NULL;
            HANDLE hChildStd_IN_Wr_local = NULL;

            if (!CreatePipe(&hChildStd_OUT_Rd_local, &hChildStd_OUT_Wr, &saAttr, 0)) {
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, 2000);
                continue;
            }
            SetHandleInformation(hChildStd_OUT_Rd_local, HANDLE_FLAG_INHERIT, 0);

            if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr_local, &saAttr, 0)) {
                CloseHandle(hChildStd_OUT_Rd_local);
                CloseHandle(hChildStd_OUT_Wr);
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, 2000);
                continue;
            }
            SetHandleInformation(hChildStd_IN_Wr_local, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA siStartInfo;
            PROCESS_INFORMATION piProcInfo;
            ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
            siStartInfo.cb = sizeof(STARTUPINFOA);
            siStartInfo.hStdError = hChildStd_OUT_Wr;
            siStartInfo.hStdOutput = hChildStd_OUT_Wr;
            siStartInfo.hStdInput = hChildStd_IN_Rd;
            siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

            // command to start lean server
            std::string cmd = "lean --server";
            // CreateProcessA requires writable buffer
            std::vector<char> cmdbuf(cmd.begin(), cmd.end());
            cmdbuf.push_back(0);

            BOOL ok = CreateProcessA(
                NULL,
                cmdbuf.data(),
                NULL,
                NULL,
                TRUE,
                0,
                NULL,
                NULL,
                &siStartInfo,
                &piProcInfo
            );

            // Close pipe handles no longer needed by the parent side
            CloseHandle(hChildStd_OUT_Wr);
            CloseHandle(hChildStd_IN_Rd);

            if (!ok) {
                CloseHandle(hChildStd_OUT_Rd_local);
                CloseHandle(hChildStd_IN_Wr_local);
                attempts++;
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, 2000);
                continue;
            }

            {
                std::lock_guard<std::mutex> lk(mtx);
                hChildStd_OUT_Rd = hChildStd_OUT_Rd_local;
                hChildStd_IN_Wr = hChildStd_IN_Wr_local;
                procInfo = piProcInfo;
                running = true;
                restart_count = 0;
            }

            // start reader thread
            reader = std::thread([this]() { this->read_loop_windows(); });

            // perform LSP initialize handshake (send initialize request and wait briefly)
            json init_params = {
                {"processId", nullptr},
                {"rootUri", nullptr},
                {"capabilities", json::object()}
            };
            auto resp = send_request("initialize", init_params, 5000);
            (void)resp;

            json initialized = {
                {"jsonrpc", "2.0"},
                {"method", "initialized"},
                {"params", json::object()}
            };
            send_notification(initialized);

            return true;
        } // attempts
        return false;
#else
        // POSIX: not implemented here; fallback userspace will be used
        return false;
#endif
    }

    // Send a notification (no id)
    void send_notification(const json &body) {
        json b = body;
        if (!b.contains("jsonrpc")) b["jsonrpc"] = "2.0";
        send_raw(b.dump());
    }

    // Send a request and wait for reply (by id)
    json send_request(const std::string &method, const json &params = json::object(), int timeout_ms = 5000) {
        int id;
        {
            std::lock_guard<std::mutex> lk(mtx);
            id = next_id++;
        }
        json req = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", method},
            {"params", params}
        };
        send_raw(req.dump());

        std::unique_lock<std::mutex> lk(mtx);
        bool ok = cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return responses.find(id) != responses.end() || !running; });
        if (!ok) return json();
        if (responses.find(id) == responses.end()) return json();
        json res = responses[id];
        responses.erase(id);
        return res;
    }

    // Core check: open/update document and wait for diagnostics for the URI
    py::dict run_check(const std::string &source, int timeout_ms) {
        // serialize calls to the LSP session
        std::unique_lock<std::mutex> op_lk(op_mtx);

        if (!start()) {
            py::dict d;
            d["status"] = "error";
            d["error"] = "failed to start lean server";
            d["exit_code"] = -1;
            return d;
        }

        // ensure we have a persistent temp file and URI
        if (current_file_path.empty()) {
            std::filesystem::path tmp = std::filesystem::temp_directory_path() / ("mcp_lean_session.lean");
            current_file_path = tmp.string();
            current_uri = "file:///" + tmp.string();
            for (auto &c : current_uri) if (c == '\\') c = '/';
            current_version = 1;
        } else {
            ++current_version;
        }

        // write file (idempotent)
        {
            std::ofstream ofs(current_file_path, std::ios::trunc);
            if (!ofs) {
                py::dict d;
                d["status"] = "error";
                d["error"] = "failed to write temp file";
                d["exit_code"] = -1;
                return d;
            }
            ofs << source;
            ofs.close();
        }

        // send didOpen once, otherwise didChange (use minimal diff)
        if (!opened) {
            json didOpen = {
                {"jsonrpc","2.0"},
                {"method","textDocument/didOpen"},
                {"params", {
                    {"textDocument", {
                        {"uri", current_uri},
                        {"languageId", "lean"},
                        {"version", current_version},
                        {"text", source}
                    }}
                }}
            };
            send_notification(didOpen);
            opened = true;
            last_content = source;
        } else {
            // compute minimal change between last_content and source
            int sLine=0, sChar=0, eLine=0, eChar=0, sIdx=0, eIdx=0;
            std::string replace_text;
            compute_change_range(last_content, source, sLine, sChar, eLine, eChar, sIdx, eIdx, replace_text);

            json changeParams;
            if (sIdx == eIdx && replace_text.empty()) {
                // no change
            } else {
                json textDocument = { {"uri", current_uri}, {"version", current_version} };
                json contentChange;
                if (sIdx == 0 && eIdx == (int)last_content.size()) {
                    // full-text replace
                    contentChange = json::object({ {"text", source} });
                } else {
                    contentChange = json::object({
                        {"range", {
                            {"start", { {"line", sLine}, {"character", sChar} }},
                            {"end",   { {"line", eLine}, {"character", eChar} }}
                        }},
                        {"rangeLength", eIdx - sIdx},
                        {"text", replace_text}
                    });
                }
                json didChange = {
                    {"jsonrpc","2.0"},
                    {"method","textDocument/didChange"},
                    {"params", {
                        {"textDocument", textDocument},
                        {"contentChanges", json::array({ contentChange })}
                    }}
                };
                send_notification(didChange);
                last_content = source;
            }
        }

        // Wait for diagnostics for this URI
        std::unique_lock<std::mutex> lk(mtx);
        bool got = cv.wait_for(lk, std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 5000), [&]{
            return diagnostics.find(current_uri) != diagnostics.end() || !running;
        });

        py::dict res;
        if (!got) {
            res["status"] = "error";
            res["error"] = "timeout waiting for diagnostics";
            res["exit_code"] = -1;
        } else if (!running) {
            res["status"] = "error";
            res["error"] = "lean server not running";
            res["exit_code"] = -1;
        } else {
            json diag = diagnostics[current_uri];
            bool has_error = false;
            if (diag.contains("diagnostics") && diag["diagnostics"].is_array()) {
                for (const auto &d : diag["diagnostics"]) {
                    if (d.contains("severity") && d["severity"].is_number_integer()) {
                        if (d["severity"].get<int>() == 1) { has_error = true; break; }
                    }
                }
            }
            if (has_error) {
                res["status"] = "error";
                res["stderr"] = diag.dump();
                res["exit_code"] = 1;
            } else {
                res["status"] = "ok";
                res["stdout"] = "";
                res["stderr"] = "";
                res["exit_code"] = 0;
            }
        }

        return res;
    }

    static LeanServer &instance() {
        static LeanServer s;
        return s;
    }

private:
    // Windows-specific members
#ifdef _WIN32
    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_IN_Wr = NULL;
    PROCESS_INFORMATION procInfo{};
    std::thread reader;
#endif

    std::mutex mtx;
    std::condition_variable cv;
    std::map<int, json> responses;
    std::map<std::string, json> diagnostics;
    int next_id = 1;
    bool running = false;
    int file_counter = 0;

    // persistent document state
    std::string current_file_path;
    std::string current_uri;
    int current_version = 0;
    bool opened = false;
    std::string last_content;
    int restart_count = 0;

    // serialize public run_check calls
    std::mutex op_mtx;

#ifdef _WIN32
    void read_loop_windows() {
        std::string buffer;
        const DWORD bufsize = 4096;
        std::vector<char> readbuf(bufsize);
        DWORD nRead = 0;
        while (true) {
            BOOL ok = ReadFile(hChildStd_OUT_Rd, readbuf.data(), bufsize, &nRead, NULL);
            if (!ok || nRead == 0) {
                std::lock_guard<std::mutex> lk(mtx);
                running = false;
                cv.notify_all();
                break;
            }
            buffer.append(readbuf.data(), readbuf.data() + nRead);
            parse_buffer(buffer);

            // check process exit code
            DWORD exitCode = 0;
            if (GetExitCodeProcess(procInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                std::lock_guard<std::mutex> lk(mtx);
                running = false;
                cv.notify_all();
                break;
            }
        }
    }
#endif

    void parse_buffer(std::string &buffer) {
        while (true) {
            size_t hdr_end = buffer.find("\r\n\r\n");
            if (hdr_end == std::string::npos) break;
            std::string header = buffer.substr(0, hdr_end);
            size_t pos = header.find("Content-Length:");
            if (pos == std::string::npos) {
                buffer.erase(0, hdr_end + 4);
                continue;
            }
            size_t num_start = pos + strlen("Content-Length:");
            size_t line_end = header.find("\r\n", pos);
            std::string numstr;
            if (line_end != std::string::npos) {
                numstr = header.substr(num_start, line_end - num_start);
            } else {
                numstr = header.substr(num_start);
            }
            // trim
            auto trim = [](std::string s) {
                size_t a = s.find_first_not_of(" \t");
                if (a == std::string::npos) return std::string();
                size_t b = s.find_last_not_of(" \t");
                return s.substr(a, b - a + 1);
            };
            numstr = trim(numstr);
            int len = 0;
            try {
                len = std::stoi(numstr);
            } catch (...) {
                buffer.erase(0, hdr_end + 4);
                continue;
            }
            if ((int)buffer.size() < (int)(hdr_end + 4 + len)) break; // wait for full body
            std::string body = buffer.substr(hdr_end + 4, len);
            buffer.erase(0, hdr_end + 4 + len);
            try {
                json j = json::parse(body);
                handle_message(j);
            } catch (...) {
                // ignore parse errors
            }
        }
    }

    void handle_message(const json &j) {
        std::unique_lock<std::mutex> lk(mtx);
        if (j.contains("id")) {
            try {
                int id = j["id"].get<int>();
                responses[id] = j;
                cv.notify_all();
            } catch (...) {}
        }
        if (j.contains("method") && j["method"] == "textDocument/publishDiagnostics") {
            try {
                auto params = j["params"];
                if (params.contains("uri")) {
                    std::string uri = params["uri"].get<std::string>();
                    diagnostics[uri] = params;
                    cv.notify_all();
                }
            } catch (...) {}
        }
    }

    // compute minimal change range between old_text and new_text
    void compute_change_range(const std::string &old_text, const std::string &new_text,
                              int &startLine, int &startChar, int &endLine, int &endChar,
                              int &startIndex, int &endIndex, std::string &replace_text) {
        startLine = startChar = endLine = endChar = startIndex = endIndex = 0;
        replace_text.clear();

        size_t len_old = old_text.size();
        size_t len_new = new_text.size();

        // common prefix
        size_t a = 0;
        while (a < len_old && a < len_new && old_text[a] == new_text[a]) ++a;
        // common suffix
        size_t b = 0;
        while (b < len_old - a && b < len_new - a &&
               old_text[len_old - 1 - b] == new_text[len_new - 1 - b]) ++b;

        startIndex = (int)a;
        endIndex = (int)(len_old - b);
        if (startIndex >= endIndex && (len_new - a - b) == 0) {
            // no change
            startLine = startChar = endLine = endChar = 0;
            replace_text = "";
            return;
        }

        replace_text = new_text.substr(a, len_new - a - b);

        // compute startLine/startChar
        int line = 0;
        int ch = 0;
        for (size_t i = 0; i < a; ++i) {
            if (old_text[i] == '\n') { ++line; ch = 0; }
            else ++ch;
        }
        startLine = line;
        startChar = ch;

        // compute endLine/endChar (position in old_text at endIndex)
        line = 0; ch = 0;
        for (size_t i = 0; i < (size_t)endIndex; ++i) {
            if (old_text[i] == '\n') { ++line; ch = 0; }
            else ++ch;
        }
        endLine = line;
        endChar = ch;
    }

    void send_raw(const std::string &body) {
#ifdef _WIN32
        std::string msg = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        DWORD written = 0;
        std::unique_lock<std::mutex> lk(mtx);
        if (!running) {
            if (restart_count < 3) {
                ++restart_count;
                lk.unlock(); // release while starting to avoid deadlock
                bool ok = start();
                lk.lock();
                if (!ok) return;
            } else {
                return;
            }
        }
        if (hChildStd_IN_Wr) {
            WriteFile(hChildStd_IN_Wr, msg.data(), (DWORD)msg.size(), &written, NULL);
        }
#else
        // Not implemented on POSIX fallback
#endif
    }
};

py::dict run_lean_persistent(const std::string &source, const std::string &mode = "check", int timeout_ms = 0) {
    if (mode != "check") {
        py::dict d;
        d["status"] = "error";
        d["error"] = "persistent runner only supports 'check' mode";
        d["exit_code"] = -1;
        return d;
    }
    return LeanServer::instance().run_check(source, timeout_ms);
}

PYBIND11_MODULE(lean_bindings, m) {
    m.doc() = "Persistent Lean LSP runner (hardened with incremental updates and restart/backoff)";
    m.def("run_lean_persistent", &run_lean_persistent, py::arg("source"), py::arg("mode")="check", py::arg("timeout_ms")=0);
}