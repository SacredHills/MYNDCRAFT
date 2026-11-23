cmake_minimum_required(VERSION 3.21)
project(MyndCraft VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)
# libzip: pkg-config first (Linux/Homebrew; Debian's CMake package config is broken),
# then the CMake package (vcpkg on Windows).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBZIP QUIET IMPORTED_TARGET libzip)
endif()
if(TARGET PkgConfig::LIBZIP)
  set(MYND_ZIP_TARGET PkgConfig::LIBZIP)
else()
  find_package(libzip CONFIG REQUIRED)
  set(MYND_ZIP_TARGET libzip::zip)
endif()

if(MSVC)
  add_compile_options(/W3 /permissive- /utf-8 /Zc:__cplusplus)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Engine library: document model, drawing, compositing, project IO (no widgets) ----
add_library(myndcore STATIC
  src/Core/Settings.cpp
  src/Core/UndoCommands.cpp
  src/Core/ProjectIO.cpp
  src/Core/Autosave.cpp
  src/Scene/VectorShape.cpp
  src/Scene/Layer.cpp
  src/Scene/Document.cpp
  src/Drawing/BrushEngine.cpp
  src/Drawing/FloodFill.cpp
  src/Graphics/Compositor.cpp
)
target_include_directories(myndcore PUBLIC src)
target_link_libraries(myndcore PUBLIC Qt6::Core Qt6::Gui ${MYND_ZIP_TARGET})

# ---- Application UI ----
add_library(myndui STATIC
  src/UI/EditorSession.cpp
  src/UI/Tools.cpp
  src/UI/CanvasView.cpp
  src/UI/Panels.cpp
  src/UI/Icons.cpp
  src/UI/Workspaces.cpp
  src/UI/MainWindow.cpp
)
target_link_libraries(myndui PUBLIC myndcore Qt6::Widgets)

add_executable(myndcraft src/main.cpp)
target_link_libraries(myndcraft PRIVATE myndui)
if(WIN32)
  set_target_properties(myndcraft PROPERTIES WIN32_EXECUTABLE ON)  # GUI subsystem: no console window
elseif(APPLE)
  set_target_properties(myndcraft PROPERTIES MACOSX_BUNDLE ON)
endif()

# ---- Tests ----
enable_testing()
function(myndcraft_test name)
  if(NOT EXISTS ${CMAKE_SOURCE_DIR}/tests/${name}.cpp)
    return()
  endif()
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE myndui Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
myndcraft_test(test_document)
myndcraft_test(test_projectio)
myndcraft_test(test_drawing)
myndcraft_test(test_compositor)
myndcraft_test(test_ui_workflow)
