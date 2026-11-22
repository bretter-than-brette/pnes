cmake_minimum_required(VERSION 3.21)

project(animator CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)

set(ANIMATOR_GUI "AUTO" CACHE STRING "Build the designer GUI: ON, OFF or AUTO")
set_property(CACHE ANIMATOR_GUI PROPERTY STRINGS ON OFF AUTO)

if(ANIMATOR_GUI)
    if(ANIMATOR_GUI STREQUAL "AUTO")
        find_package(QT NAMES Qt6 Qt5 QUIET COMPONENTS Widgets)
    else()
        find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    endif()
    if(QT_FOUND)
        find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
    endif()
endif()

find_package(tomlplusplus CONFIG QUIET)

if(NOT tomlplusplus_FOUND)
    message(STATUS "animator: no system toml++, fetching it instead")
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
            GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
            GIT_TAG v3.4.0
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

set(ANIMATOR_CORE_SOURCES
        chr_table.cpp
        codegen.cpp
        exporter.cpp
        palette.cpp
        project.cpp
)

add_executable(animator-export
        exporter_main.cpp
        ${ANIMATOR_CORE_SOURCES}
)
target_link_libraries(animator-export PRIVATE tomlplusplus::tomlplusplus)
set_target_properties(animator-export PROPERTIES AUTOMOC OFF)

if(QT_FOUND)
    add_executable(animator
            main.cpp
            animation_preview.cpp
            frame_window.cpp
            palette_store.cpp
            palette_window.cpp
            metasprite_field.cpp
            metasprite_frame.cpp
            metasprite_page.cpp
            tile_canvas.cpp
            tile_image.cpp
            ${ANIMATOR_CORE_SOURCES}
    )
    target_link_libraries(animator PRIVATE
            Qt${QT_VERSION_MAJOR}::Widgets
            tomlplusplus::tomlplusplus
    )

    if(WIN32)
        set_target_properties(animator PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(APPLE)
        set_target_properties(animator PROPERTIES
                MACOSX_BUNDLE TRUE
                INSTALL_RPATH "@executable_path")
    endif()
elseif(ANIMATOR_GUI STREQUAL "AUTO")
    message(STATUS
            "animator: no Qt Widgets found -- building animator-export only. "
            "Install Qt6 (or Qt5) for the designer GUI itself, or pass "
            "-DANIMATOR_GUI=ON to make a missing Qt an error instead.")
endif()
