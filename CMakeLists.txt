cmake_minimum_required(VERSION 3.21)
project(LibreDrop VERSION 0.1.0 LANGUAGES CXX)

# LibreDrop: contact angle and drop shape analysis for the Ossila goniometer (and any backlit side view).
# Layout:  src/core  analysis library (Qt Core + Gui for images, Eigen)
#          src/cli   command line          src/gui  Qt Widgets program
#          tests     QtTest unit tests (synthetic drops of known angle, real frames)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui)
find_package(Eigen3 REQUIRED NO_MODULE)

add_compile_definitions(LIBREDROP_VERSION="${PROJECT_VERSION}")
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
else()
  add_compile_options(-Wall -Wextra)
endif()

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(libredrop_core STATIC ${CORE_SOURCES})
target_include_directories(libredrop_core PUBLIC src)
target_link_libraries(libredrop_core PUBLIC Qt6::Core Qt6::Gui Eigen3::Eigen)

add_executable(libredrop-cli src/cli/main.cpp)
target_link_libraries(libredrop-cli PRIVATE libredrop_core)
set_target_properties(libredrop-cli PROPERTIES OUTPUT_NAME LibreDropCLI)

option(LIBREDROP_GUI "Build the graphical program" ON)
if(LIBREDROP_GUI)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
  file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.h)
  qt_add_executable(libredrop-gui WIN32 MACOSX_BUNDLE ${GUI_SOURCES})
  qt_add_resources(libredrop-gui icon PREFIX "/" BASE resources FILES resources/icon.png)
  target_link_libraries(libredrop-gui PRIVATE libredrop_core Qt6::Widgets Qt6::Concurrent)
  set_target_properties(libredrop-gui PROPERTIES OUTPUT_NAME LibreDrop)
endif()

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(src ${TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE libredrop_core Qt6::Test)
  target_compile_definitions(${name} PRIVATE TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME ${name} COMMAND ${name})
endforeach()
