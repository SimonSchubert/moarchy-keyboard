cmake_minimum_required(VERSION 3.21)
project(moarchy-keyboard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XkbCommon REQUIRED IMPORTED_TARGET xkbcommon)

qt_standard_project_setup(REQUIRES 6.5)

# The QML lives in qml/ rather than at the module root, which QTP0004 wants a
# qmldir for. NEW makes Qt generate one.
if(POLICY QTP0004)
  qt_policy(SET QTP0004 NEW)
endif()

# --- Wayland protocol glue --------------------------------------------------
#
# Both protocols are VENDORED in protocols/, not taken from a package, because
# no Arch package ships them. input-method-unstable-v2 and
# virtual-keyboard-unstable-v1 live in the wlroots source tree; the
# `wlr-protocols` package contains only the wlr-* prefixed protocols, and
# /usr/share/wayland-protocols ships input-method *v1*, a different and
# unrelated protocol that Sway does not advertise. squeekboard and wvkbd vendor
# these same two files for the same reason.
#
# Source: https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/protocol
# Refresh with scripts/update-protocols.sh.
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Generates <basename>-client-protocol.h and <basename>-protocol.c. The .c is
# `private-code`, not `public-code`: these protocols have no shared library, so
# the interface symbols must be compiled into this binary.
set(PROTOCOL_SOURCES "")
function(moarchy_protocol basename)
  set(xml "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${basename}.xml")
  set(header "${GENERATED_DIR}/${basename}-client-protocol.h")
  set(code   "${GENERATED_DIR}/${basename}-protocol.c")
  add_custom_command(OUTPUT "${header}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
    DEPENDS "${xml}" VERBATIM)
  add_custom_command(OUTPUT "${code}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${code}"
    DEPENDS "${xml}" VERBATIM)
  set(PROTOCOL_SOURCES ${PROTOCOL_SOURCES} "${header}" "${code}" PARENT_SCOPE)
endfunction()

# The enum="zwp_text_input_v3.*" attributes on content_type and
# text_change_cause are documentation only -- wayland-scanner does not emit a
# cross-protocol dependency for them -- so text-input-v3 is NOT needed here.
# Those enum values are declared in src/inputmethod.h instead.
moarchy_protocol(input-method-unstable-v2)
moarchy_protocol(virtual-keyboard-unstable-v1)

qt_add_executable(moarchy-keyboard
  src/main.cpp
  src/waylandconnection.cpp
  src/inputmethod.cpp
  src/virtualkeyboard.cpp
  src/keyrouter.cpp
  src/theme.cpp
  src/oskservice.cpp
  src/panel.cpp
  src/layoutstore.cpp
  ${PROTOCOL_SOURCES}
)

include(GNUInstallDirs)
target_compile_definitions(moarchy-keyboard PRIVATE
  MOARCHY_VERSION="${PROJECT_VERSION}"
  MOARCHY_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}"
)

target_include_directories(moarchy-keyboard PRIVATE
  "${GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(moarchy-keyboard PRIVATE
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus
  LayerShellQt::Interface
  PkgConfig::WaylandClient
  PkgConfig::XkbCommon
)

qt_add_qml_module(moarchy-keyboard
  URI moarchy
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Keyboard.qml
    qml/KeyRow.qml
    qml/KeyCap.qml
    qml/AlternatesPopup.qml
  RESOURCE_PREFIX /
)

# Shipped layouts are data, not code (AC 18) -- but they are baked into the
# binary as a fallback so a broken install still types. LayoutStore prefers
# ~/.config/moarchy-keyboard/layouts, then the installed datadir, then these.
qt_add_resources(moarchy-keyboard "layouts"
  PREFIX "/layouts"
  BASE "layouts"
  FILES
    layouts/letters.json
    layouts/symbols.json
    layouts/symbols-extended.json
    layouts/numeric.json
    layouts/terminal.json
)

install(TARGETS moarchy-keyboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY layouts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/moarchy-keyboard/layouts)
