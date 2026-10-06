# The visionOS app around the port: a SwiftUI window for the menus and the
# HUD, and an immersive space where the match stands on the player's table.
# See README.md here.
enable_language(Swift)

set(MELEE_BUNDLE_ID "com.example.melee-diorama" CACHE STRING
    "The app's bundle identifier: one of your own, to sign it")
set(MELEE_DEVELOPMENT_TEAM "" CACHE STRING "Your Apple development team ID")

file(GLOB _app_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Sources/*.swift"
    "${CMAKE_CURRENT_SOURCE_DIR}/Sources/*.metal")

add_executable(Diorama MACOSX_BUNDLE ${_app_sources})
target_link_libraries(Diorama PRIVATE melee)

set_target_properties(Diorama PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MELEE_BUNDLE_ID}"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources/Bridging-Header.h"
    XCODE_ATTRIBUTE_HEADER_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/src"
    XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 7
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MELEE_DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_MTL_LANGUAGE_REVISION Metal31
)

target_link_libraries(Diorama PRIVATE
    "-framework SwiftUI" "-framework CompositorServices" "-framework ARKit"
    "-framework Metal" "-framework QuartzCore" "-framework IOSurface"
    "-framework GameController" "-framework AVFAudio")
