cmake_minimum_required(VERSION 3.0.2)
project(usb_camera)
set(CMAKE_BUILD_TYPE Debug) # 调试模式，生成调试信息
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -ggdb -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1")


find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  image_transport
  cv_bridge
  shm_transport
)

find_package(OpenCV REQUIRED)

catkin_package(
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)


# 明确指定 usb_camera 的源文件，排除测试文件
add_executable(${PROJECT_NAME}
  src/usb_camera.cpp
  src/v4l2.cpp
)

add_executable(test_subscriber
  src/test_sub.cc
)
    target_link_libraries(test_subscriber
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       shm_transport
       -lrt
    )

add_executable(shm_camera
  src/shm_camera.cpp
  src/v4l2.cpp
)

add_executable(shm_sub
  src/shm_sub.cc
)

add_executable(test_v4l2
  src/test_v4l2.cpp
  src/v4l2.cpp
)

message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    add_definitions(-DUSE_ARM_LIB=1)
    message(STATUS "define USE_ARM_LIB 1")
    include_directories(
        /usr/include/rga
    )
    target_link_libraries(${PROJECT_NAME}
      ${catkin_LIBRARIES}
      ${OpenCV_LIBS}
      -lrt
      rga
      rknn_api
      rknnrt
    )
    target_link_libraries(shm_camera
      ${catkin_LIBRARIES}
      ${OpenCV_LIBS}
      shm_transport
      -lrt
      rga
      rknn_api
      rknnrt
    )
    target_link_libraries(shm_sub
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       shm_transport
       -lrt
    )
    target_link_libraries(test_v4l2
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       -lrt
    )
else()
    add_definitions(-DUSE_ARM_LIB=0)
    message(STATUS "define USE_ARM_LIB 0")
    target_link_libraries(${PROJECT_NAME}
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       -lrt
    )
    target_link_libraries(shm_camera
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       shm_transport
       -lrt
    )
    target_link_libraries(shm_sub
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       shm_transport
       -lrt
    )
    target_link_libraries(test_v4l2
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
       -lrt
    )
endif()
