cmake_minimum_required(VERSION 3.23)
project(libnest2d_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pybind11 CONFIG REQUIRED)

option(LIBNEST2D_FETCH "Fetch libnest2d if not present" ON)
set(LIBNEST2D_SOURCE_DIR "" CACHE PATH "Path to libnest2d source")

set(LIBNEST2D_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(LIBNEST2D_GEOMETRIES clipper CACHE STRING "" FORCE)
set(LIBNEST2D_OPTIMIZER optimlib CACHE STRING "" FORCE)
set(LIBNEST2D_THREADING none CACHE STRING "" FORCE)
set(RP_ENABLE_DOWNLOADING ON CACHE BOOL "" FORCE)

if (LIBNEST2D_SOURCE_DIR)
    set(libnest2d_SOURCE_DIR ${LIBNEST2D_SOURCE_DIR})
    set(libnest2d_BINARY_DIR ${CMAKE_BINARY_DIR}/_deps/libnest2d-build)
elseif (LIBNEST2D_FETCH)
    include(FetchContent)
    FetchContent_Declare(
        libnest2d
        GIT_REPOSITORY https://github.com/tamasmeszaros/libnest2d.git
        GIT_TAG 663daa69e1d7478669f714218e27681edbc96640
        GIT_SHALLOW TRUE
    )
    FetchContent_Populate(libnest2d)
endif()

if (DEFINED libnest2d_SOURCE_DIR)
    set(OPTIMLIB_CMAKE "${libnest2d_SOURCE_DIR}/include/libnest2d/optimizers/optimlib/CMakeLists.txt")
    if (EXISTS "${OPTIMLIB_CMAKE}")
        file(READ "${OPTIMLIB_CMAKE}" OPTIMLIB_CONTENT)
        if (OPTIMLIB_CONTENT MATCHES "optimlibOptimizer" AND NOT OPTIMLIB_CONTENT MATCHES "Libnest2DTargets")
            file(APPEND "${OPTIMLIB_CMAKE}" "\ninstall(TARGETS optimlibOptimizer EXPORT Libnest2DTargets)\n")
        endif()
    endif()
    add_subdirectory(${libnest2d_SOURCE_DIR} ${libnest2d_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

set(CORE_SRC cpp/libnest2d_core.cpp)
if (TARGET libnest2d_headeronly)
    set(CORE_SRC cpp/libnest2d_core_lib.cpp)
endif()

pybind11_add_module(libnest2d_core ${CORE_SRC})

if (TARGET libnest2d_headeronly)
    target_link_libraries(libnest2d_core PRIVATE libnest2d_headeronly)
    target_compile_definitions(libnest2d_core PRIVATE USE_LIBNEST2D)
endif()
