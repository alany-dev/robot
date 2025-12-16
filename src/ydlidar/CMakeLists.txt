cmake_minimum_required(VERSION 2.8.3)
project(ydlidar)

add_definitions(-std=c++14) # Use C++14

find_package(catkin REQUIRED COMPONENTS
  rosconsole
  roscpp
  sensor_msgs
  shm_transport
  std_msgs
)

#add_subdirectory(sdk)

#find ydlidar_sdk package
find_package(ydlidar_sdk REQUIRED)



catkin_package()


#link library directories
link_directories(${YDLIDAR_SDK_LIBRARY_DIRS})

include_directories(
  ${catkin_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}/src
  ${YDLIDAR_SDK_INCLUDE_DIRS}

)

add_executable(ydlidar_node src/ydlidar_node.cpp  )
add_executable(ydlidar_client src/ydlidar_client.cpp)

add_executable(shm_ydlidar_node src/shm_ydlidar_node.cpp)
add_executable(shm_ydlidar_client src/shm_ydlidar_client.cpp)

add_executable(ydlidar_node_v2 
  src/ydlidar_node_v2.cpp
  src/ydlidar_adapter.cpp
  src/lidar_factory.cpp
  src/lidar_config_loader.cpp
)

target_link_libraries(ydlidar_node
   ${catkin_LIBRARIES} 
   ${YDLIDAR_SDK_LIBRARIES}
 )
target_link_libraries(ydlidar_client
   ${catkin_LIBRARIES} 
   ${YDLIDAR_SDK_LIBRARIES}
 )
target_link_libraries(shm_ydlidar_node
   ${catkin_LIBRARIES} 
   ${YDLIDAR_SDK_LIBRARIES}
   shm_transport
   -lrt
 )
target_link_libraries(shm_ydlidar_client
   ${catkin_LIBRARIES} 
   ${YDLIDAR_SDK_LIBRARIES}
   shm_transport
   -lrt
 )
target_link_libraries(ydlidar_node_v2
   ${catkin_LIBRARIES} 
   ${YDLIDAR_SDK_LIBRARIES}
 )

install(TARGETS ydlidar_node ydlidar_client shm_ydlidar_node shm_ydlidar_client ydlidar_node_v2
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)


install(DIRECTORY launch startup
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
  USE_SOURCE_PERMISSIONS
)


