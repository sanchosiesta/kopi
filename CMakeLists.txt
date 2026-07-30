cmake_minimum_required(VERSION 3.21)
project(kopi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- SDL3 ---
find_package(SDL3 REQUIRED CONFIG)

# --- Dear ImGui ---
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(imgui)

# Dear ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    # SDL3 + SDL_Renderer backends
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

# --- Our sources ---
set(KOPI_SOURCES
    src/main.cpp
    src/kopi.cpp
)

# macOS-specific: global hotkey via Carbon
if(APPLE)
    list(APPEND KOPI_SOURCES src/hotkey_macos.mm)
    set(KOPI_EXTRA_LIBS "-framework Carbon -framework AppKit -framework CoreGraphics -framework ApplicationServices")
endif()

add_executable(kopi
    ${KOPI_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(kopi PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL3_INCLUDE_DIRS}
)

target_link_libraries(kopi PRIVATE SDL3::SDL3 ${KOPI_EXTRA_LIBS})

# macOS: hide from Dock (accessory app)
if(APPLE)
    set_target_properties(kopi PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.kopi.clipboard-manager"
        MACOSX_BUNDLE_BUNDLE_NAME "kopi"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

install(TARGETS kopi BUNDLE DESTINATION . RUNTIME DESTINATION bin)
