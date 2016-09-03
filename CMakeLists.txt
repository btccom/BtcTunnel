cmake_minimum_required (VERSION 2.6)

project (BTCTunnel)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

#
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Release ..
#
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
IF(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
  set(CMAKE_CXX_FLAGS "-g -O0 -Wall")
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
  set(CMAKE_CXX_FLAGS "-O2 -Wall")
ELSE()
  message("Some other build type.")
ENDIF()


SET(CMAKE_CXX_COMPILER "g++")
SET(CMAKE_C_COMPILER   "gcc")
SET(CMAKE_CXX_COMPILER_ARG1 "-std=c++0x")


find_package(Glog)
if(NOT GLOG_FOUND)
  message(FATAL_ERROR "Glog not found!")
endif(NOT GLOG_FOUND)

find_package(LibEvent)
if(NOT LibEvent_FOUND)
  message(FATAL_ERROR "libevent2 not found!")
endif(NOT LibEvent_FOUND)

include_directories(src test ${GLOG_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIR})
set(THRID_LIBRARIES -lpthread ${GLOG_LIBRARIES} ${LIBEVENT_LIB})

file(GLOB LIB_SOURCES src/*.cc src/*.c)
add_library(btctunnel STATIC ${LIB_SOURCES})

file(GLOB_RECURSE TEST_SOURCES test/*.cc)
add_executable(unittest ${TEST_SOURCES})
target_link_libraries(unittest btctunnel ${THRID_LIBRARIES})

file(GLOB_RECURSE CLIENT_SOURCES src/client/*.cc)
add_executable(tclient ${CLIENT_SOURCES})
target_link_libraries(tclient btctunnel ${THRID_LIBRARIES})

file(GLOB_RECURSE SERVER_SOURCES src/server/*.cc)
add_executable(tserver ${SERVER_SOURCES})
target_link_libraries(tserver btctunnel ${THRID_LIBRARIES})
