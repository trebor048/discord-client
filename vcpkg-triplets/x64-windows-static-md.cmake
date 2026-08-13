set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# NOTE: previously forced VCPKG_VISUAL_STUDIO_PATH to Community, which made
# vcpkg's toolset selection reject the instance ("Unable to find a valid
# Visual Studio instance"). vcpkg auto-detects and prefers the stable
# Community instance anyway, so the override is unnecessary and harmful.

if(PORT STREQUAL "opus")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
