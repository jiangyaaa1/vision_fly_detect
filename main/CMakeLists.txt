# MaixCDK主组件；默认板端仅使用OpenCV core/imgproc，不需要calib3d。
set(project_root "${CMAKE_CURRENT_LIST_DIR}/..")
list(APPEND ADD_INCLUDE "${project_root}")
list(APPEND ADD_SRCS
  "${project_root}/src/main.cpp"
  "${project_root}/tasks/target_position/yolo.cpp"
  "${project_root}/tasks/target_position/infantry.cpp"
  "${project_root}/tasks/target_position/infantry_zoom.cpp"
  "${project_root}/tasks/target_position/detector.cpp"
  "${project_root}/tasks/target_position/solver.cpp"
  "${project_root}/tools/config.cpp"
  "${project_root}/tools/debug_view.cpp"
  "${project_root}/tools/debug_frame.cpp"
  "${project_root}/tools/debug_sender.cpp"
  "${project_root}/tools/benchmark.cpp"
  "${project_root}/tools/result_writer.cpp"
  "${project_root}/io/pose_receiver.cpp"
  "${project_root}/io/uart_protocol.cpp"
  "${project_root}/io/serial_link.cpp"
)
list(APPEND ADD_REQUIREMENTS basic vision nn opencv media_server peripheral)
# SDK默认-Os以缩小程序；定位/解析循环改用-O3，保留IEEE浮点语义，不启用fast-math。
list(APPEND ADD_DEFINITIONS -std=c++17 -O3)
register_component()
