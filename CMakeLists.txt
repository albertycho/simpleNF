cmake_minimum_required(VERSION 2.8)

set(CMAKE_VERBOSE_MAKEFILE on)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    #top level project.
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
    set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format -g")
    set(CMAKE_C_FLAGS "-std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-format -g")
    set(REL_PATH "../build")
    # Sets mica value size
    #set(MICA_VSIZE_USER "492" CACHE STRING "Value size to define in MICA code. Does NOT take into account key+opcode overhead")
    set(MICA_VSIZE_USER "512" CACHE STRING "Value size to define in MICA code. Does NOT take into account key+opcode overhead")
else()
    #i am called from a higher subdirectory
    set(REL_PATH ${CMAKE_SOURCE_DIR}/build)
    #TODO: set this is a relative variable passed from CMAKE_SOURCE_DIR??
endif()

option(OPT_MEASUREMENT "Build all code for measurements")
option(BUILD_ZSIM "Build all code for ZSIM mode")

if(BUILD_ZSIM)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DZSIM -DDEBUG")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DZSIM -DDEBUG")
endif()

if(OPT_MEASUREMENT)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

set(LIBRARIES ${LIBRARIES} rt pthread)

#set(LIBZSIMNIC_DIR /nethome/acho44/zsim/zSim/src/libzsim)
#set(LIBZSIMNIC_DIR /nethome/acho44/zsim/zSim/src/libzsim PARENT_SCOPE)


set(LIBZSIMNIC_DIR /nethome/acho44/zsim/simpleNF/libzsim)
set(LIBZSIMNIC_DIR /nethome/acho44/zsim/simpleNF/libzsim PARENT_SCOPE)


set(LIBZSIMNIC_SOURCES
    ${LIBZSIMNIC_DIR}/zsim_nic_defines.cpp
    )

### TODO:wat does this do?
set(BENCHMARKS
	simpNF)
	#herd
	#mica_test)



set(RPC_SHM_SOURCES
    rpc_shm/cbuf.cpp
    rpc_shm/rpc.cpp
    )


# EDIT: LIBSONUMA_DIR comes from the son-common CMake now (/path/to/son-base/son-common/CMakeLists.txt)

#add_executable(herd herd/main.c herd/worker.c herd/client.c herd/master.c)
#add_executable(herd herd/main.c herd/worker.c herd/master.c)
#add_executable(mica_test mica/test.c)
add_executable(simpNF simpNF/main.c simpNF/worker.c)

if(BUILD_STATIC)
#add_library(mica STATIC ${MICA_SOURCES}) 
add_library(rpc_shm STATIC ${RPC_SHM_SOURCES})
add_library(rpc_shm STATIC ${RPC_SHM_SOURCES})
add_library(zsim_nic STATIC ${LIBZSIMNIC_SOURCES})
else()
add_library(rpc_shm SHARED ${RPC_SHM_SOURCES})
add_library(zsim_nic SHARED ${LIBZSIMNIC_SOURCES})
endif()
#target_compile_options(mica PUBLIC -D_GNU_SOURCE -DMICA_VALUE_SIZE_CMAKE=${MICA_VSIZE_USER}) # For sys/ipc.h, and setting dynamic value sizes from config
#target_compile_options(herd PUBLIC -DMICA_VALUE_SIZE_CMAKE=${MICA_VSIZE_USER}) # For sys/ipc.h, and setting dynamic value sizes
target_compile_options(simpNF PUBLIC -DMICA_VALUE_SIZE_CMAKE=${MICA_VSIZE_USER}) # For sys/ipc.h, and setting dynamic value sizes
target_compile_options(rpc_shm PUBLIC -D_GNU_SOURCE) # For sys/ipc.h
#target_include_directories(mica PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mica ${LIBSONUMA_DIR})
#target_include_directories(rpc_shm PUBLIC ${LIBSONUMA_DIR})
#target_include_directories(mica PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mica ${LIBZSIMNIC_DIR})
target_include_directories(rpc_shm PUBLIC ${LIBZSIMNIC_DIR})
target_include_directories(zsim_nic PUBLIC ${LIBZSIMNIC_DIR})

target_link_libraries(zsim_nic ${LIBRARIES})
target_compile_options(zsim_nic PUBLIC -D_GNU_SOURCE) # For sys/ipc.h
target_include_directories(zsim_nic PUBLIC ${LIBZSIMNIC_DIR})

# For herd benchmark
#add_dependencies(herd rpc_shm)
add_dependencies(simpNF rpc_shm)
#target_link_libraries(rpc_shm sonuma)
target_link_libraries(rpc_shm zsim_nic)
#target_link_libraries(herd ${LIBRARIES} mica rpc_shm)
target_link_libraries(simpNF ${LIBRARIES}  rpc_shm)
target_include_directories(simpNF PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rpc_shm)

# For MICA test
#target_link_libraries(mica_test ${LIBRARIES} sonuma mica)
#target_link_libraries(mica_test ${LIBRARIES} zsim_nic mica)
#target_include_directories(mica_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mica)





############ raze and build cmd:
#cmake -DBUILD_ZSIM=1 -DOPT_MEASUREMENT=1 ..
