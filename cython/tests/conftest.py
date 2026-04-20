"""
Ensure the freshly-built Cython extensions in build/lib.*/  take priority over
any in-place .pyd files that may be locked by a running MCP server process.
"""
import sys
import pathlib

_build = pathlib.Path(__file__).resolve().parents[1] / "build"
# Find the first lib.* directory (e.g. lib.win-amd64-cpython-314)
for candidate in sorted(_build.glob("lib.*")):
    if candidate.is_dir():
        sys.path.insert(0, str(candidate))
        break
