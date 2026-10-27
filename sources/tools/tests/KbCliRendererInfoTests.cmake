if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "CLI executable is missing")
endif()

function(info code_var)
    execute_process(COMMAND "${CLI}" renderer-info ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 120)
    set(${code_var} "${code}" PARENT_SCOPE)
    set(info_output "${output}" PARENT_SCOPE)
    set(info_error "${error}" PARENT_SCOPE)
endfunction()

# An unknown backend is a usage error with a message, not a crash or a silent fallback.
info(code --backend metal)
if(NOT code EQUAL 2 OR NOT info_error MATCHES "--backend expects")
    message(FATAL_ERROR "an unsupported backend must be refused with exit 2 (got ${code}): ${info_error}")
endif()
info(code --unknown value)
if(NOT code EQUAL 2)
    message(FATAL_ERROR "an unknown option must be refused with exit 2 (got ${code})")
endif()

# The default selection and Direct3D 11 start a hidden host and describe it.
foreach(request auto d3d11)
    info(code --backend ${request})
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "renderer-info --backend ${request} failed (${code}): ${info_output} ${info_error}")
    endif()
    string(JSON schema GET "${info_output}" schema)
    string(JSON selected GET "${info_output}" selected)
    string(JSON compute GET "${info_output}" features compute)
    string(JSON fell_back GET "${info_output}" fell_back)
    string(JSON reasons LENGTH "${info_output}" fallback_reasons)
    if(NOT schema STREQUAL "21kb.renderer-info/v1" OR NOT selected STREQUAL "Direct3D 11" OR NOT compute OR fell_back)
        message(FATAL_ERROR "renderer-info --backend ${request} reported a wrong host: ${info_output}")
    endif()
    # The capability report states what the renderer does not do instead of leaving it out.
    string(JSON indirect GET "${info_output}" features gpu_driven_indirect_submit)
    if(indirect OR reasons LESS 1 OR NOT info_output MATCHES "indirect submit is not implemented")
        message(FATAL_ERROR "renderer-info must list the missing indirect submit: ${info_output}")
    endif()
endforeach()

# Other backends depend on the machine. The answer must be consistent: either the requested backend was
# selected, or the command fails and says which backend it got.
foreach(request d3d12 vulkan opengl)
    info(code --backend ${request})
    if(code EQUAL 0)
        string(JSON selected GET "${info_output}" selected)
        string(JSON explicit_request GET "${info_output}" explicit_request)
        if(NOT explicit_request OR (request STREQUAL "d3d12" AND NOT selected STREQUAL "Direct3D 12") OR
           (request STREQUAL "vulkan" AND NOT selected STREQUAL "Vulkan") OR
           (request STREQUAL "opengl" AND NOT selected MATCHES "^OpenGL"))
            message(FATAL_ERROR "renderer-info --backend ${request} selected ${selected}: ${info_output}")
        endif()
    else()
        if(NOT info_error MATCHES "backend '${request}'")
            message(FATAL_ERROR "renderer-info --backend ${request} failed without naming the backend: ${info_error}")
        endif()
        info(code --backend ${request} --allow-fallback)
        message(STATUS "backend ${request} is not available on this machine; --allow-fallback exit ${code}")
    endif()
endforeach()
message(STATUS "renderer-info checks passed")
