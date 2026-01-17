cmake_minimum_required(VERSION 3.0.2)
project(llamacpp-ros)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
## Compile as C++11, supported in ROS Kinetic and newer
# add_compile_options(-std=c++11)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED COMPONENTS
  roscpp
  rospy
  std_msgs
)


catkin_package(
#  INCLUDE_DIRS include
#  LIBRARIES llamacpp-ros
#  CATKIN_DEPENDS roscpp rospy std_msgs
#  DEPENDS system_lib
)

###########
## Build ##
###########

## Specify additional locations of header files
## Your package locations should be listed before other locations



include_directories(
  ${catkin_INCLUDE_DIRS}
  ./server
  ./common
  ./vendor  # server-context 头文件路径
)

add_executable(llamacpp-ros 
  src/cli.cpp
  src/StreamTaskDispatcher.cpp
)



# Set RPATH so shared libraries can be found at runtime

get_filename_component(PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
set(LLAMA_LIB_DIR ${PROJECT_DIR}/lib)

set_target_properties(llamacpp-ros PROPERTIES
  BUILD_RPATH "${LLAMA_LIB_DIR}"
  INSTALL_RPATH "${LLAMA_LIB_DIR}"
  INSTALL_RPATH_USE_LINK_PATH FALSE  # Prevent catkin from adding link paths to RPATH
)

message(STATUS "LLAMA_LIB_DIR set to: ${LLAMA_LIB_DIR}")

target_link_libraries(llamacpp-ros
  ${catkin_LIBRARIES}        # ROS 库（无依赖，最前）
${LLAMA_LIB_DIR}/libserver-context.a 
${LLAMA_LIB_DIR}/libcommon.a 
${LLAMA_LIB_DIR}/libcpp-httplib.a  # httplib for download functionality
-pthread 
/usr/lib/x86_64-linux-gnu/libcurl.so 
${LLAMA_LIB_DIR}/libmtmd.so
${LLAMA_LIB_DIR}/libllama.so
${LLAMA_LIB_DIR}/libggml.so
${LLAMA_LIB_DIR}/libggml-cpu.so
${LLAMA_LIB_DIR}/libggml-base.so
  m                          # 数学库
  dl                         # 动态链接库（GGML 必需）
  "-Wl,-rpath,${LLAMA_LIB_DIR}"  # Explicitly set RPATH to local library directory
)

get_filename_component(OFFLINE_AGENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
install(TARGETS llamacpp-ros
  RUNTIME DESTINATION ${OFFLINE_AGENT_DIR}/install/bin
)
