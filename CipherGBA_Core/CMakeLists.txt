# Created by Jacob Hodgkins.
# Independent CipherGBA emulator core; works as a standalone CMake project.
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CipherGBA_Core LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
find_package(ZLIB REQUIRED)
# v0.1 release: freeze the default bus/timing configuration.
# Historical experimental A/B build switches are not public release options.
set(CIPHERGBA_EMULATOR_SOURCES
    src/Bios.cpp src/Cartridge.cpp src/Subsystems.cpp src/Cpu.cpp src/Ppu.cpp
    src/Core.cpp src/HleHostAdapter.cpp src/HleBios.cpp src/StateFiles.cpp
    src/ciphergba_core.cpp)
# Compile all CPU/PPU/APU code exactly once; archives do not duplicate backend logic.
add_library(ciphergba_emulator_objects OBJECT ${CIPHERGBA_EMULATOR_SOURCES})
set_target_properties(ciphergba_emulator_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ciphergba_emulator_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(ciphergba_emulator_objects PUBLIC cxx_std_17)
# Source releases default to portable code generation. Locally compiled Ubuntu
# builds may explicitly tune this hot interpreter to the *building* CPU.
# No GBA-visible timing, ISA behavior or hardware state depends on this flag.
option(CIPHERGBA_NATIVE_TUNING "Tune interpreter for this host CPU; not portable binaries" OFF)
if(CIPHERGBA_NATIVE_TUNING)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" CIPHERGBA_NATIVE_ARCH_FLAG)
    if(CIPHERGBA_NATIVE_ARCH_FLAG)
        target_compile_options(ciphergba_emulator_objects PRIVATE
            $<$<CONFIG:Release>:-march=native>
            $<$<CONFIG:RelWithDebInfo>:-march=native>)
        message(STATUS "CipherGBA: native CPU tuning enabled; rebuild on target machine")
    else()
        message(WARNING "CipherGBA: compiler lacks -march=native; using portable code")
    endif()
endif()

# Rewind uses liblz4.so.1 when present at runtime; existing retained-zlib
# fallback remains active on systems without liblz4. No new hard dependency.
target_compile_definitions(ciphergba_emulator_objects PRIVATE PIXELGBA_FAST_REWIND=1)
# Native builds link the core at process startup; use cheaper local TLS for
# APU derived mix memo. Portable CMake builds keep dlopen-safe dynamic TLS.
if(CIPHERGBA_NATIVE_TUNING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(ciphergba_emulator_objects PRIVATE CIPHERGBA_APU_FAST_TLS=1)
endif()

target_compile_definitions(ciphergba_emulator_objects PRIVATE
    CIPHERGBA_FAST_SEQUENTIAL_REFILL=1
    CIPHERGBA_PHASED_CPU_DATA_BUS=1
    CIPHERGBA_TIMER_ENABLE_FETCH_OVERLAP=1
    CIPHERGBA_PHYSICAL_ROM_BRANCH_REFILL=1
    CIPHERGBA_PREFETCH_PIPELINE_LOOKAHEAD=1
    CIPHERGBA_PHYSICAL_ROM_DATA_RETURN=1
    CIPHERGBA_DEFERRED_DMA_FIFO_CLAIM=1
    CIPHERGBA_ARM_ROM_READ_LATCH_PHASE=1
    CIPHERGBA_FIFO_DELIVERY_PREFETCH=1
    CIPHERGBA_PREFETCH_ACTIVE_DATA_CLAIM=1
    CIPHERGBA_ROM_INTERWORKING_REFILL=1
    CIPHERGBA_INTERNAL_ROM_PIPELINE=1)
if(MSVC)
    target_compile_options(ciphergba_emulator_objects PRIVATE /W4)
else()
    target_compile_options(ciphergba_emulator_objects PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Export the C ABI when building the Windows DLL. Static consumers must set
# CIPHERGBA_CORE_STATIC when directly including ciphergba_core.h.
if(WIN32)
    target_compile_definitions(ciphergba_emulator_objects PRIVATE CIPHERGBA_CORE_BUILD=1)
endif()
# These core C++ implementation methods are never intentionally overridden
# by ELF symbol interposition. Allow GCC to optimize calls between object
# files in the shared core without altering CPU/GBA timing or the public C ABI.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ciphergba_emulator_objects PRIVATE
        $<$<CONFIG:Release>:-fno-semantic-interposition>
        $<$<CONFIG:RelWithDebInfo>:-fno-semantic-interposition>)
    # Generic, dlopen-safe builds use dynamic TLS for the APU mix memo.
    # Direct GOT calls avoid repeated PLT trampolines for external helpers;
    # native builds already use initial-exec TLS and showed no benefit from
    # this flag in controlled benchmarks, so retain their validated lowering.
    # This affects host code generation only, never emulated GBA timing.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CIPHERGBA_NATIVE_TUNING)
        target_compile_options(ciphergba_emulator_objects PRIVATE
            $<$<CONFIG:Release>:-fno-plt>
            $<$<CONFIG:RelWithDebInfo>:-fno-plt>)
    endif()
endif()
add_library(CipherGBA_Core SHARED $<TARGET_OBJECTS:ciphergba_emulator_objects>)
add_library(CipherGBA_Core_Static STATIC $<TARGET_OBJECTS:ciphergba_emulator_objects>)
set_target_properties(CipherGBA_Core PROPERTIES
    OUTPUT_NAME CipherGBA_Core VERSION 1.0.0 SOVERSION 1
    BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
set_target_properties(CipherGBA_Core_Static PROPERTIES OUTPUT_NAME CipherGBA_Core)
if(WIN32)
    target_compile_definitions(CipherGBA_Core_Static INTERFACE CIPHERGBA_CORE_STATIC=1)
endif()
foreach(target CipherGBA_Core CipherGBA_Core_Static)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_link_libraries(${target} PUBLIC ZLIB::ZLIB ${CMAKE_DL_LIBS})
endforeach()
add_library(CipherGBA::Core ALIAS CipherGBA_Core)
add_library(CipherGBA::CoreStatic ALIAS CipherGBA_Core_Static)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CIPHERGBA_CORE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib")
else()
    set(CIPHERGBA_CORE_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin")
endif()
set_target_properties(CipherGBA_Core CipherGBA_Core_Static PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CIPHERGBA_CORE_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CIPHERGBA_CORE_OUTPUT_DIR}")
include(GNUInstallDirs)
install(TARGETS CipherGBA_Core CipherGBA_Core_Static
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
