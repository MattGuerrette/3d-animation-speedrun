cmake_minimum_required(VERSION 4.0)
project(3DAnimation)

find_package(SDL3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

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
    ${RESOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-static glad::glad fmt::fmt OpenGL::GL)
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
