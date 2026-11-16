# kb_packages: project packages (kb_cli package and the editor's Packages panel): the registry index, archives
# (tar, one zstd frame), downloads through kb_http, version resolution, the lock file and verification.
add_library(kb_packages STATIC
    include/kb/packages/Packages.hpp
    include/kb/packages/PackageArchive.hpp
    src/PackagesInternal.hpp
    src/PackageModel.cpp
    src/PackageModules.cpp
    src/PackageOperations.cpp
    src/PackageRegistry.cpp
    src/PackageArchive.cpp
)

target_include_directories(kb_packages
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kb_packages
    PUBLIC
        kb_engine
    PRIVATE
        kb_http
        kb_zstd
)

kb_apply_quality_gates(kb_packages)
