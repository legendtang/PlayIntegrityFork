cmake_minimum_required(VERSION 3.22.1)

project(playintegrityfix)

option(PIF_USE_PREFAB_CXX "Use the Gradle Prefab libc++ dependency" ON)
option(PIF_BUILD_NATIVE_TESTS "Build offline Android property-hook tests" OFF)
if(PIF_USE_PREFAB_CXX)
    find_package(cxx REQUIRED CONFIG)
    link_libraries(cxx::cxx)
endif()

enable_language(C CXX ASM)

include(FetchContent)
if(CMAKE_ANDROID_ARCH_ABI MATCHES "^(x86|x86_64)$")
    set(PIF_GUM_VERSION "17.25.2")
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(PIF_GUM_SHA256 "8ae298fbecab410b38d615d14187689546017d26c0e5304a484aa71162d4bb98")
    else()
        set(PIF_GUM_SHA256 "74f277555e0903dae14671cb0b14e7803e19945efa3110c124e4c805b25ff730")
    endif()
    # An explicit root permits offline/reproducible builds with a verified devkit.
    set(PIF_GUM_ROOT "" CACHE PATH "Directory containing frida-gum.h and libfrida-gum.a")
    if(NOT PIF_GUM_ROOT)
        FetchContent_Declare(gum_devkit
            URL "https://github.com/frida/frida/releases/download/${PIF_GUM_VERSION}/frida-gum-devkit-${PIF_GUM_VERSION}-android-${CMAKE_ANDROID_ARCH_ABI}.tar.xz"
            URL_HASH "SHA256=${PIF_GUM_SHA256}"
        )
        FetchContent_GetProperties(gum_devkit)
        if(NOT gum_devkit_POPULATED)
            FetchContent_Populate(gum_devkit)
        endif()
        set(PIF_GUM_ROOT "${gum_devkit_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${PIF_GUM_ROOT}/frida-gum.h" OR NOT EXISTS "${PIF_GUM_ROOT}/libfrida-gum.a")
        message(FATAL_ERROR "Missing Gum header/static library in ${PIF_GUM_ROOT}")
    endif()
    add_library(pif_gum STATIC IMPORTED)
    set_target_properties(pif_gum PROPERTIES
        IMPORTED_LOCATION "${PIF_GUM_ROOT}/libfrida-gum.a"
        INTERFACE_INCLUDE_DIRECTORIES "${PIF_GUM_ROOT}")
    set(PIF_HOOK_SRC hook_gum.cpp)
    set(PIF_HOOK_LIBS pif_gum log m atomic dl)
elseif(CMAKE_ANDROID_ARCH_ABI MATCHES "^(armeabi-v7a|arm64-v8a)$")
    FetchContent_Declare(
      shadowhook
      GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
      GIT_TAG        v2.0.1
      PATCH_COMMAND perl -pi -e "s/sh_linker_init/sh_linker_init_dummy/g" shadowhook/src/main/cpp/sh_linker.c
    )
    FetchContent_GetProperties(shadowhook)
    if(NOT shadowhook_POPULATED)
        FetchContent_Populate(shadowhook)
    endif()
    file(GLOB SHADOWHOOK_SRC
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/*.c
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/common/*.c
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/third_party/xdl/*.c)
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
        set(SHADOWHOOK_ARCH arm)
    else()
        set(SHADOWHOOK_ARCH arm64)
    endif()
    file(GLOB SHADOWHOOK_ARCH_SRC
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/arch/${SHADOWHOOK_ARCH}/*.c
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/arch/${SHADOWHOOK_ARCH}/*.S)
    include_directories(
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/common
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/include
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/arch/${SHADOWHOOK_ARCH}
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/third_party/bsd
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/third_party/lss
        ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/third_party/xdl)
    set(PIF_HOOK_SRC hook_shadow.cpp ${SHADOWHOOK_SRC} ${SHADOWHOOK_ARCH_SRC})
    set(PIF_HOOK_LIBS log)
else()
    message(FATAL_ERROR "Unsupported module ABI: ${CMAKE_ANDROID_ARCH_ABI}")
endif()

add_library(${CMAKE_PROJECT_NAME} SHARED main.cpp property_hooks.cpp
    local_cxa_atexit_finalize_impl/atexit.cpp ${PIF_HOOK_SRC})
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -fvisibility=hidden -ffunction-sections -fdata-sections)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${PIF_HOOK_LIBS})

if(PIF_BUILD_NATIVE_TESTS)
    add_executable(property_hook_test ../../../../tests/native/property_hook_test.cpp property_hooks.cpp ${PIF_HOOK_SRC})
    target_compile_features(property_hook_test PRIVATE cxx_std_23)
    target_include_directories(property_hook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(property_hook_test PRIVATE ${PIF_HOOK_LIBS})
endif()
