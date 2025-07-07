cmake_minimum_required(VERSION 4.0)
project(3DAnimation)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(FetchExternal)

option(USE_OPENGL "Use OpenGL for rendering" OFF)
if (APPLE)
    # On macOS, we need to use Metal for rendering as OpenGL is deprecated.
    option(USE_METAL "Use Metal for rendering" ON)
endif()


find_package(SDL3 CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

if (USE_OPENGL)
    find_package(OpenGL REQUIRED)
    find_package(glad CONFIG REQUIRED)
endif()

find_path(CGLTF_INCLUDE_DIRS "cgltf.h")
include_directories(${CGLTF_INCLUDE_DIRS})

set(RESOURCE_FILES "")
list(APPEND RESOURCE_FILES ${GLTF_FILES})
list(APPEND RESOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/guy.glb)
list(APPEND RESOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/guy.gltf)
list(APPEND RESOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/alien-bug.glb)

add_executable(${PROJECT_NAME} MACOSX_BUNDLE
    main.cpp
    GameTimer.hpp
    GameTimer.cpp
    File.hpp
    File.cpp
    SimpleMath.h
    SimpleMath.inl
    SimpleMath.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${RESOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-static fmt::fmt)

if (USE_OPENGL)
    target_link_libraries(${PROJECT_NAME} PRIVATE glad::glad OpenGL::GL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENGL)
    target_sources(${PROJECT_NAME} PRIVATE
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

if (APPLE)
    if (USE_METAL)
        # If Metal is used, we need to link against the Apple Metal frameworks.
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_METAL)
    endif()

target_link_libraries(${PROJECT_NAME} PRIVATE AppleFrameworksCpp
        "-framework Foundation"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore")
set_target_properties(${PROJECT_NAME}
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER com.github.MattGuerrette.${PROJECT_NAME}
        XCODE_GENERATE_SCHEME YES
        XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET 17.0
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET 17.0
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET 14.0
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC NO
        XCODE_ATTRIBUTE_PRODUCT_NAME ${PROJECT_NAME}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.github.MattGuerrette.${PROJECT_NAME}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        RESOURCE "${RESOURCE_FILES}")
endif()
