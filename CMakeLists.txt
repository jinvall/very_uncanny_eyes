cmake_minimum_required(VERSION 3.16)
project(uncanny_eyes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)

add_executable(uncanny-eyes
    src/main.cpp
    src/eye.cpp
    src/DrowsinessModel.cpp
    src/LDRSerialInput.cpp
    src/artwork_default.cpp
    src/artwork_cat.cpp
    src/artwork_dragon.cpp
    src/artwork_custom_dragon.cpp
    src/artwork_custom_dragon_A.cpp
    src/artwork_custom_dragon_B.cpp
    src/artwork_cold_blue.cpp
    src/artwork_fire.cpp
    src/artwork_chalk_blue.cpp
    src/artwork_registry.cpp
)

target_include_directories(uncanny-eyes PRIVATE src)
target_link_libraries(uncanny-eyes PRIVATE SDL2::SDL2)
target_compile_options(uncanny-eyes PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(uncanny-eyes-ldr-test
    tests/ldr_test.cpp
    src/LDRSerialInput.cpp
    src/PupilResponseModel.cpp
)
target_include_directories(uncanny-eyes-ldr-test PRIVATE src)
target_compile_options(uncanny-eyes-ldr-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ldr_serial_model COMMAND uncanny-eyes-ldr-test)
