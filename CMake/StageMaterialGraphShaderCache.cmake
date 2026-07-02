if(NOT DEFINED KB_GRAPH_SHADER_CACHE_SOURCE)
    message(FATAL_ERROR "KB_GRAPH_SHADER_CACHE_SOURCE is required")
endif()

if(NOT DEFINED KB_GRAPH_SHADER_CACHE_DEST)
    message(FATAL_ERROR "KB_GRAPH_SHADER_CACHE_DEST is required")
endif()

file(MAKE_DIRECTORY "${KB_GRAPH_SHADER_CACHE_DEST}")

set(_kb_graph_cache_status "source_absent")
if(EXISTS "${KB_GRAPH_SHADER_CACHE_SOURCE}")
    file(COPY "${KB_GRAPH_SHADER_CACHE_SOURCE}/" DESTINATION "${KB_GRAPH_SHADER_CACHE_DEST}")
    set(_kb_graph_cache_status "copied")
endif()

file(WRITE
    "${KB_GRAPH_SHADER_CACHE_DEST}/.kb_graph_shader_cache_manifest"
    "source=${KB_GRAPH_SHADER_CACHE_SOURCE}\n"
    "destination=${KB_GRAPH_SHADER_CACHE_DEST}\n"
    "status=${_kb_graph_cache_status}\n"
)
