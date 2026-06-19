# Downloads a file from URL to DST only when DST does not already exist.
# Usage: cmake -DURL=<url> -DDST=<path> -P download_if_missing.cmake
if(NOT EXISTS "${DST}")
    get_filename_component(_dir "${DST}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    message(STATUS "Downloading ${URL}")
    file(DOWNLOAD "${URL}" "${DST}" SHOW_PROGRESS
         STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        list(GET _status 1 _err)
        message(WARNING "Download failed (${_err}): ${URL}")
        file(REMOVE "${DST}")
    endif()
endif()
