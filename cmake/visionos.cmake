# The native port for Apple Vision Pro (cmake --preset visionos), as an Xcode
# project. For the simulator, set CMAKE_OSX_SYSROOT to xrsimulator.
set(CMAKE_SYSTEM_NAME visionOS)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 2.0)
if(NOT CMAKE_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT xros)
endif()

# Dependencies (Aurora's, Dawn's) that check for Apple's mobile platforms by
# IOS take visionOS for one: it is UIKit, Metal and CAMetalLayer as iOS is
set(IOS TRUE)
