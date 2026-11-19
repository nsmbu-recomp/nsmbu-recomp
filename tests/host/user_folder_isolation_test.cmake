set(user "${WORK}/user")
file(REMOVE_RECURSE "${WORK}" "${user}")
file(MAKE_DIRECTORY "${WORK}/home")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "HOME=${WORK}/home" "APPDATA=${WORK}/home/AppData"
                        "XDG_CONFIG_HOME=${WORK}/home/.config"
                        "${PROBE}" --user-dir "${user}"
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "user_folder_probe failed (${rc}): ${out}${err}")
endif()
file(GLOB_RECURSE leaked LIST_DIRECTORIES true "${WORK}/home/*")
if(leaked)
  message(FATAL_ERROR "written outside the user folder: ${leaked}")
endif()
if(NOT EXISTS "${user}/controls.json")
  message(FATAL_ERROR "controls.json was not saved in the user folder")
endif()
message(STATUS "user folder only: ${user}/controls.json")
file(REMOVE_RECURSE "${WORK}" "${user}")
