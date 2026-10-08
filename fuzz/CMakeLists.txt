# libFuzzer harnesses for the readers that take files a player, a mod or a
# downloaded project can hand the engine. Configure a dedicated tree with
#   -DKB_BUILD_FUZZERS=ON -DKB_ENABLE_SANITIZERS=ON -DKB_BUILD_EDITOR=OFF
# and build the kb_fuzzers target. Each harness replays its seed corpus as a test;
# run one for longer with e.g. `kb_fuzz_scene -max_total_time=300 <work corpus> fuzz/corpus/scene`.

add_custom_target(kb_fuzzers)

function(kb_add_fuzzer name)
    set(_kb_fuzzer kb_fuzz_${name})
    add_executable(${_kb_fuzzer} targets/${name}.cpp FuzzSupport.hpp)
    target_include_directories(${_kb_fuzzer} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/sources/engine/src/private
        ${CMAKE_SOURCE_DIR}/sources/renderer/src
    )
    target_link_libraries(${_kb_fuzzer} PRIVATE ${ARGN})
    kb_apply_quality_gates(${_kb_fuzzer})
    if(MSVC)
        target_compile_options(${_kb_fuzzer} PRIVATE /fsanitize=fuzzer)
        # A PDB, so AddressSanitizer reports name the functions and lines.
        target_link_options(${_kb_fuzzer} PRIVATE /DEBUG)
        target_link_libraries(${_kb_fuzzer} PRIVATE "${KB_MSVC_ASAN_RUNTIME_DIR}/clang_rt.fuzzer_MD-x86_64.lib")
    else()
        target_compile_options(${_kb_fuzzer} PRIVATE -fsanitize=fuzzer)
        target_link_options(${_kb_fuzzer} PRIVATE -fsanitize=fuzzer)
    endif()
    set_target_properties(${_kb_fuzzer} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
    add_dependencies(kb_fuzzers ${_kb_fuzzer})
    if(BUILD_TESTING)
        # Every committed seed and regression input must still pass.
        add_test(NAME ${_kb_fuzzer}_corpus
            COMMAND ${_kb_fuzzer} -runs=0 ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
        set_tests_properties(${_kb_fuzzer}_corpus PROPERTIES LABELS "fuzz" TIMEOUT 300)
    endif()
endfunction()

kb_add_fuzzer(scene kb_engine)
kb_add_fuzzer(scene_meta kb_engine)
kb_add_fuzzer(prefab kb_engine)
kb_add_fuzzer(save_game kb_engine)
kb_add_fuzzer(particle kb_engine)
kb_add_fuzzer(localization kb_engine)
kb_add_fuzzer(input kb_engine)
kb_add_fuzzer(project kb_engine)
kb_add_fuzzer(json kb_engine)
kb_add_fuzzer(asset_pack kb_runtime_asset_pack_validation kb_renderer kb_engine)
kb_add_fuzzer(baked_payload kb_renderer_bake kb_renderer kb_engine)
kb_add_fuzzer(mesh_gltf kb_renderer kb_engine)
kb_add_fuzzer(mesh_fbx kb_renderer kb_engine)
kb_add_fuzzer(mesh_obj kb_renderer kb_engine)
kb_add_fuzzer(fbx_ufbx kb_renderer kb_engine)
kb_add_fuzzer(image kb_renderer kb_engine)
kb_add_fuzzer(font kb_renderer kb_engine)
kb_add_fuzzer(material kb_renderer kb_engine)
