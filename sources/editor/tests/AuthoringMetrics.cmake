# Count the package harness as well as the editor scenario. File assembly is
# explicit external preparation, not an editor control or a hidden free action.
file(READ "${artifacts}/report.json" editorReport)
string(JSON editorMetrics GET "${editorReport}" metrics)
set(exportActions 0)
set(exportAssertions 0)
set(exportFailures 0)
set(exportWorkarounds 0)
set(exportSteps "")
set(exportResult "RUNNING")

function(write_authoring_metrics)
    foreach(metric actions assertions failures workarounds)
        string(JSON editorValue GET "${editorMetrics}" "${metric}")
        if(metric STREQUAL "actions")
            math(EXPR totalActions "${editorValue} + ${exportActions}")
        elseif(metric STREQUAL "assertions")
            math(EXPR totalAssertions "${editorValue} + ${exportAssertions}")
        elseif(metric STREQUAL "failures")
            math(EXPR totalFailures "${editorValue} + ${exportFailures}")
        else()
            math(EXPR totalWorkarounds "${editorValue} + ${exportWorkarounds}")
        endif()
    endforeach()
    file(WRITE "${package}/authoring-metrics.json"
        "{\"schema\":\"21kb.authoring-metrics/v1\",\"result\":\"${exportResult}\","
        "\"editor\":${editorMetrics},\"export\":{\"actions\":${exportActions},\"assertions\":${exportAssertions},"
        "\"failures\":${exportFailures},\"workarounds\":${exportWorkarounds},\"steps\":[${exportSteps}]},"
        "\"total\":{\"actions\":${totalActions},\"assertions\":${totalAssertions},\"failures\":${totalFailures},\"workarounds\":${totalWorkarounds}},"
        "\"basis\":\"Executed scenario operations and export commands, not physical clicks or a study with people. Player assembly is counted as external preparation.\"}\n")
endfunction()

macro(record_export_step operation kind succeeded workaround)
    if("${kind}" STREQUAL "action")
        math(EXPR exportActions "${exportActions} + 1")
    else()
        math(EXPR exportAssertions "${exportAssertions} + 1")
    endif()
    if("${succeeded}" STREQUAL "false")
        math(EXPR exportFailures "${exportFailures} + 1")
        set(exportResult "FAIL")
    endif()
    if("${workaround}" STREQUAL "true")
        math(EXPR exportWorkarounds "${exportWorkarounds} + 1")
    endif()
    if(NOT exportSteps STREQUAL "")
        string(APPEND exportSteps ",")
    endif()
    string(APPEND exportSteps "{\"operation\":\"${operation}\",\"kind\":\"${kind}\",\"succeeded\":${succeeded},\"workaround\":${workaround}}")
    write_authoring_metrics()
endmacro()

write_authoring_metrics()
