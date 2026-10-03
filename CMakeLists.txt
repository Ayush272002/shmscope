cmake_minimum_required(VERSION 4.2)

option(SHMSCOPE_USE_CONAN "Fetch dependencies with Conan" ON)
if(SHMSCOPE_USE_CONAN)
    set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/conan.cmake)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SHMSCOPE_VERSION)

project(shmscope VERSION ${SHMSCOPE_VERSION} LANGUAGES C CXX)

foreach(_shm_language C CXX)
    if(NOT CMAKE_${_shm_language}_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "shmscope builds with Clang only, but the ${_shm_language} "
            "compiler is ${CMAKE_${_shm_language}_COMPILER_ID} "
            "${CMAKE_${_shm_language}_COMPILER_VERSION} "
            "(${CMAKE_${_shm_language}_COMPILER}). Use a preset, e.g. "
            "'cmake --preset debug', or pass -DCMAKE_C_COMPILER=clang "
            "-DCMAKE_CXX_COMPILER=clang++.")
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SHMSCOPE_IPO_SUPPORTED OUTPUT SHMSCOPE_IPO_ERROR)
    if(SHMSCOPE_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO unavailable: ${SHMSCOPE_IPO_ERROR}")
    endif()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers.cmake)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/shmscope/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/shmscope/version.hpp
    @ONLY
)

file(GLOB_RECURSE SHMSCOPE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SHMSCOPE_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)

add_library(shmscope_core STATIC ${SHMSCOPE_SOURCES} ${SHMSCOPE_HEADERS})

target_include_directories(shmscope_core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

find_package(ftxui CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

if(SHMSCOPE_USE_CONAN)
    find_package(highway CONFIG REQUIRED)
else()
    find_package(hwy CONFIG REQUIRED)
    add_library(highway::highway ALIAS hwy::hwy)
endif()

target_link_libraries(shmscope_core PRIVATE
        ftxui::screen
        ftxui::dom
        ftxui::component
        highway::highway
        shmscope::warnings
        shmscope::sanitizers
        yaml-cpp::yaml-cpp
        nlohmann_json::nlohmann_json
)

target_include_directories(shmscope_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(shmscope main.cpp)
target_link_libraries(shmscope PRIVATE
        shmscope_core
        ftxui::component
        shmscope::warnings
        shmscope::sanitizers
        CLI11::CLI11
)

include(GNUInstallDirs)
install(TARGETS shmscope RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(SHMSCOPE_BUILD_TESTS_DEFAULT ON)
else()
    set(SHMSCOPE_BUILD_TESTS_DEFAULT OFF)
endif()

option(SHMSCOPE_BUILD_TESTS "Build shmscope tests" ${SHMSCOPE_BUILD_TESTS_DEFAULT})

option(SHMSCOPE_BUILD_EXAMPLES "Build the example writers" ${SHMSCOPE_BUILD_TESTS_DEFAULT})
if(SHMSCOPE_BUILD_EXAMPLES)
    add_executable(ring_writer examples/ring_writer.cpp)
    target_link_libraries(ring_writer PRIVATE shmscope::warnings shmscope::sanitizers)
endif()

option(SHMSCOPE_BUILD_BENCHMARKS "Build shmscope benchmarks" ${SHMSCOPE_BUILD_TESTS_DEFAULT})
if(SHMSCOPE_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

if(SHMSCOPE_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    if(TEST_SOURCES)
        add_executable(shmscope_tests ${TEST_SOURCES})
        target_link_libraries(shmscope_tests PRIVATE shmscope_core ftxui::component highway::highway shmscope::warnings shmscope::sanitizers GTest::gtest_main)

        target_include_directories(shmscope_tests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/tests"
                "${CMAKE_CURRENT_SOURCE_DIR}/examples")

        include(GoogleTest)
        gtest_discover_tests(shmscope_tests)
    endif()
endif()
