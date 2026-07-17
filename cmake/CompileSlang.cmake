# CompileSlang.cmake — build-time Slang shader codegen
# ============================================================================
# Turns a single `.slang` source into the three shading languages the engine
# actually consumes:
#
#   .slang ──slangc──▶ .spv ──spirv-cross──▶ <name>.<stage>.glsl   (GL 4.1)
#          │                 └──spirv-cross──▶ <name>.<stage>.es.glsl (GLSL ES 300)
#          └────────slangc──▶ <name>.wgsl                            (WebGPU)
#
# Rationale (option 1 in docs/slang-migration.md): Slang's *native* GLSL
# emitter is Vulkan-flavoured (descriptor sets, explicit bindings) and is not
# guaranteed to feed OpenGL 4.1 / WebGL2 unmodified. Going Slang → SPIR-V →
# SPIRV-Cross gives us a downlevel GLSL emitter purpose-built for legacy GL and
# ES targets, while WGSL is emitted by slangc directly.
#
# This module is OFF by default and never touches the existing hand-written
# GLSL/WGSL assets. Enable with -DAE_USE_SLANG=ON once slangc + spirv-cross are
# on PATH (vcpkg `atmospheric[slang]`, the Vulkan SDK, or a Slang release).
# ============================================================================

option(AE_USE_SLANG "Compile .slang shaders to GLSL/WGSL at build time" OFF)

if(AE_USE_SLANG)
    find_program(SLANGC_EXECUTABLE NAMES slangc
        DOC "Path to the slangc compiler")
    find_program(SPIRV_CROSS_EXECUTABLE NAMES spirv-cross
        DOC "Path to the SPIRV-Cross CLI")

    if(NOT SLANGC_EXECUTABLE)
        message(FATAL_ERROR
            "AE_USE_SLANG=ON but slangc was not found. Install the Slang SDK or "
            "vcpkg `shader-slang`, or pass -DSLANGC_EXECUTABLE=/path/to/slangc.")
    endif()
    if(NOT SPIRV_CROSS_EXECUTABLE)
        message(FATAL_ERROR
            "AE_USE_SLANG=ON but spirv-cross was not found. Install via "
            "vcpkg `atmospheric[slang]`, or pass -DSPIRV_CROSS_EXECUTABLE=/path.")
    endif()
    message(STATUS "Slang codegen enabled: ${SLANGC_EXECUTABLE}")
    message(STATUS "SPIRV-Cross:           ${SPIRV_CROSS_EXECUTABLE}")
endif()

# atmos_compile_slang(<out_var>
#   SOURCE   <file.slang>
#   OUTDIR   <generated dir>
#   ENTRIES  vs:vertex fs:fragment [cs:compute ...]
#   [WGSL] [GLSL] [ESSL])
#
# Appends the generated file paths to <out_var>. Each `ENTRIES` item is
# "<entryPoint>:<stage>". WGSL/GLSL/ESSL select which targets to emit
# (default: all three).
function(atmos_compile_slang out_var)
    cmake_parse_arguments(ARG "WGSL;GLSL;ESSL" "SOURCE;OUTDIR" "ENTRIES" ${ARGN})

    if(NOT ARG_SOURCE OR NOT ARG_OUTDIR OR NOT ARG_ENTRIES)
        message(FATAL_ERROR "atmos_compile_slang: SOURCE, OUTDIR and ENTRIES are required")
    endif()

    # Default to emitting every target when none is requested explicitly.
    if(NOT ARG_WGSL AND NOT ARG_GLSL AND NOT ARG_ESSL)
        set(ARG_WGSL ON)
        set(ARG_GLSL ON)
        set(ARG_ESSL ON)
    endif()

    get_filename_component(_stem "${ARG_SOURCE}" NAME_WE)
    file(MAKE_DIRECTORY "${ARG_OUTDIR}")
    set(_outputs "")

    # WGSL: slangc emits the whole module (all entry points) in one file.
    if(ARG_WGSL)
        set(_wgsl "${ARG_OUTDIR}/${_stem}.wgsl")
        add_custom_command(
            OUTPUT "${_wgsl}"
            COMMAND "${SLANGC_EXECUTABLE}" "${ARG_SOURCE}"
                    -target wgsl -o "${_wgsl}"
            DEPENDS "${ARG_SOURCE}"
            COMMENT "Slang → WGSL: ${_stem}.wgsl"
            VERBATIM)
        list(APPEND _outputs "${_wgsl}")
    endif()

    # GLSL / ESSL go per entry point through SPIR-V then SPIRV-Cross.
    foreach(_entry IN LISTS ARG_ENTRIES)
        string(REPLACE ":" ";" _parts "${_entry}")
        list(GET _parts 0 _ep)
        list(GET _parts 1 _stage)

        set(_spv "${ARG_OUTDIR}/${_stem}.${_ep}.spv")
        add_custom_command(
            OUTPUT "${_spv}"
            COMMAND "${SLANGC_EXECUTABLE}" "${ARG_SOURCE}"
                    -target spirv -entry "${_ep}" -stage "${_stage}"
                    -o "${_spv}"
            DEPENDS "${ARG_SOURCE}"
            COMMENT "Slang → SPIR-V: ${_stem}.${_ep} (${_stage})"
            VERBATIM)

        if(ARG_GLSL)
            set(_glsl "${ARG_OUTDIR}/${_stem}.${_ep}.glsl")
            add_custom_command(
                OUTPUT "${_glsl}"
                COMMAND "${SPIRV_CROSS_EXECUTABLE}" "${_spv}"
                        --version 410 --no-es --output "${_glsl}"
                DEPENDS "${_spv}"
                COMMENT "SPIRV-Cross → GLSL 410: ${_stem}.${_ep}.glsl"
                VERBATIM)
            list(APPEND _outputs "${_glsl}")
        endif()

        if(ARG_ESSL)
            set(_essl "${ARG_OUTDIR}/${_stem}.${_ep}.es.glsl")
            add_custom_command(
                OUTPUT "${_essl}"
                COMMAND "${SPIRV_CROSS_EXECUTABLE}" "${_spv}"
                        --version 300 --es --output "${_essl}"
                DEPENDS "${_spv}"
                COMMENT "SPIRV-Cross → GLSL ES 300: ${_stem}.${_ep}.es.glsl"
                VERBATIM)
            list(APPEND _outputs "${_essl}")
        endif()
    endforeach()

    set(${out_var} "${_outputs}" PARENT_SCOPE)
endfunction()
