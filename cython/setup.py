from setuptools import setup, Extension
from Cython.Build import cythonize

extensions = [
    Extension("mcp_z3", ["mcp_z3.pyx"]),
    Extension("mcp_sympy", ["mcp_sympy.pyx"]),
]

setup(
    name="mcp_cython",
    ext_modules=cythonize(extensions, language_level=3),
)
