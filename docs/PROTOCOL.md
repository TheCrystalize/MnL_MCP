# Protocol — JSON-RPC 2.0 over stdio (Content-Length framing)

Purpose
- Wire format: JSON-RPC 2.0 transported over stdio using Content-Length framing (LSP-style).
- Goals: simple, debuggable, language-agnostic, easily consumable by TypeScript clients generated from JSON Schema.

Framing
- Each message is prefixed with a header block that MUST include `Content-Length: <N>` followed by an empty line, then exactly N bytes of UTF-8 JSON.

Example (raw stdio message):

```bash
Content-Length: 123\r\n
\r\n
{"jsonrpc":"2.0","id":1,"method":"z3.solve","params":{"smt2":"(declare-const x Int) (assert (> x 0)) (check-sat)","timeout_ms":5000}}
```

Reading loop (pseudo-C++):

```cpp
// read headers until an empty line, parse Content-Length, then read body
std::string line;
int content_length = 0;
while (std::getline(std::cin, line) && !line.empty()) {
    if (line.rfind("Content-Length:", 0) == 0) {
        content_length = std::stoi(line.substr(15));
    }
}
std::string body(content_length, '\0');
std::cin.read(&body[0], content_length);
auto req = nlohmann::json::parse(body);
```

Message shape
- Use JSON-RPC 2.0 standard request/response/notification shapes.
- All method params/results MUST conform to method-specific JSON Schema files in docs/API_SCHEMA/.

Example request:

```json
{
  "jsonrpc":"2.0",
  "id": 1,
  "method":"lean.exec",
  "params": {
    "source":"#eval 2+2",
    "mode":"eval",
    "timeout_ms":5000
  }
}
```

Example response:

```json
{
  "jsonrpc":"2.0",
  "id": 1,
  "result": {
    "status":"ok",
    "stdout":"4\n",
    "stderr":"",
    "runtime_ms": 12,
    "exit_code": 0
  }
}
```

Validation
- Server MUST validate incoming requests against JSON Schema for each method and return a JSON-RPC error (-32602 Invalid params) if validation fails.
- Schemas live in docs/API_SCHEMA/*.schema.json and are the source of truth for TS client generation.

Error model
- Standard JSON-RPC errors:
  - -32700 Parse error
  - -32600 Invalid Request
  - -32601 Method not found
  - -32602 Invalid params
  - -32603 Internal error
- Server-defined (use codes in -32000 .. -32099):
  - -32001 Timeout — job exceeded allowed time (include data.timeout_ms)
  - -32002 Cancelled — job cancelled by client
  - -32003 SandboxError — resource or isolation failure (include data.details)
  - -32010 BackendError — solver/tool-specific error (include backend name and raw output)

Cancellation
- Cancellation via notification:
  - Method: `$/cancelRequest` or `mcp.cancel` (notification)
  - Params: `{ "id": <request-id> }`
- Server MUST attempt to cancel running work and reply with -32002 Cancelled for the cancelled request when possible.

Notifications and progress
- Handlers MAY emit progress notifications (no id required) to inform clients:
  - Method: `mcp.progress` (notification)
  - Params: `{ "id": <request-id>, "percent": 0-100, "message": "..." }`
- For long-running or streaming results use notifications + final result response.

Batching
- JSON-RPC batching is supported but discouraged for long-running or interactive jobs. Prefer issuing independent requests and using ids for concurrency control.

Limits & timeouts
- Default max message body: 4 MiB (configurable).
- Default per-job timeout: server-default (e.g., 30s) but individual methods SHOULD accept `timeout_ms` param to override within safe bounds.
- If a job exceeds its allowed resources, server returns a -32001 Timeout or -32003 SandboxError.

Response metadata
- Responses SHOULD include diagnostic metadata:
  - runtime_ms — milliseconds spent executing
  - exit_code — subprocess exit code (if applicable)
  - stdout / stderr — captured output (truncate if large; indicate truncation)
  - stats — solver-specific statistics (optional, structured)

Type generation
- Generate TypeScript types from JSON Schema (docs/API_SCHEMA) using `json-schema-to-typescript` or `quicktype`.
- TypeScript clients should serialize/deserialize to the same JSON-RPC framing described above.

Security & sanitation
- Always validate incoming JSON against schemas.
- Limit allowed sizes and execution resources.
- Treat `source`/`smt2` payloads as untrusted and execute in sandboxes (timeouts, process isolation).

Backward compatibility
- Additive changes to method params/results are allowed (new optional fields).
- Breaking changes require a major version bump and clear migration docs.

Examples and schemas
- See docs/API/ for per-method examples and docs/API_SCHEMA/ for schema files.