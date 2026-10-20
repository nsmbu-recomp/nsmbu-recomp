set(NSMBU_LUAJIT_ROOT "${CMAKE_SOURCE_DIR}/vendor/luajit")
set(NSMBU_LUAJIT_SRC "${NSMBU_LUAJIT_ROOT}/src")
set(NSMBU_LUAJIT_LIB "${NSMBU_LUAJIT_SRC}/libluajit.a")

if(NOT EXISTS "${NSMBU_LUAJIT_SRC}/lua.h")
  message(FATAL_ERROR "LuaJIT submodule missing at ${NSMBU_LUAJIT_ROOT} (run git submodule update --init)")
endif()

if(WIN32 AND MSVC)
  message(FATAL_ERROR "LuaJIT static build uses the POSIX Makefile; MSVC is not wired yet")
endif()

find_program(NSMBU_LUAJIT_MAKE NAMES gmake make REQUIRED)

set(NSMBU_LUAJIT_MAKE_ARGS
  BUILDMODE=static
  "XCFLAGS=-DLUAJIT_DISABLE_FFI"
  libluajit.a)

if(ANDROID)
  find_program(NSMBU_LUAJIT_HOST_CC NAMES cc gcc clang REQUIRED)
  set(NSMBU_LUAJIT_TARGET_FLAGS "")
  if(CMAKE_C_COMPILER_TARGET)
    list(APPEND NSMBU_LUAJIT_TARGET_FLAGS "--target=${CMAKE_C_COMPILER_TARGET}")
  endif()
  if(CMAKE_SYSROOT)
    list(APPEND NSMBU_LUAJIT_TARGET_FLAGS "--sysroot=${CMAKE_SYSROOT}")
  endif()
  list(APPEND NSMBU_LUAJIT_TARGET_FLAGS -fPIC)
  string(JOIN " " NSMBU_LUAJIT_TARGET_FLAGS ${NSMBU_LUAJIT_TARGET_FLAGS})
  list(APPEND NSMBU_LUAJIT_MAKE_ARGS
    "HOST_CC=${NSMBU_LUAJIT_HOST_CC}"
    "CC=${CMAKE_C_COMPILER} ${NSMBU_LUAJIT_TARGET_FLAGS}"
    "TARGET_FLAGS=${NSMBU_LUAJIT_TARGET_FLAGS}"
    "TARGET_AR=${CMAKE_AR} rcus"
    TARGET_SYS=Linux)
else()
  list(APPEND NSMBU_LUAJIT_MAKE_ARGS "CC=${CMAKE_C_COMPILER}")
endif()

if(APPLE)
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    list(APPEND NSMBU_LUAJIT_MAKE_ARGS "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endif()

add_custom_command(
  OUTPUT "${NSMBU_LUAJIT_LIB}"
  COMMAND "${NSMBU_LUAJIT_MAKE}" -C "${NSMBU_LUAJIT_SRC}" ${NSMBU_LUAJIT_MAKE_ARGS}
  DEPENDS
    "${NSMBU_LUAJIT_SRC}/Makefile"
    "${NSMBU_LUAJIT_SRC}/lua.h"
    "${NSMBU_LUAJIT_SRC}/luajit.c"
    "${NSMBU_LUAJIT_SRC}/ljamalg.c"
  COMMENT "Building vendored LuaJIT"
  VERBATIM)

add_custom_target(nsmbu_luajit_build DEPENDS "${NSMBU_LUAJIT_LIB}")

add_library(nsmbu_luajit STATIC IMPORTED GLOBAL)
add_dependencies(nsmbu_luajit nsmbu_luajit_build)
set_target_properties(nsmbu_luajit PROPERTIES
  IMPORTED_LOCATION "${NSMBU_LUAJIT_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${NSMBU_LUAJIT_SRC}")

if(UNIX)
  target_link_libraries(nsmbu_luajit INTERFACE m ${CMAKE_DL_LIBS})
endif()

message(STATUS "LuaJIT: vendored static (${NSMBU_LUAJIT_ROOT}, BUILDMODE=static, LUAJIT_DISABLE_FFI)")
