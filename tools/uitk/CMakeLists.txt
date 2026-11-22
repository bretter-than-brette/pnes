cmake_minimum_required(VERSION 3.21)

project(uitk CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(uitk main.cpp)
target_link_libraries(uitk PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(WIN32)
    set_target_properties(uitk PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    set_target_properties(uitk PROPERTIES
            MACOSX_BUNDLE TRUE
            INSTALL_RPATH "@executable_path")
endif()
