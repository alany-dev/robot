cmake_minimum_required(VERSION 3.5)
project(usb_camera)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 调试模式设置
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1")

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

# Include directories
include_directories(
  include
  ${OpenCV_INCLUDE_DIRS}
)

# 检查架构
message(STATUS "CMAKE_HOST_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    add_definitions(-DUSE_ARM_LIB=1)
    message(STATUS "define USE_ARM_LIB 1")
    include_directories(/usr/include/rga)
else()
    add_definitions(-DUSE_ARM_LIB=0)
    message(STATUS "define USE_ARM_LIB 0")
endif()

# USB Camera 主节点
add_executable(usb_camera
  src/usb_camera.cpp
  src/v4l2.cpp
)

ament_target_dependencies(usb_camera
  rclcpp
  sensor_msgs
  std_msgs
)

target_link_libraries(usb_camera
  ${OpenCV_LIBS}
  -lrt
)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    target_link_libraries(usb_camera
      rga
      rknn_api
      rknnrt
    )
endif()

# 测试订阅者节点
add_executable(test_subscriber
  src/test_sub.cc
)

ament_target_dependencies(test_subscriber
  rclcpp
  sensor_msgs
)

target_link_libraries(test_subscriber
  ${OpenCV_LIBS}
)

# V4L2 测试程序
add_executable(test_v4l2
  src/test_v4l2.cpp
  src/v4l2.cpp
)

target_link_libraries(test_v4l2
  ${OpenCV_LIBS}
  -lrt
)

# Install executables
install(TARGETS
  usb_camera
  test_subscriber
  test_v4l2
  DESTINATION lib/${PROJECT_NAME}
)



if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
