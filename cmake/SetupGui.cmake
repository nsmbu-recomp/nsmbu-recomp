





include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(NOT TARGET SDL3::SDL3-static AND (APPLE OR NOT TARGET SDL3::SDL3))
  if(NOT APPLE AND NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG QUIET)
  endif()
  if(APPLE OR NOT TARGET SDL3::SDL3)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
      URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
    FetchContent_MakeAvailable(SDL3)
  endif()
endif()
if(TARGET SDL3::SDL3-static)
  set(NSMBU_SETUP_SDL SDL3::SDL3-static)
else()
  set(NSMBU_SETUP_SDL SDL3::SDL3)
endif()

add_executable(nsmbu-setup WIN32
  installer/gui/setup_gui.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(nsmbu-setup PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(nsmbu-setup PRIVATE imgui ${NSMBU_SETUP_SDL})
set_source_files_properties(${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  PROPERTIES COMPILE_OPTIONS "-w")
if(WIN32)

  target_sources(nsmbu-setup PRIVATE installer/gui/console_setup_win.cpp)
  nsmbu_windows_resources(nsmbu-setup "NSMBU setup and launcher" "NSMBU.exe" gui)
  if(NSMBU_STRIP_RELEASE)
    target_link_options(nsmbu-setup PRIVATE ${NSMBU_STRIP_RELEASE})
  endif()
endif()
if(APPLE)
  target_link_libraries(nsmbu-setup PRIVATE "-framework CoreGraphics")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

  set_target_properties(nsmbu-setup PROPERTIES INSTALL_RPATH "\$ORIGIN/sdk/runtime;\$ORIGIN" BUILD_WITH_INSTALL_RPATH TRUE)
endif()
