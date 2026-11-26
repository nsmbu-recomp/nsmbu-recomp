# Decomp build (NSMBU_DECOMP=ON).
# tools/decomp_build/gen.py writes the function list and the hook wrappers into the build directory.
# GEN_DIR must be the output of recomp.py --decomp-hooks with that list.
# The normal build does not compile vendor/decomp.

set(NSMBU_DECOMP_OUT "${CMAKE_BINARY_DIR}/decomp")

function(nsmbu_decomp_fetch dest url marker)
  if(NOT EXISTS "${marker}")
    message(STATUS "decomp build: cloning ${url}")
    execute_process(COMMAND git clone --depth 1 "${url}" "${dest}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "decomp build: git clone ${url} failed")
    endif()
  endif()
endfunction()

function(nsmbu_decomp_prepare)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(out "${NSMBU_DECOMP_OUT}")
  set(third "${CMAKE_SOURCE_DIR}/.tmp")
  file(MAKE_DIRECTORY "${third}/nw")
  nsmbu_decomp_fetch("${third}/sead" "https://github.com/aboood40091/sead.git"
    "${third}/sead/engine/library/include/heap/seadDisposer.h")
  nsmbu_decomp_fetch("${third}/nw/Lyt" "https://github.com/nw4f/Lyt.git" "${third}/nw/Lyt/include/nw/lyt/lyt_Types.h")
  nsmbu_decomp_fetch("${third}/nw/G3d" "https://github.com/nw4f/G3d.git" "${third}/nw/G3d/include/nw/g3d/res/g3d_ResFile.h")
  nsmbu_decomp_fetch("${third}/nw/Snd" "https://github.com/nw4f/Snd.git" "${third}/nw/Snd/include/nw/snd/snd_SoundHandle.h")
  nsmbu_decomp_fetch("${third}/nw/Sys" "https://github.com/nw4f/Sys.git" "${third}/nw/Sys/include/nw/ut/ut_Memory.h")
  nsmbu_decomp_fetch("${third}/nw/Eft" "https://github.com/nw4f/Eft.git" "${third}/nw/Eft/include/nw/eft/eft_EmitterSet.h")
  nsmbu_decomp_fetch("${third}/nw/FFL" "https://github.com/aboood40091/FFL.git" "${third}/nw/FFL/include/nn/ffl.h")
  set(drop_args)
  if(EXISTS "${CMAKE_SOURCE_DIR}/tools/decomp_build/drop.txt")
    set(drop_args --drop "${CMAKE_SOURCE_DIR}/tools/decomp_build/drop.txt")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/decomp_build/gen.py"
                          --out "${out}"
                          --root "${CMAKE_SOURCE_DIR}/vendor/decomp"
                          --hooks "${CMAKE_SOURCE_DIR}/recomp"
                          ${drop_args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE gen_out ERROR_VARIABLE gen_err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "decomp build: gen.py failed:\n${gen_out}\n${gen_err}")
  endif()
  message(STATUS "${gen_out}")
  set(list "${out}/decomp_hooks.txt")
  string(CONCAT fix "Translate the game code with the decomp function list:\n"
         "  python3 recomp/recomp.py GAME/code/red-pro2.rpx ${GEN_DIR} --decomp-hooks ${list}\n"
         "Keep the normal build's GEN_DIR unchanged. Use -DGEN_DIR=${CMAKE_SOURCE_DIR}/build/gen-decomp for this build.")
  if(NOT EXISTS "${GEN_DIR}/decomp_hooks.txt")
    message(FATAL_ERROR "NSMBU_DECOMP: ${GEN_DIR} is not decomp build code.\n${fix}")
  endif()
  file(SHA256 "${GEN_DIR}/decomp_hooks.txt" have)
  file(SHA256 "${list}" want)
  if(NOT have STREQUAL want)
    message(FATAL_ERROR "NSMBU_DECOMP: ${GEN_DIR} was made with another decomp function list.\n${fix}")
  endif()
  set(NSMBU_DECOMP_OUT "${out}" PARENT_SCOPE)
  set(NSMBU_DECOMP_THIRD "${third}" PARENT_SCOPE)
endfunction()

function(nsmbu_decomp_link target)
  file(GLOB wraps CONFIGURE_DEPENDS "${NSMBU_DECOMP_OUT}/wrap_*.cpp")
  target_sources(${target} PRIVATE ${wraps} "${NSMBU_DECOMP_OUT}/slots.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/decomp.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/sdk_bridge.cpp")
  set(third "${NSMBU_DECOMP_THIRD}")
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/decomp_build/include"
    "${CMAKE_SOURCE_DIR}/vendor/decomp/include"
    "${third}/sead/engine/library/include"
    "${third}/sead/packages/agl/include"
    "${third}/sead/packages/nw_ptcl/library/include"
    "${third}/nw/Lyt/include"
    "${third}/nw/G3d/include"
    "${third}/nw/Snd/include"
    "${third}/nw/Sys/include"
    "${third}/nw/Eft/include"
    "${third}/nw/FFL/include")
  set_source_files_properties(${wraps} PROPERTIES
    COMPILE_OPTIONS "-w;-Wno-register;-fdelayed-template-parsing;-include;${CMAKE_SOURCE_DIR}/tools/decomp_build/host_prefix.h;-ffp-contract=off;-fno-fast-math;-fno-strict-aliasing;-fwrapv")
  target_compile_definitions(${target} PRIVATE NSMBU_DECOMP=1)
  target_sources(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/missing_stub.cpp"
    "${CMAKE_SOURCE_DIR}/runtime/src/decomp/missing_stubs.cpp")
endfunction()
