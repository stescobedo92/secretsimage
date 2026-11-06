cmake_minimum_required(VERSION 3.20)

# Single source of truth for the Semantic Version. The release workflow
# verifies that the pushed tag (vMAJOR.MINOR.PATCH) matches this value.
project(secretsimage
    VERSION 0.1.1
    DESCRIPTION "Automatic redaction of secrets and personal data in screenshots"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SECRETSIMAGE_BUILD_TESTS "Build the test suite" ON)
option(SECRETSIMAGE_BUILD_TOOLS "Build auxiliary tools (synthetic corpus generator)" ON)
option(SECRETSIMAGE_ENABLE_LTO "Enable link-time optimization in Release builds" ON)
option(SECRETSIMAGE_WERROR "Treat compiler warnings as errors" OFF)

include(GNUInstallDirs)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

if(SECRETSIMAGE_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "LTO not supported: ${ipo_error}")
    endif()
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/secretsimage/version.hpp"
    @ONLY)

# ---------------------------------------------------------------------------
# Vendored single-header image codecs (public domain / MIT, see file headers)
# ---------------------------------------------------------------------------
add_library(stb_image STATIC src/stb_impl.cpp)
target_include_directories(stb_image PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
set_target_properties(stb_image PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(secretsimage_core
    src/audit.cpp
    src/clipboard_watcher.cpp
    src/detector.cpp
    src/image.cpp
    src/ocr_tesseract_cli.cpp
    src/pipeline.cpp
    src/redactor.cpp
    src/sha256.cpp
)
add_library(secretsimage::core ALIAS secretsimage_core)
target_include_directories(secretsimage_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(secretsimage_core PUBLIC stb_image)
secretsimage_set_warnings(secretsimage_core)

# Platform clipboard / foreground-application backends
if(WIN32)
    target_sources(secretsimage_core PRIVATE src/platform/clipboard_win32.cpp)
    target_link_libraries(secretsimage_core PRIVATE user32 kernel32 gdi32)
    target_compile_definitions(secretsimage_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
elseif(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(src/platform/clipboard_macos.mm PROPERTIES LANGUAGE OBJCXX)
    target_sources(secretsimage_core PRIVATE src/platform/clipboard_macos.mm)
    target_link_libraries(secretsimage_core PRIVATE "-framework AppKit" "-framework Foundation")
elseif(UNIX)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_sources(secretsimage_core PRIVATE src/platform/clipboard_x11.cpp)
        target_link_libraries(secretsimage_core PRIVATE X11::X11)
        target_compile_definitions(secretsimage_core PRIVATE SECRETSIMAGE_HAVE_X11=1)
    else()
        message(STATUS "X11 not found: Linux clipboard backend will report unsupported at runtime")
        target_sources(secretsimage_core PRIVATE src/platform/clipboard_stub.cpp)
    endif()
else()
    target_sources(secretsimage_core PRIVATE src/platform/clipboard_stub.cpp)
endif()

# ---------------------------------------------------------------------------
# Command line application
# ---------------------------------------------------------------------------
add_executable(secretsimage apps/cli/main.cpp)
target_link_libraries(secretsimage PRIVATE secretsimage::core)
secretsimage_set_warnings(secretsimage)

# App icon and version metadata: on Windows both are embedded in the .exe
# via a generated .rc (icon + VERSIONINFO from the project version); the
# icon is installed as a hicolor icon + .desktop entry on Linux and as an
# .icns file on macOS. Artwork: assets/icon.svg (tools/assets/generate_icon.py).
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/secretsimage.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/secretsimage.rc"
        @ONLY)
    target_sources(secretsimage PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/secretsimage.rc")
endif()

# ---------------------------------------------------------------------------
# Synthetic corpus support + generator (reproducible tests PR-3 / PR-4)
# ---------------------------------------------------------------------------
if(SECRETSIMAGE_BUILD_TOOLS OR SECRETSIMAGE_BUILD_TESTS)
    add_library(synthetic_support STATIC tools/synthetic/synthetic.cpp tools/synthetic/render.cpp)
    target_compile_definitions(synthetic_support PRIVATE
        "SYNTHETIC_FONT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tools/synthetic/fonts\"")
    target_include_directories(synthetic_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tools/synthetic")
    target_link_libraries(synthetic_support PUBLIC secretsimage::core)
    secretsimage_set_warnings(synthetic_support)
endif()

if(SECRETSIMAGE_BUILD_TOOLS)
    add_executable(gen_synthetic apps/gen_synthetic/main.cpp)
    target_link_libraries(gen_synthetic PRIVATE synthetic_support)
    secretsimage_set_warnings(gen_synthetic)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SECRETSIMAGE_BUILD_TESTS)
    enable_testing()
    add_executable(secretsimage_tests
        tests/test_main.cpp
        tests/test_detector.cpp
        tests/test_image_redactor.cpp
        tests/test_pipeline.cpp
        tests/test_metrics.cpp
        tests/test_ocr_discovery.cpp
        tests/test_ocr_e2e.cpp
    )
    target_include_directories(secretsimage_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(secretsimage_tests PRIVATE secretsimage::core synthetic_support)
    secretsimage_set_warnings(secretsimage_tests)

    add_test(NAME unit COMMAND secretsimage_tests --exclude-tag ocr)
    set_tests_properties(unit PROPERTIES LABELS "unit" TIMEOUT 120)
    add_test(NAME ocr_e2e COMMAND secretsimage_tests --tag ocr)
    set_tests_properties(ocr_e2e PROPERTIES LABELS "ocr;requires-ocr" TIMEOUT 300)
endif()

# MinGW-built distribution executables statically link the GCC runtime so
# a foreign libstdc++/libgcc earlier on PATH (for example inside a
# Tesseract install directory) can never be picked up instead of the
# toolchain runtime. Debug is excluded: unoptimized MinGW objects emit a
# strong std::type_info symbol that collides with the static library.
if(MINGW)
    foreach(_exe secretsimage gen_synthetic secretsimage_tests)
        if(TARGET ${_exe})
            target_link_options(${_exe} PRIVATE
                $<$<NOT:$<CONFIG:Debug>>:-static-libgcc>
                $<$<NOT:$<CONFIG:Debug>>:-static-libstdc++>)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Install / packaging
# ---------------------------------------------------------------------------
install(TARGETS secretsimage RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(SECRETSIMAGE_BUILD_TOOLS)
    install(TARGETS gen_synthetic RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(FILES LICENSE README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/secretsimage")
install(FILES assets/icon.png assets/icon.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/secretsimage")
if(UNIX AND NOT APPLE)
    install(FILES assets/icon.png
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
            RENAME secretsimage.png)
    install(FILES assets/secretsimage.desktop
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
elseif(APPLE)
    install(FILES assets/secretsimage.icns DESTINATION "${CMAKE_INSTALL_DATADIR}/secretsimage")
endif()

set(CPACK_PACKAGE_NAME "secretsimage")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
include(CPack)
