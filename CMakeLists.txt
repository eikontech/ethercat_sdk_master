# Copyright 2020 Robotic Systems Lab - ETH Zurich:
# Lennart Nachtigall, Jonas Junger
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

# 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.5)
project(ethercat_sdk_master)

# Add support for C++11
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(message_logger REQUIRED)
find_package(soem_interface REQUIRED)
find_package(soem_ros2 REQUIRED)

set(INCLUDE_DIRS include ${ament_cmake_INCLUDE_DIRS}
  ${message_logger_INCLUDE_DIRS}
  ${soem_interface_INCLUDE_DIRS})
include_directories(${INCLUDE_DIRS})

message(STATUS "INCLUDE_DIRS - ${INCLUDE_DIRS}")

set(LIBRARY_DIRS ${ament_cmake_LIBRARY_DIRS}
  ${message_logger_LIBRARY_DIRS}
  ${soem_interface_LIBRARY_DIRS})
link_directories(${LIBRARY_DIRS})

message(STATUS "LIBRARY_DIRS - ${LIBRARY_DIRS}")

set(LIBS 
  ${ament_cmake_LIBRARIES} 
  ${soem_interface_LIBRARIES}
  ${message_logger_LIBRARIES}
  ${soem_ros2_LIBRARIES}
  )

message(STATUS "LIBS - ${LIBS}")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  message(STATUS "Using ccache")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

## Enable all compiler warnings and treat them as errors.
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads)

###################################
## catkin specific configuration ##
###################################

###########
## Build ##
###########

add_library(${PROJECT_NAME} 
  src/${PROJECT_NAME}/EthercatMaster.cpp
  src/${PROJECT_NAME}/EthercatDevice.cpp)

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

ament_target_dependencies(${PROJECT_NAME} rclcpp)
target_link_libraries(${PROJECT_NAME} ${LIBS} ${YAML_CPP_LIBRARIES}
  Threads::Threads)

#############
## Install ##
#############
install(TARGETS ${PROJECT_NAME}
  ARCHIVE
  DESTINATION lib
  LIBRARY
  DESTINATION lib
  RUNTIME
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION include/${PROJECT_NAME})

ament_export_dependencies(ament_cmake)
ament_export_dependencies(soem_interface message_logger)
ament_export_include_directories(${INCLUDE_DIRS})
ament_export_libraries(${PROJECT_NAME} ${LIBS})

ament_package()
