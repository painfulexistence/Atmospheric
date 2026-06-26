# Helper used by add_custom_target COMMAND blocks at build time.
# Usage: cmake -Dsrc=<path> -Ddst=<path> -P copy_if_exists.cmake
# Silently skips when src does not exist or is empty.
#
# - src is a directory: copies its contents into dst (mkdir dst if needed)
# - src is a file:      copies it to dst (only if content differs)
# - src does not exist: no-op (this is the "if exists" part of the name)
if(IS_DIRECTORY "${src}")
    file(GLOB _entries "${src}/*")
    if(_entries)
        file(COPY "${src}/." DESTINATION "${dst}")
    endif()
elseif(EXISTS "${src}")
    file(COPY_FILE "${src}" "${dst}" ONLY_IF_DIFFERENT)
endif()
