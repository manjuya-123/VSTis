cmake_minimum_required(VERSION 3.22)

project(VSTis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VSTIS_CORE_ONLY "Build JUCE-independent physical-model tests only" OFF)

include(CTest)

if (NOT VSTIS_CORE_ONLY)
    include(cmake/JUCE.cmake)
endif()

add_subdirectory(shared/physical)
add_subdirectory(instruments/FiddleModel)
