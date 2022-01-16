cmake_minimum_required(VERSION 3.10)
project(.)

find_package(Vulkan REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O")

file(GLOB_RECURSE SOURCES "../src/*.cpp" "../../src/*.c")

add_executable("renderer" ${SOURCES})
target_include_directories("renderer" PRIVATE "../src/renderer" ${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})

# Compile on Windows systems
if(WIN32)
    set(GLSLC "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
    target_link_libraries("renderer" mingw32 SDL2main SDL2 ${Vulkan_LIBRARIES})
endif()

# Compile on Linux systems
if(UNIX OR MSVC)
    find_package(SDL2 REQUIRED)
    set(GLSLC "glslangValidator")
    target_link_libraries("renderer" ${SDL2_LIBRARIES} ${Vulkan_LIBRARIES})
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