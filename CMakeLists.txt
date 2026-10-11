cmake_minimum_required(VERSION 3.16)
project(uncanny_eyes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
find_package(ZLIB REQUIRED)

add_executable(uncanny-eyes
    src/main.cpp
    src/eye.cpp
    src/DrowsinessModel.cpp
    src/AlertnessModel.cpp
    src/ExpressionProfile.cpp
    src/PupilResponseModel.cpp
    src/LDRSerialInput.cpp
    src/artwork_default.cpp
    src/artwork_cat.cpp
    src/artwork_dragon.cpp
    src/artwork_custom_dragon.cpp
    src/artwork_custom_dragon_A.cpp
    src/artwork_custom_dragon_B.cpp
    src/artwork_testy.cpp
    src/artwork_no_sclera.cpp
    src/artwork_icu.cpp
    src/artwork_night_bloom.cpp
    src/artwork_sgtpkldck.cpp
    src/artwork_coldborgjr.cpp
    src/artwork_cold_borg.cpp
    src/artwork_alien_diabetic.cpp
    src/artwork_cold_blue.cpp
    src/artwork_fire.cpp
    src/artwork_bluehole.cpp
    src/artwork_nptest.cpp
    src/artwork_zwhole.cpp
    src/artwork_cham.cpp
    src/artwork_xwhole.cpp
    src/artwork_yolk.cpp
    src/artwork_agate.cpp
    src/artwork_chalk_blue.cpp
    src/artwork_registry.cpp
)

target_include_directories(uncanny-eyes PRIVATE src)
target_link_libraries(uncanny-eyes PRIVATE SDL2::SDL2 ${SDL2_IMAGE_LIBRARIES} ZLIB::ZLIB)
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

# Renderer regression suite. Links no SDL: it exercises the pure renderer and
# the animator only, so it runs headless in CI.
add_executable(uncanny-eyes-render-test
    tests/eye_render_test.cpp
    src/eye.cpp
    src/DrowsinessModel.cpp
    src/AlertnessModel.cpp
    src/ExpressionProfile.cpp
    src/PupilResponseModel.cpp
    src/artwork_default.cpp
    src/artwork_cat.cpp
    src/artwork_dragon.cpp
    src/artwork_custom_dragon.cpp
    src/artwork_custom_dragon_A.cpp
    src/artwork_custom_dragon_B.cpp
    src/artwork_testy.cpp
    src/artwork_no_sclera.cpp
    src/artwork_icu.cpp
    src/artwork_night_bloom.cpp
    src/artwork_sgtpkldck.cpp
    src/artwork_coldborgjr.cpp
    src/artwork_cold_borg.cpp
    src/artwork_alien_diabetic.cpp
    src/artwork_cold_blue.cpp
    src/artwork_fire.cpp
    src/artwork_bluehole.cpp
    src/artwork_nptest.cpp
    src/artwork_zwhole.cpp
    src/artwork_cham.cpp
    src/artwork_xwhole.cpp
    src/artwork_yolk.cpp
    src/artwork_agate.cpp
    src/artwork_chalk_blue.cpp
    src/artwork_registry.cpp
)
target_include_directories(uncanny-eyes-render-test PRIVATE src)
target_compile_options(uncanny-eyes-render-test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME eye_render_model COMMAND uncanny-eyes-render-test)
