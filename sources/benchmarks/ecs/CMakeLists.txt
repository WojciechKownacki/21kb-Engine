execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KB_ECS_BENCHMARK_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE KB_ECS_BENCHMARK_GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(KB_ECS_BENCHMARK_GIT_COMMIT STREQUAL "")
    set(KB_ECS_BENCHMARK_GIT_COMMIT "unknown")
endif()

if(KB_ECS_BENCHMARK_GIT_BRANCH STREQUAL "")
    set(KB_ECS_BENCHMARK_GIT_BRANCH "unknown")
endif()

option(KB_ECS_BENCHMARKS_ENABLE_LTO "Enable link-time optimization for ECS benchmark targets" OFF)
set(KB_ECS_BENCHMARKS_PGO_MODE "Off" CACHE STRING "ECS benchmark PGO mode: Off, Generate, or Use")
set_property(CACHE KB_ECS_BENCHMARKS_PGO_MODE PROPERTY STRINGS Off Generate Use)
set(KB_ECS_BENCHMARKS_PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/ecs-benchmarks-pgo" CACHE PATH "Profile data directory for ECS benchmark PGO builds")
set(KB_ECS_BENCHMARKS_PGO_TRAINING_ARGS "--smoke" CACHE STRING "Arguments passed to the ECS benchmark during PGO training")

set(_kb_ecs_benchmarks_pgo_mode "${KB_ECS_BENCHMARKS_PGO_MODE}")
if(NOT _kb_ecs_benchmarks_pgo_mode MATCHES "^(Off|Generate|Use)$")
    message(FATAL_ERROR "KB_ECS_BENCHMARKS_PGO_MODE must be Off, Generate, or Use")
endif()

set(_kb_ecs_benchmarks_profile_dir "${KB_ECS_BENCHMARKS_PGO_PROFILE_DIR}")
if(NOT IS_ABSOLUTE "${_kb_ecs_benchmarks_profile_dir}")
    cmake_path(ABSOLUTE_PATH _kb_ecs_benchmarks_profile_dir BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
endif()
file(MAKE_DIRECTORY "${_kb_ecs_benchmarks_profile_dir}")

add_executable(kb_ecs_benchmarks
    main.cpp
)

target_link_libraries(kb_ecs_benchmarks
    PRIVATE
        kb_engine
)

target_compile_definitions(kb_ecs_benchmarks
    PRIVATE
        "KB_ECS_BENCHMARK_GIT_COMMIT=\"${KB_ECS_BENCHMARK_GIT_COMMIT}\""
        "KB_ECS_BENCHMARK_GIT_BRANCH=\"${KB_ECS_BENCHMARK_GIT_BRANCH}\""
        "KB_ECS_BENCHMARK_BUILD_CONFIG=\"$<CONFIG>\""
)

kb_apply_quality_gates(kb_ecs_benchmarks)

add_library(kb_ecs_kernel_report_kernels OBJECT
    KernelReportKernels.cpp
    KernelReportKernels.hpp
)

target_include_directories(kb_ecs_kernel_report_kernels
    PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/engine/include
)

kb_apply_quality_gates(kb_ecs_kernel_report_kernels)

add_executable(kb_ecs_kernel_report
    KernelReportMain.cpp
    KernelReportKernels.hpp
    $<TARGET_OBJECTS:kb_ecs_kernel_report_kernels>
)

target_include_directories(kb_ecs_kernel_report
    PRIVATE
        ${CMAKE_SOURCE_DIR}/sources/engine/include
)

target_compile_definitions(kb_ecs_kernel_report
    PRIVATE
        "KB_ECS_KERNEL_REPORT_GIT_COMMIT=\"${KB_ECS_BENCHMARK_GIT_COMMIT}\""
        "KB_ECS_KERNEL_REPORT_GIT_BRANCH=\"${KB_ECS_BENCHMARK_GIT_BRANCH}\""
        "KB_ECS_KERNEL_REPORT_BUILD_CONFIG=\"$<CONFIG>\""
        "KB_ECS_KERNEL_REPORT_COMPILER_ID=\"${CMAKE_CXX_COMPILER_ID}\""
        "KB_ECS_KERNEL_REPORT_COMPILER_VERSION=\"${CMAKE_CXX_COMPILER_VERSION}\""
)

kb_apply_quality_gates(kb_ecs_kernel_report)

if(MSVC)
    target_compile_options(kb_ecs_kernel_report_kernels PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
    target_compile_options(kb_ecs_kernel_report PRIVATE "$<$<CONFIG:Debug,RelWithDebInfo>:/Z7>")
endif()

set(_kb_ecs_kernel_report_assembly_dir "${CMAKE_CURRENT_BINARY_DIR}/kernel-report/assembly")
set(_kb_ecs_kernel_report_source "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.cpp")

if(MSVC)
    set(_kb_ecs_kernel_report_assembly "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.asm")
    set(_kb_ecs_kernel_report_object "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.obj")
    add_custom_command(
        OUTPUT "${_kb_ecs_kernel_report_assembly}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_kernel_report_assembly_dir}"
        COMMAND ${CMAKE_CXX_COMPILER}
            /nologo
            /std:c++20
            /EHsc
            /O2
            /DNDEBUG
            "/I${CMAKE_SOURCE_DIR}/sources/engine/include"
            /c
            /FAcs
            "/Fa${_kb_ecs_kernel_report_assembly}"
            "/Fo${_kb_ecs_kernel_report_object}"
            "${_kb_ecs_kernel_report_source}"
        DEPENDS
            "${_kb_ecs_kernel_report_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.hpp"
        VERBATIM
    )
else()
    set(_kb_ecs_kernel_report_assembly "${_kb_ecs_kernel_report_assembly_dir}/KernelReportKernels.s")
    add_custom_command(
        OUTPUT "${_kb_ecs_kernel_report_assembly}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_kernel_report_assembly_dir}"
        COMMAND ${CMAKE_CXX_COMPILER}
            -std=c++20
            -O3
            -DNDEBUG
            "-I${CMAKE_SOURCE_DIR}/sources/engine/include"
            -S
            "${_kb_ecs_kernel_report_source}"
            -o
            "${_kb_ecs_kernel_report_assembly}"
        DEPENDS
            "${_kb_ecs_kernel_report_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/KernelReportKernels.hpp"
        VERBATIM
    )
endif()

add_custom_target(kb_ecs_kernel_report_assembly
    DEPENDS "${_kb_ecs_kernel_report_assembly}"
)

add_custom_target(kb_ecs_kernel_report_run
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/kernel-report"
    COMMAND "$<TARGET_FILE:kb_ecs_kernel_report>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/kernel-report/ecs_kernel_report.json"
        --assembly-dir "${_kb_ecs_kernel_report_assembly_dir}"
        --elements 32768
        --iterations 8
        --samples 5
        --warmup 1
    DEPENDS
        kb_ecs_kernel_report
        kb_ecs_kernel_report_assembly
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

if(BUILD_TESTING)
    add_test(
        NAME kb_ecs_benchmark_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
            --smoke
            --output "${CMAKE_CURRENT_BINARY_DIR}/ecs_benchmark_smoke.json"
    )
    set_tests_properties(kb_ecs_benchmark_smoke PROPERTIES
        LABELS "ecs;benchmark;smoke"
        TIMEOUT 300
    )

    add_test(
        NAME kb_ecs_kernel_report_smoke
        COMMAND "$<TARGET_FILE:kb_ecs_kernel_report>"
            --output "${CMAKE_CURRENT_BINARY_DIR}/kernel-report-smoke.json"
            --elements 1024
            --iterations 1
            --samples 1
            --warmup 1
    )
endif()

add_custom_target(kb_ecs_benchmarks_full
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/full"
    COMMAND "$<TARGET_FILE:kb_ecs_benchmarks>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/full/ecs_benchmark_full.json"
    DEPENDS kb_ecs_benchmarks
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)

function(kb_ecs_benchmarks_enable_ipo target_name)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _kb_ecs_benchmarks_ipo_supported OUTPUT _kb_ecs_benchmarks_ipo_error LANGUAGES CXX)
    if(NOT _kb_ecs_benchmarks_ipo_supported)
        message(FATAL_ERROR "IPO/LTO is not supported for ${target_name}: ${_kb_ecs_benchmarks_ipo_error}")
    endif()

    set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endfunction()

function(kb_ecs_benchmarks_apply_pgo target_name)
    if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Off")
        return()
    endif()

    get_target_property(_kb_ecs_benchmarks_target_type ${target_name} TYPE)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /GL)

        if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
            set(_kb_ecs_benchmarks_pgd "${_kb_ecs_benchmarks_profile_dir}/kb_ecs_benchmarks.pgd")
            if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
                target_link_options(${target_name} PRIVATE /LTCG "/GENPROFILE:PGD=${_kb_ecs_benchmarks_pgd}")
                get_filename_component(_kb_ecs_benchmarks_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set(_kb_ecs_benchmarks_pgort "${_kb_ecs_benchmarks_compiler_dir}/pgort140.dll")
                if(EXISTS "${_kb_ecs_benchmarks_pgort}")
                    add_custom_command(
                        TARGET ${target_name}
                        POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_kb_ecs_benchmarks_pgort}" "$<TARGET_FILE_DIR:${target_name}>/pgort140.dll"
                    )
                endif()
            elseif(_kb_ecs_benchmarks_pgo_mode STREQUAL "Use")
                target_link_options(${target_name} PRIVATE /LTCG "/USEPROFILE:PGD=${_kb_ecs_benchmarks_pgd}")
            endif()
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
            target_compile_options(${target_name} PRIVATE "-fprofile-generate=${_kb_ecs_benchmarks_profile_dir}")
            if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
                target_link_options(${target_name} PRIVATE "-fprofile-generate=${_kb_ecs_benchmarks_profile_dir}")
            endif()
        elseif(_kb_ecs_benchmarks_pgo_mode STREQUAL "Use")
            target_compile_options(${target_name} PRIVATE "-fprofile-use=${_kb_ecs_benchmarks_profile_dir}")
            if(_kb_ecs_benchmarks_target_type STREQUAL "EXECUTABLE" OR _kb_ecs_benchmarks_target_type STREQUAL "SHARED_LIBRARY" OR _kb_ecs_benchmarks_target_type STREQUAL "MODULE_LIBRARY")
                target_link_options(${target_name} PRIVATE "-fprofile-use=${_kb_ecs_benchmarks_profile_dir}")
            endif()
        endif()
    else()
        message(FATAL_ERROR "PGO for ECS benchmarks is not configured for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()

set(_kb_ecs_benchmarks_enable_ipo OFF)
if(KB_ECS_BENCHMARKS_ENABLE_LTO)
    set(_kb_ecs_benchmarks_enable_ipo ON)
endif()
if(MSVC AND NOT _kb_ecs_benchmarks_pgo_mode STREQUAL "Off")
    set(_kb_ecs_benchmarks_enable_ipo OFF)
endif()

foreach(_kb_ecs_benchmarks_optimized_target IN ITEMS kb_engine kb_ecs_benchmarks)
    if(TARGET ${_kb_ecs_benchmarks_optimized_target})
        if(_kb_ecs_benchmarks_enable_ipo)
            kb_ecs_benchmarks_enable_ipo(${_kb_ecs_benchmarks_optimized_target})
        endif()
        kb_ecs_benchmarks_apply_pgo(${_kb_ecs_benchmarks_optimized_target})
    endif()
endforeach()

if(_kb_ecs_benchmarks_pgo_mode STREQUAL "Generate")
    separate_arguments(_kb_ecs_benchmarks_pgo_training_args NATIVE_COMMAND "${KB_ECS_BENCHMARKS_PGO_TRAINING_ARGS}")
    add_custom_target(kb_ecs_benchmarks_pgo_train
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_kb_ecs_benchmarks_profile_dir}"
        COMMAND ${CMAKE_COMMAND} -E env "VCPROFILE_PATH=${_kb_ecs_benchmarks_profile_dir}" "$<TARGET_FILE:kb_ecs_benchmarks>" ${_kb_ecs_benchmarks_pgo_training_args} --output "${_kb_ecs_benchmarks_profile_dir}/ecs_benchmarks_pgo_training.json"
        DEPENDS kb_ecs_benchmarks
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        USES_TERMINAL
    )
endif()
