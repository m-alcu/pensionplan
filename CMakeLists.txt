cmake_minimum_required(VERSION 3.16)
project(pensionplan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 on this system is installed in /usr/local; its exported CMake config is
# broken, so locate it through pkg-config instead.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)

set(IMGUI_DIR src/vendor/imgui)
add_executable(pensionplan
    src/main.cpp
    src/pension.cpp
    src/pensioner.cpp
    src/ui.cpp
    src/tabs.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(pensionplan PRIVATE ${IMGUI_DIR})
target_link_libraries(pensionplan PRIVATE SDL3::SDL3)

# The engine runs against the worked example in examples/; no SDL or ImGui
# involved, so these run anywhere. Point PENSION_FIXTURE at a real .dat to check
# the engine against an actual INSS simulation as well.
enable_testing()
add_executable(pension_tests tests/pension_tests.cpp src/pension.cpp src/pensioner.cpp)
target_include_directories(pension_tests PRIVATE src)
add_test(NAME pension_tests COMMAND pension_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
