cmake_minimum_required(VERSION 3.21)

project(smalti
  VERSION 0.1.2
  DESCRIPTION "A C++20 GUI library for desktop applications"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(cmake/embed_files.cmake)

option(SMALTI_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(SMALTI_BUILD_TESTS "Build the tests and register them with CTest" ${PROJECT_IS_TOP_LEVEL})
option(SMALTI_BUILD_TOOLS "Build smalti-theme, the theme file checker" ${PROJECT_IS_TOP_LEVEL})
option(SMALTI_HEADLESS_ONLY "Build without the Win32 or X11 backend (windows draw into memory)" OFF)
option(SMALTI_SANITIZE "Build our targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SMALTI_INSTALL "Generate install rules and a smaltiConfig.cmake" ${PROJECT_IS_TOP_LEVEL})

set(smalti_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
smalti_embed_files("${smalti_generated}/inter_fonts.cpp"
  kInterRegular "${PROJECT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
  kInterMedium "${PROJECT_SOURCE_DIR}/fonts/Inter-Medium.ttf"
  kInterSemiBold "${PROJECT_SOURCE_DIR}/fonts/Inter-SemiBold.ttf")
smalti_embed_files("${smalti_generated}/builtin_themes.cpp"
  kThemeDark "${PROJECT_SOURCE_DIR}/themes/dark.jsonc"
  kThemeBlack "${PROJECT_SOURCE_DIR}/themes/black.jsonc"
  kThemeLight "${PROJECT_SOURCE_DIR}/themes/light.jsonc")

add_library(smalti STATIC
  src/core/app.cpp
  src/core/layout.cpp
  src/core/painter.cpp
  src/core/registry.cpp
  src/core/testing.cpp
  src/platform/text_input.cpp
  src/core/widget.cpp
  src/core/window.cpp
  src/platform/backend.cpp
  src/platform/headless.cpp
  src/render/canvas.cpp
  src/render/embedded_fonts.cpp
  src/render/font.cpp
  src/render/geometry.cpp
  src/render/raster.cpp
  src/render/text.cpp
  src/theme/jsonc.cpp
  src/theme/theme.cpp
  src/theme/tokens.cpp
  src/theme/widget_styles.cpp
  src/theme/widgets_section.cpp
  src/widgets/controls.cpp
  src/widgets/label.cpp
  src/widgets/range.cpp
  "${smalti_generated}/inter_fonts.cpp"
  "${smalti_generated}/builtin_themes.cpp")
add_library(smalti::smalti ALIAS smalti)
# Reading and watching theme files (live reload), through each system's own calls.
if(WIN32)
  target_sources(smalti PRIVATE src/platform/file_watch_win32.cpp src/platform/read_file_win32.cpp)
elseif(UNIX AND NOT APPLE)
  target_sources(smalti PRIVATE src/platform/file_watch_linux.cpp src/platform/read_file_posix.cpp)
else()
  message(FATAL_ERROR "smalti: no file watcher for this system yet (see docs/Porting-to-a-new-platform.md)")
endif()
target_compile_features(smalti PUBLIC cxx_std_20)
target_include_directories(smalti
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(smalti PUBLIC Threads::Threads)

if(SMALTI_HEADLESS_ONLY)
  target_compile_definitions(smalti PRIVATE SMALTI_HEADLESS_ONLY)
elseif(WIN32)
  target_sources(smalti PRIVATE src/platform/win32.cpp src/platform/win32_clipboard.cpp
                                src/platform/win32_gpu.cpp)
  target_compile_definitions(smalti PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  # Only system DLLs on Windows' protected KnownDLLs list are linked directly. Direct3D, DXGI
  # and DWM are loaded at run time from the system directory (see SECURITY.md).
  target_link_libraries(smalti PRIVATE user32 gdi32 advapi32 imm32)
elseif(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_sources(smalti PRIVATE src/platform/x11.cpp src/platform/x11_clipboard.cpp)
  target_link_libraries(smalti PRIVATE X11::X11 ${CMAKE_DL_LIBS})
endif()

# Strict warnings and the sanitizers apply to our own targets only, never to consumers.
function(smalti_strict target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    if(SMALTI_SANITIZE)
      target_compile_options(${target} PRIVATE /fsanitize=address)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(SMALTI_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined,float-cast-overflow)
    endif()
  endif()
endfunction()
smalti_strict(smalti)
# The library's objects are instrumented, so whatever links it needs the runtime too.
if(SMALTI_SANITIZE AND NOT MSVC)
  target_link_options(smalti INTERFACE -fsanitize=address,undefined,float-cast-overflow)
endif()
# The generated arrays are data, not code worth warning about.
set_source_files_properties("${smalti_generated}/inter_fonts.cpp" "${smalti_generated}/builtin_themes.cpp"
  PROPERTIES SKIP_LINTING ON)

if(SMALTI_BUILD_EXAMPLES)
  foreach(example hello gallery settings)
    add_executable(${example} WIN32 examples/${example}.cpp)
    target_link_libraries(${example} PRIVATE smalti::smalti)
    smalti_strict(${example})
    if(MSVC)
      # WIN32 programs start at WinMain; this lets them keep a portable main(). The gallery
      # starts at wmain instead, so a theme file's name arrives intact in any language.
      if(example STREQUAL "gallery")
        target_link_options(${example} PRIVATE /ENTRY:wmainCRTStartup)
      else()
        target_link_options(${example} PRIVATE /ENTRY:mainCRTStartup)
      endif()
    elseif(MINGW AND example STREQUAL "gallery")
      target_link_options(${example} PRIVATE -municode)
    endif()
  endforeach()
  # Renders the documentation screenshots without a screen (see tools/screenshots.py).
  add_executable(screenshots examples/screenshots.cpp)
  target_link_libraries(screenshots PRIVATE smalti::smalti)
  smalti_strict(screenshots)
endif()

if(SMALTI_BUILD_TOOLS)
  add_executable(smalti-theme tools/smalti_theme.cpp)
  target_link_libraries(smalti-theme PRIVATE smalti::smalti)
  smalti_strict(smalti-theme)
  if(WIN32)
    target_compile_definitions(smalti-theme PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MINGW)
      target_link_options(smalti-theme PRIVATE -municode)  # its entry point is wmain
    endif()
  endif()
endif()

if(SMALTI_BUILD_TESTS)
  enable_testing()
  add_executable(smalti_tests
    tests/main.cpp
    tests/test_font.cpp
    tests/test_jsonc.cpp
    tests/test_layout.cpp
    tests/test_memory.cpp
    tests/test_raster.cpp
    tests/test_review.cpp
    tests/test_review2.cpp
    tests/test_review3.cpp
    tests/test_review4.cpp
    tests/test_review5.cpp
    tests/test_review6.cpp
    tests/test_styles.cpp
    tests/test_ref.cpp
    tests/test_reload.cpp
    tests/test_text.cpp
    tests/test_text_input.cpp
    tests/test_clipboard.cpp
    tests/test_theme.cpp
    tests/test_timing.cpp
    tests/test_widgets.cpp)
  target_link_libraries(smalti_tests PRIVATE smalti::smalti)
  target_include_directories(smalti_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  smalti_strict(smalti_tests)
  add_test(NAME smalti_tests COMMAND smalti_tests)
  set_tests_properties(smalti_tests PROPERTIES TIMEOUT 300)
endif()

if(SMALTI_INSTALL)
  include(CMakePackageConfigHelpers)
  install(DIRECTORY include/smalti DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS smalti EXPORT smaltiTargets)
  if(SMALTI_BUILD_TOOLS)
    install(TARGETS smalti-theme)
  endif()
  install(EXPORT smaltiTargets NAMESPACE smalti:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/smalti)
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/smaltiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
  # A consumer needs what the installed library links: X11 only where that backend was built.
  set(smalti_config_deps "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\n")
  if(UNIX AND NOT APPLE AND NOT SMALTI_HEADLESS_ONLY)
    string(APPEND smalti_config_deps "find_dependency(X11)\n")
  endif()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/smaltiConfig.cmake"
    "${smalti_config_deps}include(\"\${CMAKE_CURRENT_LIST_DIR}/smaltiTargets.cmake\")\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/smaltiConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/smaltiConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/smalti)
  install(FILES LICENSE fonts/OFL.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/smalti)
endif()
