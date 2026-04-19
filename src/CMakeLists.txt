cmake_minimum_required(VERSION 3.15)
project(mcp_stdio_src LANGUAGES CXX)

set(MCP_SERVER_SOURCES
  mcp_stdio_server.cpp
  pyembed.cpp
)
if(WIN32)
  list(APPEND MCP_SERVER_SOURCES server.rc)
endif()

add_executable(mcp_stdio_server ${MCP_SERVER_SOURCES})

target_link_libraries(mcp_stdio_server PRIVATE nlohmann_json::nlohmann_json ${Python_LIBRARIES})
target_include_directories(mcp_stdio_server PRIVATE ${Python_INCLUDE_DIRS})

# Pybind11 module for a persistent Lean runner prototype
# Requires pybind11 to be made available by the top-level CMakeLists (FetchContent)
pybind11_add_module(lean_bindings MODULE lean_persistent.cpp)
target_link_libraries(lean_bindings PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(lean_bindings PRIVATE ${Python_INCLUDE_DIRS})

# Pybind11 module for Z3 model serialization helper (disabled until Z3 is installed)
# pybind11_add_module(z3_bindings MODULE z3_bindings.cpp)
# target_link_libraries(z3_bindings PRIVATE nlohmann_json::nlohmann_json z3)
# target_include_directories(z3_bindings PRIVATE ${Python_INCLUDE_DIRS})

# Pybind11 module for Z3 C API wrapper (disabled until Z3 is installed)
# pybind11_add_module(z3_capi MODULE z3_capi.cpp)
# target_link_libraries(z3_capi PRIVATE nlohmann_json::nlohmann_json z3)
# target_include_directories(z3_capi PRIVATE ${Python_INCLUDE_DIRS})

# Pybind11 module for SymPy hot-path helpers
pybind11_add_module(sympy_bindings MODULE sympy_bindings.cpp)
target_link_libraries(sympy_bindings PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(sympy_bindings PRIVATE ${Python_INCLUDE_DIRS})
