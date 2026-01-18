cmake_minimum_required(VERSION 3.0.2)
project(sherpa_onnx_voice)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS roscpp std_msgs
)

include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/../..
  ${catkin_INCLUDE_DIRS}
)


add_executable(sherpa-onnx-microphone-test1
    sherpa-onnx-microphone.cc
    microphone.cc
  )

target_link_libraries(sherpa-onnx-microphone-test1 
  portaudio_static 
  sherpa-onnx-core 
  ${catkin_LIBRARIES}
  pthread  
  /usr/local/lib/libzmq_component.so
  zmq
  jack  
)


target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib")
target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../../../sherpa_onnx/lib")

get_filename_component(OFFLINE_AGENT_DIR ${PROJECT_SOURCE_DIR}/../../../.. ABSOLUTE)
install(
    TARGETS sherpa-onnx-microphone-test1 
    RUNTIME DESTINATION ${OFFLINE_AGENT_DIR}/install/bin
  )
