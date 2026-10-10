cmake_minimum_required(VERSION 3.20)

foreach(dependency IN ITEMS daoc-niflib DAoC-Client-Lib)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dependency}/CMakeLists.txt")
        message(FATAL_ERROR "Required ${dependency} source missing. Run git submodule update --init --recursive from the Camelot Core checkout.")
    endif()
endforeach()

project(CamelotCoreDependencies LANGUAGES C CXX)

include(CTest)
option(NIFLIB_BUILD_TESTS "Build NIF library regressions" ON)
option(DAOC_CLIENT_LIB_BUILD_TESTS "Build client-data library regressions" ON)

# Declare the NIF target before the client library, which links Niflib::niflib.
add_subdirectory(daoc-niflib)
add_subdirectory(DAoC-Client-Lib)
