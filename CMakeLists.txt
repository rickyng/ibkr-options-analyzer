cmake_minimum_required(VERSION 3.26)
project(ibkr-options-analyzer
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Modern CLI tool for IBKR options analysis"
)

# C++20 required, C++23 features used where available
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# FetchContent for all dependencies
include(FetchContent)

# CLI11 - Command line parser
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
    GIT_SHALLOW TRUE
)

# fmt - Modern formatting library
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
    GIT_SHALLOW TRUE
)

# spdlog - Fast logging library
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    GIT_SHALLOW TRUE
)

# nlohmann_json - JSON parser
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

# pugixml - XML parser for Flex responses
FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)

# cpp-httplib - HTTP client
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE
)

# rapidcsv - CSV parser
FetchContent_Declare(
    rapidcsv
    GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
    GIT_TAG v8.83
    GIT_SHALLOW TRUE
)

# SQLiteCpp - SQLite wrapper
FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG 3.3.1
    GIT_SHALLOW TRUE
)

# date - Howard Hinnant's date library for timezone-aware date handling
FetchContent_Declare(
    date
    GIT_REPOSITORY https://github.com/HowardHinnant/date.git
    GIT_TAG v3.0.1
    GIT_SHALLOW TRUE
)

# Make all dependencies available
FetchContent_MakeAvailable(
    cli11
    fmt
    spdlog
    nlohmann_json
    pugixml
    httplib
    rapidcsv
    SQLiteCpp
    date
)

# Core static library: libibkr_core.a
add_library(ibkr_core STATIC
    src/core/config/config_manager.cpp
    src/core/config/config_manager.hpp
    src/core/flex/flex_downloader.cpp
    src/core/flex/flex_downloader.hpp
    src/core/parsers/option_symbol_parser.cpp
    src/core/parsers/option_symbol_parser.hpp
    src/core/parsers/csv_parser.cpp
    src/core/parsers/csv_parser.hpp
    src/core/db/database.cpp
    src/core/db/database.hpp
    src/core/db/schema.hpp
    src/core/analysis/strategy_detector.cpp
    src/core/analysis/strategy_detector.hpp
    src/core/analysis/risk_calculator.cpp
    src/core/analysis/risk_calculator.hpp
    src/core/report/report_generator.cpp
    src/core/report/report_generator.hpp
    src/core/report/csv_exporter.cpp
    src/core/report/csv_exporter.hpp
    src/core/utils/result.hpp
    src/core/utils/logger.cpp
    src/core/utils/logger.hpp
    src/core/utils/http_client.cpp
    src/core/utils/http_client.hpp
    src/core/utils/price_fetcher.cpp
    src/core/utils/price_fetcher.hpp
)

target_include_directories(ibkr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_link_libraries(ibkr_core PUBLIC
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    pugixml::pugixml
    httplib::httplib
    rapidcsv
    SQLiteCpp
    date::date
)

# CLI executable
add_executable(ibkr-options-analyzer
    src/cli/main.cpp
    src/cli/commands/download_command.cpp
    src/cli/commands/download_command.hpp
    src/cli/commands/import_command.cpp
    src/cli/commands/import_command.hpp
    src/cli/commands/manual_add_command.cpp
    src/cli/commands/manual_add_command.hpp
    src/cli/commands/analyze_command.cpp
    src/cli/commands/analyze_command.hpp
    src/cli/commands/report_command.cpp
    src/cli/commands/report_command.hpp
)

# Compiler warnings — target-only to avoid propagating -Werror to dependencies
if(MSVC)
    target_compile_options(ibkr-options-analyzer PRIVATE /W4 /WX)
else()
    target_compile_options(ibkr-options-analyzer PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-literal-operator)
endif()

target_include_directories(ibkr-options-analyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
)

target_link_libraries(ibkr-options-analyzer PRIVATE
    ibkr_core
    CLI11::CLI11
)

# Workaround: Apple Clang 21 consteval bug in fmt FMT_STRING
# Disables constexpr evaluation that triggers compiler errors
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_definitions(fmt PRIVATE FMT_CONSTEVAL=)
    target_compile_definitions(spdlog PRIVATE FMT_CONSTEVAL=)
endif()

# Installation
install(TARGETS ibkr-options-analyzer
    RUNTIME DESTINATION bin
)

# Testing (optional, can be expanded later)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
