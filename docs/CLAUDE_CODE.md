# Using MnL MCP with Claude Code

Claude Code (Anthropic's CLI / VS Code extension) speaks MCP natively, so no
extension or plugin needs to be built — the existing `mcp_stdio_server.exe` is
picked up as a regular stdio MCP server once it's registered in Claude Code's
config.

## Prerequisites

- MnL MCP built at `C:\dev\MnL_MCP\build\src\Release\mcp_stdio_server.exe` (or
  installed via `mcp_server_installer.exe`, in which case the path is
  `C:\Program Files\MCP Server\mcp_stdio_server.exe`).
- The `cython/` directory from this repo (contains `mcp_z3.py`, `mcp_sympy.py`,
  `mcp_lean.py`) on `PYTHONPATH`.

## Option A — `claude mcp add` (recommended)

From any shell:

```powershell
claude mcp add `
  --transport stdio `
  --scope user `
  --env PYTHONPATH="C:\dev\MnL_MCP\build\src\Release;C:\dev\MnL_MCP\cython" `
  mnl-math-logic-server `
  -- "C:\dev\MnL_MCP\build\src\Release\mcp_stdio_server.exe"
```

Verify:

```powershell
claude mcp list
```

`--scope user` makes the server available in every project. Use `--scope local`
(the default) to register it only for the current project, or `--scope project`
to share it via a checked-in `.mcp.json`.

## Option B — edit the config file directly

Claude Code's user-level config lives at `%USERPROFILE%\.claude.json`. Add an
entry under `mcpServers`:

```json
{
  "mcpServers": {
    "mnl-math-logic-server": {
      "type": "stdio",
      "command": "C:\\dev\\MnL_MCP\\build\\src\\Release\\mcp_stdio_server.exe",
      "env": {
        "PYTHONPATH": "C:\\dev\\MnL_MCP\\build\\src\\Release;C:\\dev\\MnL_MCP\\cython"
      }
    }
  }
}
```

Restart Claude Code (or reload the VS Code window) after editing.

## Exposed tools

Once connected, Claude Code sees three tools:

- `z3.solve` — SMT-LIB2 constraint solving via Z3
- `sympy.explore` — symbolic math (simplify, factor, solve, expand, diff, integrate)
- `lean.exec` — Lean 4 source check/eval

## Notes

- Keep `PYTHONPATH` pointing at **both** `build\src\Release` (for the compiled
  `.pyd` bindings) and `cython\` (for the pure-Python wrappers).
- Claude Code does not shell-expand env values — write `%USERPROFILE%` literally
  using Claude Code's own `${...}` syntax if you need expansion, e.g.
  `"PYTHONPATH": "${USERPROFILE}\\some\\path"`.
- The server logs startup banner and debug messages to stderr, which Claude
  Code ignores for protocol purposes. Only stdout carries JSON-RPC.
