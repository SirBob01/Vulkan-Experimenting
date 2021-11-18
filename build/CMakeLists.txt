cmake_minimum_required(VERSION 3.10)
project(.)

find_file(SDL2_INCLUDE_DIR NAME SDL.h HINTS SDL2)
find_library(SDL2_LIBRARY NAME SDL2)
find_package(Vulkan REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O")

file(GLOB_RECURSE SOURCES "../src/*.cpp" "../../src/*.c")

add_executable("renderer" ${SOURCES})

# Compile on Windows systems
if(WIN32)
    set(GLSLC "C:/VulkanSDK/1.2.176.1/Bin/glslangValidator.exe")
    target_include_directories("renderer" PRIVATE "../src/renderer" ${Vulkan_INCLUDE_DIR})
    target_link_libraries("renderer" mingw32 SDL2main SDL2 ${Vulkan_LIBRARY})
endif()

# Compile on Linux systems
if(UNIX OR MSVC)
    set(GLSLC "glslangValidator")
    target_include_directories("renderer" PRIVATE "../src/renderer")
    target_link_libraries("renderer" SDL2 vulkan)
endif()

# Compile shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "../src/renderer/shaders/*.frag"
    "../src/renderer/shaders/*.vert"
)
foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "./${FILE_NAME}.spv")
    execute_process(
        COMMAND ${GLSLC} -V ${GLSL} -o ${SPIRV}
    )
endforeach()