# Copyright (c) 2026 Sachu James. SPDX-License-Identifier: Apache-2.0.
#
# TickDots: deterministic market microstructure simulator.
# Day 01: build foundation only. No simulation subsystems are implemented yet.

cmake_minimum_required(VERSION 3.22)

project(tickdots
  VERSION 0.2.0
  DESCRIPTION "Deterministic market microstructure simulator"
  HOMEPAGE_URL "https://github.com/SachuJames/tickdots"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings are applied per-target to TickDots's own targets only, so that
# third-party dependency builds (e.g. GoogleTest via FetchContent) are not
# affected by our warning set.
add_library(tickdots_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tickdots_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
  )
endif()

# ---------------------------------------------------------------------------
# Sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer)
# ---------------------------------------------------------------------------
option(TICKDOTS_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan" OFF)
if(TICKDOTS_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(WARNING "Sanitizers require GCC or Clang; ignoring TICKDOTS_ENABLE_SANITIZERS")
  else()
    add_library(tickdots_sanitizers INTERFACE)
    target_compile_options(tickdots_sanitizers INTERFACE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(tickdots_sanitizers INTERFACE
      -fsanitize=address,undefined)
  endif()
endif()

# ---------------------------------------------------------------------------
# Version header. Single source of truth: the project() VERSION above.
# ---------------------------------------------------------------------------
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/tickdots/version.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/tickdots/version.hpp"
  @ONLY
)

add_library(tickdots_core INTERFACE)
target_include_directories(tickdots_core INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>"
)
target_link_libraries(tickdots_core INTERFACE tickdots_warnings)
if(TARGET tickdots_sanitizers)
  target_link_libraries(tickdots_core INTERFACE tickdots_sanitizers)
endif()

# ---------------------------------------------------------------------------
# Event model library (Day 02): canonical normalized event types.
# Public headers live in include/tickdots/event/; non-trivial function
# definitions live in src/event/.
# ---------------------------------------------------------------------------
add_library(tickdots_event STATIC src/event/event.cpp)
target_link_libraries(tickdots_event PUBLIC tickdots_core)

# ---------------------------------------------------------------------------
# Replay driver library (Day 03): deterministic event replay.
# Public headers live in include/tickdots/replay/; the implementation in
# src/replay/. Knows nothing about market logic.
# ---------------------------------------------------------------------------
add_library(tickdots_replay STATIC src/replay/replay.cpp src/replay/session_config.cpp)
target_link_libraries(tickdots_replay PUBLIC tickdots_core tickdots_event)

# ---------------------------------------------------------------------------
# Order book library (Day 03): MBO resting-order state.
# Public headers live in include/tickdots/book/; the implementation in
# src/book/. Implements the EventProcessor interface; performs no matching.
# ---------------------------------------------------------------------------
add_library(tickdots_book STATIC src/book/order_book.cpp)
target_link_libraries(tickdots_book PUBLIC tickdots_core tickdots_event tickdots_replay)

# ---------------------------------------------------------------------------
# Matching engine library (Day 04): deterministic price-time priority
# execution. Public headers live in include/tickdots/matching/; the
# implementation in src/matching/. Drives the OrderBook through its public
# mutation API; the replay driver dispatches to it as an EventProcessor.
# ---------------------------------------------------------------------------
add_library(tickdots_matching STATIC src/matching/matching_engine.cpp)
target_link_libraries(tickdots_matching PUBLIC tickdots_core tickdots_event tickdots_replay tickdots_book)

# ---------------------------------------------------------------------------
# Analytics library (Day 05): deterministic queue-position tracking and
# execution statistics. Public headers live in include/tickdots/analytics/;
# the implementation in src/analytics/. Passive observers only: they read
# the OrderBook and Fill records, never mutate state or perform matching.
# ---------------------------------------------------------------------------
add_library(tickdots_analytics STATIC
  src/analytics/queue_tracker.cpp
  src/analytics/execution_statistics.cpp
)
target_link_libraries(tickdots_analytics PUBLIC tickdots_core tickdots_event tickdots_replay tickdots_book tickdots_matching)

# ---------------------------------------------------------------------------
# Microstructure model library (SPEC.md 15): the per-fill simulated
# latency model and its engine observer. Observes fills only; never
# feeds back into matching.
# ---------------------------------------------------------------------------
add_library(tickdots_model STATIC src/model/latency_model.cpp)
target_link_libraries(tickdots_model PUBLIC tickdots_core tickdots_event tickdots_replay tickdots_matching)

# ---------------------------------------------------------------------------
# Market-data parser library (Day 06): deterministic CSV input boundary.
# Public headers live in include/tickdots/market_data/; the implementation
# in src/market_data/. Converts external CSV records into normalized
# Events with parser-assigned dense sequences. Knows nothing about the
# book, matching engine, or analytics.
# ---------------------------------------------------------------------------
add_library(tickdots_market_data STATIC
  src/market_data/csv_parser.cpp
  src/market_data/market_state.cpp
)
target_link_libraries(tickdots_market_data PUBLIC tickdots_core tickdots_event tickdots_book)

# ---------------------------------------------------------------------------
# Main executable: the CLI defined in SPEC.md section 13.
# ---------------------------------------------------------------------------
add_executable(tickdots src/main.cpp)
target_link_libraries(tickdots PRIVATE tickdots_core tickdots_event tickdots_market_data tickdots_replay tickdots_book tickdots_matching tickdots_model)

# Installation: the CLI executable only. There is no stable library
# installation contract yet, so headers and libraries are not installed.
include(GNUInstallDirs)
install(TARGETS tickdots RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------------------
# Benchmark executable: deterministic workloads and a dependency-free
# measurement harness (bench/). Links the production libraries plus
# the generation-version-1 event generator from test support (which
# internally uses the reference book to construct valid lifecycles;
# the benchmark's own checks do not rely on it). The bench is not
# installed and adds no dependency to the production libraries.
# ---------------------------------------------------------------------------
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE TICKDOTS_GIT_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TICKDOTS_GIT_COMMIT)
  set(TICKDOTS_GIT_COMMIT "unknown")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(TICKDOTS_BENCH_BUILD_TYPE "unspecified")
else()
  set(TICKDOTS_BENCH_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()
add_library(tickdots_bench_lib STATIC bench/harness.cpp bench/workloads.cpp tests/support/event_generator.cpp tests/support/reference_model.cpp)
target_include_directories(tickdots_bench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tickdots_bench_lib PUBLIC tickdots_core tickdots_event tickdots_replay tickdots_book tickdots_matching tickdots_market_data tickdots_model)
target_compile_definitions(tickdots_bench_lib PRIVATE
  TICKDOTS_BENCH_COMMIT="${TICKDOTS_GIT_COMMIT}"
  TICKDOTS_BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  TICKDOTS_BENCH_BUILD_TYPE="${TICKDOTS_BENCH_BUILD_TYPE}")
add_executable(tickdots_bench bench/main.cpp)
target_link_libraries(tickdots_bench PRIVATE tickdots_bench_lib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(TICKDOTS_BUILD_TESTS "Build TickDots unit tests" ON)
if(TICKDOTS_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
  )
  # Avoid overriding the parent project's compiler/linker settings on Windows.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  add_executable(tickdots_tests
    tests/test_smoke.cpp
    tests/test_primitives.cpp
    tests/test_side_and_type.cpp
    tests/test_event.cpp
    tests/test_event_ordering.cpp
    tests/test_event_validation.cpp
    tests/test_order_book.cpp
    tests/test_replay.cpp
    tests/test_replay_lenient.cpp
    tests/test_session_config.cpp
    tests/test_reproducibility.cpp
    tests/test_replay_book_integration.cpp
    tests/test_matching.cpp
    tests/test_matching_properties.cpp
    tests/test_replay_matching_integration.cpp
    tests/test_queue_tracker.cpp
    tests/test_execution_statistics.cpp
    tests/test_analytics_integration.cpp
    tests/test_csv_parser.cpp
    tests/test_market_data_integration.cpp
    tests/test_market_state.cpp
    tests/test_market_state_integration.cpp
    tests/test_reference_model.cpp
    tests/test_minimizer.cpp
    tests/test_parser_fuzz.cpp
    tests/test_parser_replay_integration.cpp
    tests/test_replay_adversarial.cpp
    tests/test_modify_adversarial.cpp
    tests/test_config_metadata.cpp
    tests/test_csv_stream.cpp
    tests/test_cli.cpp
    tests/test_session_scale.cpp
    tests/test_seeded_generation.cpp
    tests/test_latency_model.cpp
    tests/test_benchmark.cpp
    tests/test_price_boundaries.cpp
    tests/test_property_matching.cpp
    tests/test_property_analytics.cpp
    tests/test_property_replay.cpp
    tests/support/reference_model.cpp
    tests/support/event_generator.cpp
    tests/support/minimizer.cpp
    tests/support/csv_generator.cpp
  )
  target_include_directories(tickdots_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(tickdots_tests PRIVATE tickdots_core tickdots_event tickdots_replay tickdots_book tickdots_matching tickdots_analytics tickdots_market_data tickdots_model tickdots_bench_lib GTest::gtest_main)
  # Process-boundary tests (test_cli.cpp) launch the real executable.
  target_compile_definitions(tickdots_tests PRIVATE TICKDOTS_EXE_PATH="$<TARGET_FILE:tickdots>")
  target_compile_definitions(tickdots_tests PRIVATE TICKDOTS_EXAMPLE_CSV="${CMAKE_SOURCE_DIR}/examples/sample_session.csv")
  add_dependencies(tickdots_tests tickdots)

  include(GoogleTest)
  gtest_discover_tests(tickdots_tests)
endif()
