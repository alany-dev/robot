cmake_minimum_required(VERSION 3.0.2)
project(img_decode)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  sensor_msgs
  shm_transport
)

find_package(OpenCV REQUIRED)

catkin_package(
)

include_directories(
  ${catkin_INCLUDE_DIRS}
)

set(SOURCES 
  src/img_decode.cpp
  src/mpp_decode.cpp
  src/rga_resize.cpp
)

add_executable(${PROJECT_NAME}
${SOURCES}
)

add_executable(img_decode_sub
  src/img_decode_sub.cpp
)

add_executable(shm_img_decode
  src/shm_img_decode.cpp
  src/mpp_decode.cpp
  src/rga_resize.cpp
)

add_executable(shm_img_decode_test
  src/shm_img_decode_test.cpp
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
      rga
      # rknn_api
      # rknnrt
      rockchip_mpp
    )

    target_link_libraries(img_decode_sub
      ${catkin_LIBRARIES}
    )
 
    target_link_libraries(shm_img_decode
      ${catkin_LIBRARIES}
      ${OpenCV_LIBS}
      rga
      # rknn_api
      # rknnrt
      rockchip_mpp
      shm_transport
      -lrt
    )
    target_link_libraries(shm_img_decode_test
      ${catkin_LIBRARIES}
      shm_transport
      -lrt
    )
else()
    add_definitions(-DUSE_ARM_LIB=0)
    message(STATUS "define USE_ARM_LIB 0")
    target_link_libraries(${PROJECT_NAME}
       ${catkin_LIBRARIES}
       ${OpenCV_LIBS}
    )
    # img_decode订阅节点链接库（非ARM平台）
    target_link_libraries(img_decode_sub
      ${catkin_LIBRARIES}
    )
    # 共享内存版本链接库（非ARM平台）
    target_link_libraries(shm_img_decode
      ${catkin_LIBRARIES}
      ${OpenCV_LIBS}
      shm_transport
      -lrt
    )
    target_link_libraries(shm_img_decode_test
      ${catkin_LIBRARIES}
      shm_transport
      -lrt
    )
endif()

