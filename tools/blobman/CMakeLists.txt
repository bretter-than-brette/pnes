cmake_minimum_required(VERSION 3.18)
project(blobman CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(tomlplusplus CONFIG QUIET)

if (NOT tomlplusplus_FOUND)
    message(STATUS "blobman: no system toml++, fetching it instead")
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
            GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
            GIT_TAG v3.4.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

add_executable(blobman main.cpp)
target_link_libraries(blobman PRIVATE tomlplusplus::tomlplusplus)
