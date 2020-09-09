###############################################################
#*【项目】CA
#*【描述】
#*【作者】hongchunhua
#*【时间】2020.07.22
###############################################################

cmake_minimum_required(VERSION 2.8)
project(arpc)

set(COM_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

#设置依赖的文件路径
set(ARPC_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

	#开源
set(ARPC_OPENSRC_PATH "${ARPC_ROOT_PATH}/open_src")

set(ARPC_DEMO_PATH "${ARPC_ROOT_PATH}/demo")

include("${COM_ROOT_PATH}/common.cmake")

#设定源码
set(XIO_INCLUDE ${OPENSRC_PATH}/xio/include)
set(SRC_COMMON ${COM_SRC_PATH}/common)
set(SRC_SESSION ${COM_SRC_PATH}/session)
set(ARPC_INCLUDE ${COM_ROOT_PATH}/inc)

set(SOURCE_FILES "")
aux_source_directory(${SRC_COMMON} SOURCE_FILES)
aux_source_directory(${SRC_SESSION} SOURCE_FILES)

 
#设定头文件路径
include_directories(${ARPC_INCLUDE} ${XIO_INCLUDE} ${SRC_COMMON} ${SRC_SESSION})
 
 
#设定链接库的路径（一般使用第三方非系统目录下的库）
set(LINK_LIB_PATH ${DPENDENCY_LIB_PATH})
LINK_DIRECTORIES(${LIBRARY_OUTPUT_PATH} ${LINK_LIB_PATH})
 

#动态库
add_library(arpc SHARED ${SOURCE_FILES})
target_link_libraries(arpc 
 "-Wl,--whole-archive" #之后的库使用--whole-archive选项
 xio
"-Wl,--no-whole-archive")  #之后的库不使用--whole-archive选项

# 生成静态库目标
add_library(arpc_static STATIC ${SOURCE_FILES})
target_link_libraries(arpc_static 
 "-Wl,--whole-archive" #之后的库使用--whole-archive选项
 xio
"-Wl,--no-whole-archive")  #之后的库不使用--whole-archive选项

# 指定静态库的输出名称
set_target_properties(arpc_static PROPERTIES OUTPUT_NAME "arpc")

# 使动态库和静态库同时存在
set_target_properties(arpc PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(arpc_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)

INSTALL(TARGETS arpc LIBRARY DESTINATION ${LINK_LIB_PATH})

#子项目
add_subdirectory("${ARPC_OPENSRC_PATH}/xio/")
add_subdirectory("${ARPC_DEMO_PATH}/client_file_send")
add_subdirectory("${ARPC_DEMO_PATH}/server_file_rev")