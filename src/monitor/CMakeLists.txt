cmake_minimum_required(VERSION 3.0.2)
project(monitor)

## Compile as C++11, supported in ROS Kinetic and newer
add_compile_options(-std=c++11)

## Find catkin macros and libraries
find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  std_msgs
  rosbag
  message_generation
)

## System dependencies
find_package(Threads REQUIRED)

################################################
## Declare ROS messages, services and actions ##
################################################

## Generate messages in the 'msg' folder
add_message_files(
  FILES
  CpuLoad.msg
  CpuStat.msg
  CpuSoftIrq.msg
  MemInfo.msg
  NetInfo.msg
  NodeMonitorInfo.msg
  MonitorInfo.msg
)

## Generate services in the 'srv' folder
add_service_files(
  FILES
  RegisterNode.srv
  UnregisterNode.srv
)

## Generate added messages and services with any dependencies listed here
generate_messages(
  DEPENDENCIES
  std_msgs
)

###################################
## catkin specific configuration ##
###################################
catkin_package(
  INCLUDE_DIRS include
  LIBRARIES monitor
  CATKIN_DEPENDS roscpp rospy std_msgs rosbag message_runtime
  DEPENDS Threads
)

###########
## Build ##
###########

## Specify additional locations of header files
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

## Declare a C++ library
add_library(monitor
  src/monitor/cpu_load_monitor.cpp
  src/monitor/cpu_softirq_monitor.cpp
  src/monitor/cpu_stat_monitor.cpp
  src/monitor/mem_monitor.cpp
  src/monitor/net_monitor.cpp
  src/monitor/process_monitor.cpp
  src/monitor/monitor_helper.cpp
  src/utils/read_file.cpp
)

## Add cmake target dependencies of the library
add_dependencies(monitor ${monitor_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS} ${PROJECT_NAME}_generate_messages_cpp)

## Declare a C++ executable
add_executable(monitor_node src/main.cpp)

## Add cmake target dependencies of the executable
add_dependencies(monitor_node ${monitor_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS} ${PROJECT_NAME}_generate_messages_cpp)

## Specify libraries to link against
target_link_libraries(monitor
  ${catkin_LIBRARIES}
  Threads::Threads
)

target_link_libraries(monitor_node
  monitor
  ${catkin_LIBRARIES}
  ${rosbag_LIBRARIES}
)

#############
## Install ##
#############

## Mark executables for installation
install(TARGETS monitor_node
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

## Mark libraries for installation
install(TARGETS monitor
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_GLOBAL_BIN_DESTINATION}
)

## Mark cpp header files for installation
install(DIRECTORY include/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.h"
)
