# The tools that tools/pins.json pins. The build accepts no other version of them, so that every
# machine builds and checks Stasis alike, and editing the pins configures it again.

include_guard(GLOBAL)

set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tools/pins.json"
)

# Sets `variable` to the version of `tool` that tools/pins.json pins.
function(stasis_pinned_version variable tool)
    file(READ "${PROJECT_SOURCE_DIR}/tools/pins.json" pins)
    string(JSON version GET "${pins}" ${tool} version)
    set(${variable} "${version}" PARENT_SCOPE)
endfunction()
