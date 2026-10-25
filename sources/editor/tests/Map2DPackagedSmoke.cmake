# Exercise the editor's authored map in a separate packaged player, with no loose project assets.
execute_process(COMMAND "${EDITOR}"
    --selftest-scenario "${ENGINE_ROOT}/sources/editor/tests/Map2DAutomationScenario.json"
    --selftest-root "${TEST_ROOT}" --selftest-task SELFTEST-Map2D-Package
    WORKING_DIRECTORY "${ENGINE_ROOT}" RESULT_VARIABLE result TIMEOUT 120)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Map editor scenario failed: ${result}")
endif()
set(artifacts "${TEST_ROOT}/SELFTEST-Map2D-Package")
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef run)
set(package "${artifacts}/package-${run}")
file(MAKE_DIRECTORY "${package}")
execute_process(COMMAND "${COOKER}" --project "${artifacts}/workspace"
    --target Windows.x64 --output "${package}/Game.kbpack" --engine-root "${ENGINE_ROOT}"
    --runtime-modules-output "${package}/RuntimeModules"
    --cache "${TEST_ROOT}/Map2D-CookCache"
    WORKING_DIRECTORY "${ENGINE_ROOT}" RESULT_VARIABLE result TIMEOUT 120
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${package}/cook.log" "${output}\n${errors}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Map cook failed: ${result}\n${output}\n${errors}")
endif()
file(COPY "${PLAYER}" ${PROVIDERS} DESTINATION "${package}")
get_filename_component(playerName "${PLAYER}" NAME)
execute_process(COMMAND "${package}/${playerName}" --headless --frames=120
    WORKING_DIRECTORY "${package}" RESULT_VARIABLE result TIMEOUT 60
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${package}/launch.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR NOT output MATCHES "rendered=120" OR NOT output MATCHES "errors=0")
    message(FATAL_ERROR "Packaged map launch failed: ${result}\n${output}\n${errors}")
endif()
# Profiling and readback are development-only switches. Load the same cooked pack
# through the build player after proving the deployed executable's normal startup.
execute_process(COMMAND "${PLAYER}" "--project=${package}/Game.kbpack"
    --headless --frames=120 --renderer=d3d11 --profile-fixed-step
    "--profile-file=${package}/frames.csv"
    "--screenshot-file=${package}/game.png" --screenshot-frame=60
    WORKING_DIRECTORY "${package}" RESULT_VARIABLE result TIMEOUT 60
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${package}/player.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR NOT output MATCHES "rendered=120" OR NOT output MATCHES "errors=0")
    message(FATAL_ERROR "Packaged map failed: ${result}\n${output}\n${errors}")
endif()
file(SIZE "${package}/game.png" imageSize)
if(imageSize LESS 1024)
    message(FATAL_ERROR "Packaged map did not produce a game screenshot")
endif()
file(STRINGS "${package}/frames.csv" frames)
list(REMOVE_AT frames 0)
foreach(frame IN LISTS frames)
    string(REPLACE "," ";" columns "${frame}")
    list(GET columns 11 submitted)
    list(GET columns 12 dropped)
    list(GET columns 13 missing)
    if(submitted LESS 1 OR NOT dropped EQUAL 0 OR NOT missing EQUAL 0)
        message(FATAL_ERROR "Packaged map has an incomplete frame: ${frame}")
    endif()
endforeach()
message(STATUS "Map editor -> package -> player passed: ${package}")
