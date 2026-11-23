cmake_minimum_required(VERSION 3.13)
project(vision_fly_host_checks LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d)

set(project_root "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(vision_core
  "${project_root}/tools/config.cpp"
  "${project_root}/tools/debug_view.cpp"
  "${project_root}/tools/debug_frame.cpp"
  "${project_root}/tools/result_writer.cpp"
  "${project_root}/io/pose_receiver.cpp"
  "${project_root}/io/uart_protocol.cpp"
  "${project_root}/io/serial_link.cpp"
  "${project_root}/tasks/target_position/detector.cpp"
  "${project_root}/tasks/target_position/infantry.cpp"
  "${project_root}/tasks/target_position/infantry_zoom.cpp"
  "${project_root}/tasks/target_position/solver.cpp"
)
target_include_directories(vision_core PUBLIC "${project_root}" ${OpenCV_INCLUDE_DIRS})
# 此库与板端保持一致，只链接core/imgproc；calib3d仅用于测试参考答案。
target_link_libraries(vision_core PUBLIC opencv_core opencv_imgproc Threads::Threads)
target_compile_options(vision_core PRIVATE -O3 -Wall -Wextra -Wpedantic -Werror)
foreach(name solver detector io pipeline infantry uart infantry_zoom)
  add_executable(test_${name} test_${name}.cpp)
  target_link_libraries(test_${name} PRIVATE vision_core)
  target_compile_options(test_${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()
target_link_libraries(test_solver PRIVATE opencv_calib3d)
add_executable(vision_offline offline.cpp)
target_link_libraries(vision_offline PRIVATE vision_core opencv_imgcodecs)

target_link_libraries(test_uart PRIVATE util)
