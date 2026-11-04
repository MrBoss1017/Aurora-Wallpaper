cmake_minimum_required(VERSION 3.25)
project(AuroraWallpaper VERSION 0.1.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_executable(aurora_policy_tests tests/policy_tests.cpp)
target_include_directories(aurora_policy_tests PRIVATE src)
add_test(NAME policy COMMAND aurora_policy_tests)
if(MSVC)
    target_compile_options(aurora_policy_tests PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(aurora_policy_tests PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Keep deterministic scheduling/geometry tests buildable on any C++20 toolchain.
if(NOT WIN32)
    message(STATUS "Windows desktop target disabled; building portable policy tests only.")
    return()
endif()
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The desktop application requires the MSVC x64 toolchain.")
endif()
enable_language(RC)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()
set(WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/WebView2" CACHE PATH "Extracted WebView2 NuGet package")
set(VLC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vlc" CACHE PATH "libVLC 3.x x64 runtime directory")
if(NOT EXISTS "${WEBVIEW2_ROOT}/build/native/include/WebView2.h")
    message(FATAL_ERROR "Run tools/Setup-Dependencies.ps1 first (WebView2 SDK missing).")
endif()
if(NOT EXISTS "${VLC_ROOT}/libvlc.dll")
    message(FATAL_ERROR "Run tools/Setup-Dependencies.ps1 first (VLC runtime missing).")
endif()
qt_add_executable(AuroraWallpaper WIN32
    src/app/main.cpp src/app/Library.cpp src/app/Engine.cpp src/app/MainWindow.cpp
    src/platform/win32/DesktopHost.cpp src/platform/win32/DesktopEvents.cpp
    src/render/ShaderRenderer.cpp src/render/VideoRenderer.cpp src/render/WebRenderer.cpp
    resources/app.rc
)
target_include_directories(AuroraWallpaper PRIVATE src "${WEBVIEW2_ROOT}/build/native/include")
target_include_directories(AuroraWallpaper PRIVATE resources)
target_compile_definitions(AuroraWallpaper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(AuroraWallpaper PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /guard:cf)
target_link_options(AuroraWallpaper PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA
    "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
target_link_libraries(AuroraWallpaper PRIVATE Qt6::Widgets Qt6::Concurrent
    d3d11 dxgi d3dcompiler dcomp dwmapi user32 gdi32 ole32 oleaut32 shell32 shlwapi
    propsys wtsapi32 powrprof uuid version advapi32
    "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
add_custom_command(TARGET AuroraWallpaper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/samples" "$<TARGET_FILE_DIR:AuroraWallpaper>/samples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VLC_ROOT}" "$<TARGET_FILE_DIR:AuroraWallpaper>/vlc"
    VERBATIM)

install(TARGETS AuroraWallpaper RUNTIME DESTINATION bin)
install(DIRECTORY samples DESTINATION bin)
install(DIRECTORY "${VLC_ROOT}/" DESTINATION bin/vlc)
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(DIRECTORY docs DESTINATION .)
install(FILES installer/START-HERE.txt DESTINATION .)
# App-local MSVC runtime DLLs let the installed app run without developer tools
# or a separate administrator-level Visual C++ Redistributable installation.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
include(InstallRequiredSystemLibraries)
qt_generate_deploy_app_script(TARGET AuroraWallpaper OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "AuroraWallpaper-${PROJECT_VERSION}-windows-x64")
include(CPack)
