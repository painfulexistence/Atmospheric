# AssetPipeline.cmake — Atmospheric Engine build-time asset helpers
#
# Functions:
#   ae_copy_engine_assets(TARGET)
#   ae_copy_game_assets(TARGET ASSETS_DIR)
#   ae_convert_textures(TARGET SRC_DIR DST_DIR)   — Emscripten+BasisUniversal only
#
# Include via:
#   include(AssetPipeline)     # after InstallDependencies sets CMAKE_MODULE_PATH
#   include(AtmosphericEngine/cmake/helpers/AssetPipeline.cmake)  # full path

# Capture definition-time dir; CMAKE_CURRENT_LIST_DIR inside a function
# body refers to the CALLER's directory, not this file's directory.
set(_AE_HELPERS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

# ── ae_copy_engine_assets(<target>) ───────────────────────────────────────────
# Copies Engine/default_assets → <target binary dir>/assets at POST_BUILD.
# No-op on Android and Emscripten (those platforms handle assets differently).
function(ae_copy_engine_assets TARGET)
    if(ANDROID OR EMSCRIPTEN)
        return()
    endif()
    if(NOT DEFINED AE_ASSETS_DIR)
        message(WARNING "ae_copy_engine_assets: AE_ASSETS_DIR not set. "
                        "Make sure add_subdirectory(Atmospheric) runs before this call.")
        return()
    endif()
    if(IOS)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${AE_ASSETS_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/assets"
            COMMENT "[AE] Copying engine assets into iOS bundle for ${TARGET}"
            VERBATIM
        )
    else()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${AE_ASSETS_DIR}"
                "$<TARGET_FILE_DIR:${TARGET}>/assets"
            COMMENT "[AE] Copying engine assets for ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()

# ── ae_copy_game_assets(<target> <assets_dir>) ────────────────────────────────
# Copies <assets_dir> → <target binary dir>/assets at POST_BUILD.
# Silently skips if <assets_dir> does not exist (build-time check via script).
# No-op on Android and Emscripten.
function(ae_copy_game_assets TARGET ASSETS_DIR)
    if(ANDROID OR EMSCRIPTEN)
        return()
    endif()
    if(IOS)
        # Configure-time existence check is sufficient for iOS bundles.
        if(EXISTS "${ASSETS_DIR}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${ASSETS_DIR}"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/assets"
                COMMENT "[AE] Copying game assets into iOS bundle for ${TARGET}"
                VERBATIM
            )
        endif()
    else()
        # Use copy_if_exists.cmake for a build-time existence check so the
        # command does not fail if the assets directory is absent.
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-Dsrc=${ASSETS_DIR}"
                "-Ddst=$<TARGET_FILE_DIR:${TARGET}>/assets"
                -P "${_AE_HELPERS_DIR}/../copy_if_exists.cmake"
            COMMENT "[AE] Copying game assets for ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()

# ── ae_copy_scenes(<target> <scenes_dir>) ─────────────────────────────────────
# Copies <scenes_dir> → <target binary dir>/assets/scenes at POST_BUILD.
# Keeping scenes under assets/ makes cross-platform packaging consistent and
# allows a second assets/ overlay folder to patch individual scenes at runtime.
# Silently skips if <scenes_dir> does not exist.
# No-op on Android and Emscripten (Emscripten handles scenes via --preload-file).
function(ae_copy_scenes TARGET SCENES_DIR)
    if(ANDROID OR EMSCRIPTEN)
        return()
    endif()
    if(IOS)
        if(EXISTS "${SCENES_DIR}")
            add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${SCENES_DIR}"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/assets/scenes"
                COMMENT "[AE] Copying scenes into iOS bundle for ${TARGET}"
                VERBATIM
            )
        endif()
    else()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-Dsrc=${SCENES_DIR}"
                "-Ddst=$<TARGET_FILE_DIR:${TARGET}>/assets/scenes"
                -P "${_AE_HELPERS_DIR}/../copy_if_exists.cmake"
            COMMENT "[AE] Copying scenes for ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()

# ── ae_convert_textures(<target> <src_dir> <dst_dir>) ─────────────────────────
# Emscripten + BasisUniversal only: converts PNG/JPG under <src_dir> to KTX2
# in <dst_dir>. No-op on native builds or when AE_USE_BASIS_UNIVERSAL is OFF.
function(ae_convert_textures TARGET SRC_DIR DST_DIR)
    if(NOT EMSCRIPTEN OR NOT AE_USE_BASIS_UNIVERSAL)
        return()
    endif()
    if(NOT EXISTS "${SRC_DIR}")
        return()
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_DIR}")

    file(GLOB_RECURSE _imgs LIST_DIRECTORIES false
        "${SRC_DIR}/*.png" "${SRC_DIR}/*.jpg" "${SRC_DIR}/*.jpeg"
    )
    if(NOT _imgs)
        return()
    endif()

    set(_outputs)
    foreach(_img ${_imgs})
        if(_img MATCHES "heightmap")
            continue()
        endif()
        file(RELATIVE_PATH _rel "${SRC_DIR}" "${_img}")
        get_filename_component(_stem   "${_rel}" NAME_WE)
        get_filename_component(_subdir "${_rel}" DIRECTORY)
        set(_out_dir "${DST_DIR}/${_subdir}")
        set(_ktx2    "${_out_dir}/${_stem}.ktx2")
        list(APPEND _outputs "${_ktx2}")

        add_custom_command(
            OUTPUT "${_ktx2}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_dir}"
            COMMAND "${BASISU_EXECUTABLE}" -ktx2 -mipmap -y_flip "${_img}" -output_path "${_out_dir}"
            DEPENDS "${_img}" basisu_host_tool
            COMMENT "[AE] Converting ${_stem} → KTX2"
            VERBATIM
        )
    endforeach()

    if(_outputs)
        add_custom_target(${TARGET}_convert_textures ALL DEPENDS ${_outputs})
        add_dependencies(${TARGET} ${TARGET}_convert_textures)
    endif()
endfunction()
