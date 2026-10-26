cmake_minimum_required(VERSION 3.16)
project(live_engine LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

find_package(X11 REQUIRED)

add_executable(live_engine
    src/main.cpp
    src/cuda_renderer.cu
    src/engine/swaptube_outer_billiards_engine.cu
    src/swaptube/CUDA/outer_billiards_singularity.cu
)

target_include_directories(live_engine PRIVATE src)

target_link_libraries(live_engine PRIVATE ${X11_LIBRARIES})
target_include_directories(live_engine PRIVATE ${X11_INCLUDE_DIR})

if(UNIX AND NOT APPLE)
    target_link_libraries(live_engine PRIVATE m pthread)
endif()
