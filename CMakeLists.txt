cmake_minimum_required(VERSION 3.15)
project(mcp_stdio_lean_z3 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build tests" ON)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5")

include(FetchContent)

# nlohmann::json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# pybind11
FetchContent_Declare(
  pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v2.10.4
)
FetchContent_MakeAvailable(pybind11)

# Python (for embedding)
find_package(Python COMPONENTS Interpreter Development REQUIRED)

# Subprojects
add_subdirectory(src)

message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")