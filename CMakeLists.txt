cmake_minimum_required(VERSION 3.22)
project(SeerSdk LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(SeerSdk INTERFACE)
add_library(SeerSdk::SeerSdk ALIAS SeerSdk)

target_include_directories(SeerSdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(SeerSdk INTERFACE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

target_compile_features(SeerSdk INTERFACE cxx_std_17)

install(TARGETS SeerSdk EXPORT SeerSdkTargets)
install(DIRECTORY seer/ DESTINATION include/seer FILES_MATCHING PATTERN "*.h")
install(EXPORT SeerSdkTargets
    FILE SeerSdkTargets.cmake
    NAMESPACE SeerSdk::
    DESTINATION lib/cmake/SeerSdk
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SeerSdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SeerSdkConfig.cmake"
    INSTALL_DESTINATION lib/cmake/SeerSdk
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SeerSdkVersion.cmake"
    VERSION 3.0.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SeerSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SeerSdkVersion.cmake"
    DESTINATION lib/cmake/SeerSdk
)
