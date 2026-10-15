# CMake build for desktop (Windows/Linux/macOS) and the web (Emscripten).
#
#   Desktop:  cmake -B build && cmake --build build --config Release
#   Web:      emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web
#             -> copied to server/web/, which the Go server serves at http://localhost:8080
#                cd server && go run .
#
# Visual Studio users can also simply open TheCatsOfHalloween.slnx.
cmake_minimum_required(VERSION 3.16)
project(TheCatsOfHalloween C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# ---------------------------------------------------------------------------
# raylib 5.5, compiled straight from the vendored sources
# ---------------------------------------------------------------------------
set(RL ${CMAKE_CURRENT_SOURCE_DIR}/vendor/raylib/src)
set(RAYLIB_SOURCES
    ${RL}/rcore.c ${RL}/rshapes.c ${RL}/rtextures.c ${RL}/rtext.c
    ${RL}/rmodels.c ${RL}/raudio.c ${RL}/utils.c)

if(EMSCRIPTEN)
    add_library(raylib STATIC ${RAYLIB_SOURCES})
    target_compile_definitions(raylib PUBLIC PLATFORM_WEB GRAPHICS_API_OPENGL_ES2)
else()
    add_library(raylib STATIC ${RAYLIB_SOURCES} ${RL}/rglfw.c)
    target_compile_definitions(raylib PUBLIC PLATFORM_DESKTOP GRAPHICS_API_OPENGL_33)
    target_include_directories(raylib PRIVATE ${RL}/external/glfw/include)
    if(WIN32)
        target_link_libraries(raylib PUBLIC winmm gdi32 opengl32)
        target_compile_definitions(raylib PRIVATE _CRT_SECURE_NO_WARNINGS)
    elseif(APPLE)
        target_compile_options(raylib PRIVATE -x objective-c)
        target_link_libraries(raylib PUBLIC "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework OpenGL")
    else()
        target_compile_definitions(raylib PRIVATE _GLFW_X11 _GNU_SOURCE)
        find_package(Threads REQUIRED)
        target_link_libraries(raylib PUBLIC m dl Threads::Threads)
    endif()
endif()
target_include_directories(raylib PUBLIC ${RL})
if(NOT MSVC)
    target_compile_options(raylib PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(TheCatsOfHalloween TheCatsOfHalloween.cpp ${GAME_SOURCES})
target_link_libraries(TheCatsOfHalloween PRIVATE raylib)

if(EMSCRIPTEN)
    set_target_properties(TheCatsOfHalloween PROPERTIES SUFFIX ".html")
    target_link_options(TheCatsOfHalloween PRIVATE
        -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -lwebsocket.js
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # drop the finished web build where the Go server serves it by default (server/web)
    set(WEB_OUT ${CMAKE_CURRENT_SOURCE_DIR}/server/web)
    add_custom_command(TARGET TheCatsOfHalloween POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_OUT}
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:TheCatsOfHalloween>/TheCatsOfHalloween.html
            $<TARGET_FILE_DIR:TheCatsOfHalloween>/TheCatsOfHalloween.js
            $<TARGET_FILE_DIR:TheCatsOfHalloween>/TheCatsOfHalloween.wasm
            ${WEB_OUT})
elseif(WIN32)
    target_link_libraries(TheCatsOfHalloween PRIVATE ws2_32)
    # no console window in release builds
    set_target_properties(TheCatsOfHalloween PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    if(MSVC)
        target_link_options(TheCatsOfHalloween PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
endif()
