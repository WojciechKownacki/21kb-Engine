# Exercise the editor's authored map (or another scenario's scene) in a separate packaged player, with no loose project assets.
if(NOT DEFINED SCENARIO)
    set(SCENARIO "Map2DAutomationScenario.json")
endif()
if(NOT DEFINED TASK)
    set(TASK "SELFTEST-Map2D-Package")
endif()
set(editorTimeout 120)
if(DEFINED SCREENSHOT_FRAME)
    set(editorTimeout 240)
endif()
execute_process(COMMAND "${EDITOR}"
    --selftest-scenario "${ENGINE_ROOT}/sources/editor/tests/${SCENARIO}"
    --selftest-root "${TEST_ROOT}" --selftest-task ${TASK}
    WORKING_DIRECTORY "${ENGINE_ROOT}" RESULT_VARIABLE result TIMEOUT ${editorTimeout})
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Map editor scenario failed: ${result}")
endif()
set(artifacts "${TEST_ROOT}/${TASK}")
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
# Startup needs real rendering but no full-size image comparison. Bound its
# framebuffer cost on software adapters; the parity capture below keeps its authored size.
execute_process(COMMAND "${package}/${playerName}" --headless --frames=120 --width=320 --height=180
    WORKING_DIRECTORY "${package}" RESULT_VARIABLE result TIMEOUT 60
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${package}/launch.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR NOT output MATCHES "rendered=120" OR NOT output MATCHES "errors=0")
    message(FATAL_ERROR "Packaged map launch failed: ${result}\n${output}\n${errors}")
endif()
# Profiling and readback are development-only switches. Load the same cooked pack
# through the build player after proving the deployed executable's normal startup.
set(sizeArguments)
if(DEFINED WIDTH AND DEFINED HEIGHT)
    set(sizeArguments "--width=${WIDTH}" "--height=${HEIGHT}")
endif()
set(playerFrames 120)
set(screenshotFrame 60)
set(playerTimeout 60)
if(DEFINED SCREENSHOT_FRAME)
    set(screenshotFrame ${SCREENSHOT_FRAME})
    math(EXPR playerFrames "${SCREENSHOT_FRAME} + 60")
    set(playerTimeout 240)
endif()
execute_process(COMMAND "${PLAYER}" "--project=${package}/Game.kbpack"
    --headless --frames=${playerFrames} --renderer=d3d11 --profile-fixed-step
    "--profile-file=${package}/frames.csv"
    "--screenshot-file=${package}/game.png" --screenshot-frame=${screenshotFrame} ${sizeArguments}
    WORKING_DIRECTORY "${package}" RESULT_VARIABLE result TIMEOUT ${playerTimeout}
    OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(WRITE "${package}/player.log" "${output}\n${errors}")
if(NOT result EQUAL 0 OR NOT output MATCHES "rendered=${playerFrames}" OR NOT output MATCHES "errors=0")
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
# Image parity: the picture the editor's Play view drew (PARITY_CHECKPOINT) against the packaged player's, same size and scene.
if(DEFINED PARITY_CHECKPOINT)
    execute_process(COMMAND "${PYTHON}" -I "${ENGINE_ROOT}/scripts/compare_images.py"
        "${artifacts}/automation/screenshots/${PARITY_CHECKPOINT}.png" "${package}/game.png"
        --pixel-tolerance ${PARITY_PIXEL_TOLERANCE} --max-mean ${PARITY_MAX_MEAN} --max-differing ${PARITY_MAX_DIFFERING}
        ${PARITY_EXTRA}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    file(WRITE "${package}/parity.log" "${output}\n${errors}")
    message(STATUS "${output}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The packaged player's picture differs from the editor's: ${output}${errors}")
    endif()
endif()
message(STATUS "Map editor -> package -> player passed: ${package}")
