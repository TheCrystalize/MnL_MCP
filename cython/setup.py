"""
Build the three MCP Cython extension modules.

  cd cython
  python setup.py build_ext --inplace

Produces mcp_z3.pyd / mcp_sympy.pyd / mcp_lean.pyd alongside the .pyx sources.
The C++ server loads them via PyImport_ImportModule, which prefers compiled .pyd
over the plain .py fallbacks when both are present.
"""

from setuptools import setup, Extension
from Cython.Build import cythonize

extensions = [
    Extension("mcp_z3",    ["mcp_z3.pyx"]),
    Extension("mcp_sympy", ["mcp_sympy.pyx"]),
    Extension("mcp_lean",  ["mcp_lean.pyx"]),
]

setup(
    name="mnl_mcp_cython",
    ext_modules=cythonize(
        extensions,
        compiler_directives={
            "language_level": 3,
            "boundscheck": False,
            "wraparound": False,
        },
        annotate=True,   # produces .html annotation files for profiling
    ),
)
