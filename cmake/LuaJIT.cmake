set(NSMBU_LUAJIT_ROOT "${CMAKE_SOURCE_DIR}/vendor/luajit")
set(NSMBU_LUAJIT_SRC "${NSMBU_LUAJIT_ROOT}/src")
set(NSMBU_LUAJIT_LIB "${CMAKE_CURRENT_BINARY_DIR}/libluajit.a")

if(NOT EXISTS "${NSMBU_LUAJIT_SRC}/lua.h")
  message(FATAL_ERROR "LuaJIT submodule missing at ${NSMBU_LUAJIT_ROOT} (run git submodule update --init)")
endif()
if(NSMBU_IOS)
  # LuaJIT builds in its source tree; keep the iOS objects away from the host build's
  file(COPY "${NSMBU_LUAJIT_ROOT}/" DESTINATION "${CMAKE_BINARY_DIR}/luajit" PATTERN ".git" EXCLUDE PATTERN "*.o" EXCLUDE PATTERN "*.a" EXCLUDE)
  set(NSMBU_LUAJIT_SRC "${CMAKE_BINARY_DIR}/luajit/src")
endif()

if(WIN32 AND MSVC)
  message(FATAL_ERROR "LuaJIT static build uses the POSIX Makefile; MSVC is not wired yet")
endif()

find_program(NSMBU_LUAJIT_MAKE NAMES gmake make REQUIRED)

set(NSMBU_LUAJIT_MAKE_ARGS
  BUILDMODE=static
  "XCFLAGS=-DLUAJIT_DISABLE_FFI"
  libluajit.a)

if(NSMBU_IOS)
  # cross build: host tools (minilua, buildvm) for the Mac, the library for iOS arm64; LuaJIT turns its JIT off on iOS
  find_program(NSMBU_LUAJIT_HOST_CC NAMES clang cc REQUIRED)
  set(NSMBU_LUAJIT_IOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  if(NOT NSMBU_LUAJIT_IOS_MIN)
    set(NSMBU_LUAJIT_IOS_MIN 17.0)
  endif()
  if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(NSMBU_LUAJIT_TARGET_FLAGS "--target=arm64-apple-ios${NSMBU_LUAJIT_IOS_MIN}-simulator -isysroot ${CMAKE_OSX_SYSROOT}")
  else()
    set(NSMBU_LUAJIT_TARGET_FLAGS "--target=arm64-apple-ios${NSMBU_LUAJIT_IOS_MIN} -isysroot ${CMAKE_OSX_SYSROOT}")
  endif()
  set(NSMBU_LUAJIT_CC_ARG "CC=${CMAKE_C_COMPILER}")
  list(APPEND NSMBU_LUAJIT_MAKE_ARGS
    "HOST_CC=${NSMBU_LUAJIT_HOST_CC}"
    "${NSMBU_LUAJIT_CC_ARG}"
    "CROSS="
    "TARGET_FLAGS=${NSMBU_LUAJIT_TARGET_FLAGS}"
    "TARGET_AR=${CMAKE_AR} rcus"
    TARGET_SYS=iOS)
elseif(ANDROID)
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
  set(NSMBU_LUAJIT_CC_ARG "CC=${CMAKE_C_COMPILER} ${NSMBU_LUAJIT_TARGET_FLAGS}")
  list(APPEND NSMBU_LUAJIT_MAKE_ARGS
    "HOST_CC=${NSMBU_LUAJIT_HOST_CC}"
    "${NSMBU_LUAJIT_CC_ARG}"
    "TARGET_FLAGS=${NSMBU_LUAJIT_TARGET_FLAGS}"
    "TARGET_AR=${CMAKE_AR} rcus"
    TARGET_SYS=Linux)
else()
  set(NSMBU_LUAJIT_CC_ARG "CC=${CMAKE_C_COMPILER}")
  list(APPEND NSMBU_LUAJIT_MAKE_ARGS "${NSMBU_LUAJIT_CC_ARG}")
endif()

set(NSMBU_LUAJIT_CLEAN_ARGS "${NSMBU_LUAJIT_CC_ARG}")
if(NSMBU_IOS)
  # the Makefile asks the compiler about the target even to clean: give it the iOS SDK too
  list(APPEND NSMBU_LUAJIT_CLEAN_ARGS "CROSS=" "TARGET_FLAGS=${NSMBU_LUAJIT_TARGET_FLAGS}" TARGET_SYS=iOS)
elseif(APPLE)
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(NSMBU_LUAJIT_OSX_TARGET "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    list(APPEND NSMBU_LUAJIT_MAKE_ARGS "${NSMBU_LUAJIT_OSX_TARGET}")
    list(APPEND NSMBU_LUAJIT_CLEAN_ARGS "${NSMBU_LUAJIT_OSX_TARGET}")
  endif()
endif()

add_custom_command(
  OUTPUT "${NSMBU_LUAJIT_LIB}"
  COMMAND "${NSMBU_LUAJIT_MAKE}" -C "${NSMBU_LUAJIT_SRC}" ${NSMBU_LUAJIT_CLEAN_ARGS} clean
  COMMAND "${NSMBU_LUAJIT_MAKE}" -C "${NSMBU_LUAJIT_SRC}" ${NSMBU_LUAJIT_MAKE_ARGS}
  COMMAND ${CMAKE_COMMAND} -E copy "${NSMBU_LUAJIT_SRC}/libluajit.a" "${NSMBU_LUAJIT_LIB}"
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

message(STATUS "LuaJIT: vendored static (${NSMBU_LUAJIT_ROOT} -> ${NSMBU_LUAJIT_LIB}, BUILDMODE=static, LUAJIT_DISABLE_FFI)")
