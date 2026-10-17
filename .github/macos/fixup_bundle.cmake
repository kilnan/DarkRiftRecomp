include(BundleUtilities)

# Check for pkgx installation
find_program(PKGX_EXECUTABLE pkgx)

# Xcode generator puts the build type in the build directory
set(BUILD_PREFIX "")
if (CMAKE_GENERATOR STREQUAL "Xcode")
    set(BUILD_PREFIX "${CMAKE_BUILD_TYPE}/")
endif()

# Use generator expressions to get the absolute path to the bundle
set(APPS "${BUILD_PREFIX}DarkRiftRecompiled.app/Contents/MacOS/DarkRiftRecompiled")

# Set up framework search paths
set(DIRS "${BUILD_PREFIX}DarkRiftRecompiled.app/Contents/Frameworks")

# Detect if we're using pkgx
if(PKGX_EXECUTABLE)
    message(STATUS "pkgx detected, adding pkgx directories to framework search path")
    list(APPEND DIRS "$ENV{HOME}/.pkgx/")
endif()

# Convert all paths to absolute paths
file(REAL_PATH ${APPS} APPS)

set(RESOLVED_DIRS "")
foreach(DIR IN LISTS DIRS)
    # Handle home directory expansion
    string(REPLACE "~" "$ENV{HOME}" DIR "${DIR}")
    # Convert to absolute path, but don't fail if directory doesn't exist
    if(EXISTS "${DIR}")
        file(REAL_PATH "${DIR}" RESOLVED_DIR)
        list(APPEND RESOLVED_DIRS "${RESOLVED_DIR}")
    endif()
endforeach()

# Debug output
message(STATUS "Bundle fixup paths:")
message(STATUS "  App: ${APPS}")
message(STATUS "  Search dirs: ${RESOLVED_DIRS}")

# Fix up the bundle
fixup_bundle("${APPS}" "" "${RESOLVED_DIRS}")
# Homebrew's "sdl2" is sdl2-compat, which loads SDL3 at runtime from
# @loader_path/libSDL3.dylib. Bundle SDL3 next to it when it is available.
find_library(SDL3_LIBRARY NAMES SDL3 PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH)
if(SDL3_LIBRARY)
    message(STATUS "Bundling SDL3 for sdl2-compat: ${SDL3_LIBRARY}")
    file(REAL_PATH "${SDL3_LIBRARY}" SDL3_REAL)
    list(GET RESOLVED_DIRS 0 FRAMEWORKS_DIR)
    file(COPY_FILE "${SDL3_REAL}" "${FRAMEWORKS_DIR}/libSDL3.dylib")
endif()
