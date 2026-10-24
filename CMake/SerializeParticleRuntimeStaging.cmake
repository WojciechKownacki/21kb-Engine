include_guard(GLOBAL)

# Different consumers may stage the provider into the same output directory.
# Keep their existing paths and serialize the copy actions in the build graph.
function(kb_serialize_particle_runtime_staging stage_target)
    get_property(_kb_registered TARGET "${stage_target}" PROPERTY KB_PARTICLE_STAGING_REGISTERED)
    if(_kb_registered)
        return()
    endif()
    get_property(_kb_previous GLOBAL PROPERTY KB_PARTICLE_STAGING_PREVIOUS_TARGET)
    if(_kb_previous)
        add_dependencies("${stage_target}" "${_kb_previous}")
    endif()
    set_property(TARGET "${stage_target}" PROPERTY KB_PARTICLE_STAGING_REGISTERED TRUE)
    set_property(GLOBAL PROPERTY KB_PARTICLE_STAGING_PREVIOUS_TARGET "${stage_target}")
endfunction()
