# Header-only library.
#
# Uses vcpkg_from_git rather than the registry port's vcpkg_from_github: GitHub
# regenerates release .tar.gz archives lazily, so a SHA512 pinned against one
# can drift under a fixed tag. A git clone pinned to a commit is content-
# addressed and immune. REF must be a full commit SHA (vcpkg_from_git
# enforces this) — resolved from the v2.9.7 tag via:
#   git ls-remote https://github.com/syoyo/tinygltf.git v2.9.7
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/syoyo/tinygltf.git
    REF 488a70a3df62a4df1a736e9e56fb8836580c4888 # v2.9.7
)

# Installed unpatched: the engine defines TINYGLTF_NO_INCLUDE_JSON and
# includes its own nlohmann before <tiny_gltf.h>, so the header's default
# #include "json.hpp" is never reached. No source patch needed.
file(INSTALL "${SOURCE_PATH}/tiny_gltf.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
