# Helper used by add_custom_target COMMAND blocks at build time.
# Usage: cmake -Dsrc=<path> -Ddst=<path> -P copy_dir_if_exists.cmake
# Silently skips when src does not exist or is empty.
if(IS_DIRECTORY "${src}")
    file(GLOB _entries "${src}/*")
    if(_entries)
        file(COPY "${src}/." DESTINATION "${dst}")
    endif()
endif()
