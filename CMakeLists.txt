# Set version requirement
cmake_minimum_required(VERSION 3.20)

# Define project
project(piep-cxx
    VERSION 0.0.1
    DESCRIPTION "Interprocess communication test project using pipes."
    LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default to Debug
if("${CMAKE_BUILD_TYPE}" STREQUAL "")
    set(CMAKE_BUILD_TYPE Debug)
endif()
message("Build type: ${CMAKE_BUILD_TYPE}")

# Set publisher name 
set(EXE "pipe-cxx")
# Add executable
add_executable(${EXE} "src/main.cxx")
target_include_directories(${EXE} PUBLIC ${CMAKE_SOURCE_DIR}/src)

