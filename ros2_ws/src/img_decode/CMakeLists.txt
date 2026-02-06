cmake_minimum_required(VERSION 3.5)
project(img_decode)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED)

# Include directories
include_directories(
  include
  src
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


# 源文件
set(SOURCES 
  src/img_decode.cpp
  src/mpp_decode.cpp
  src/rga_resize.cpp
)

# img_decode 主节点
add_executable(img_decode ${SOURCES})
ament_target_dependencies(img_decode
  rclcpp
  sensor_msgs
  cv_bridge
  image_transport
)

target_link_libraries(img_decode
  ${OpenCV_LIBS}
)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
    target_link_libraries(img_decode
      rga
      rockchip_mpp
    )
endif()

# img_decode_sub 订阅者节点
add_executable(img_decode_sub
  src/img_decode_sub.cpp
)

ament_target_dependencies(img_decode_sub
  rclcpp
  sensor_msgs
)

# Install executables
install(TARGETS
  img_decode
  img_decode_sub
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
