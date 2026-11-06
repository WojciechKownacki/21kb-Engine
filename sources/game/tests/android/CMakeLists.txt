cmake_minimum_required(VERSION 3.25)
project(ControllerTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(game-activity REQUIRED CONFIG)
find_package(games-controller REQUIRED CONFIG)
add_library(kb_controller_tests SHARED
    src/main/cpp/ControllerTests.cpp
    src/main/cpp/TouchTests.cpp
    src/main/cpp/HapticsTests.cpp
    ../../../engine/src/input/InputText.cpp
    ../../src/android/AndroidGamepadInput.cpp
    ../../src/android/AndroidTouchInput.cpp)
target_include_directories(kb_controller_tests PRIVATE
    ../../src/private ../../../engine/include)
target_compile_options(kb_controller_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(kb_controller_tests PRIVATE
    game-activity::game-activity_static games-controller::paddleboat_static android log)
target_link_options(kb_controller_tests PRIVATE
    -Wl,--wrap=Paddleboat_setControllerStatusCallback
    -Wl,--wrap=Paddleboat_getControllerInfo
    -Wl,--wrap=Paddleboat_getControllerData
    -Wl,--wrap=Paddleboat_setControllerVibrationData
    -Wl,-u,Java_com_google_androidgamesdk_GameActivity_initializeNativeCode)
