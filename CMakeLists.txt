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

set(XIO_ROOT_PATH ${OPENSRC_PATH}/xio)
set(XIO_INC ${XIO_ROOT_PATH}/include)
set(XIO_COM ${XIO_ROOT_PATH}/src/common)
set(XIO_XIO ${XIO_ROOT_PATH}/src/usr)
set(XIO_LINUX ${XIO_ROOT_PATH}/src/usr)
if(XIO_TYPE STREQUAL kernel)
	set(TPYE_SRC  ${XIO_ROOT_PATH}/src/kernel)
	set(XIO_OS ${XIO_ROOT_PATH}/src/libxio_os/linuxkernel)
else()
	set(XIO_TYPE "user")
	set(TPYE_SRC  ${XIO_ROOT_PATH}/src/usr)
	set(XIO_OS ${XIO_ROOT_PATH}/src/libxio_os/linuxapp)
endif()

#version文件
set(VERSION_FILE_PATH ${XIO_ROOT_PATH})

#版本
set(VERSION_NUM "xio_1.7.0_release")

set(VERSION_MSG "const char XIO_VERSION_STRING[] = \"${VERSION_NUM}\"\;")
file(WRITE ${VERSION_FILE_PATH}/version.c ${VERSION_MSG})

#设定源码
set(XIO_INCLUDE ${OPENSRC_PATH}/xio/include)
set(SRC_COMMON ${COM_SRC_PATH}/common)
set(SRC_SESSION ${COM_SRC_PATH}/session)
set(ARPC_INCLUDE ${COM_ROOT_PATH}/inc)

set(SOURCE_FILES "")
aux_source_directory(${SRC_COMMON} SOURCE_FILES)
aux_source_directory(${SRC_SESSION} SOURCE_FILES)

aux_source_directory(${TPYE_SRC}/transport SOURCE_FILES)
aux_source_directory(${TPYE_SRC}/transport/tcp SOURCE_FILES)
aux_source_directory(${TPYE_SRC}/xio SOURCE_FILES)
aux_source_directory(${XIO_COM} SOURCE_FILES)
aux_source_directory(${VERSION_FILE_PATH}/ SOURCE_FILES)

#设定头文件路径
include_directories(${ARPC_INCLUDE} 
					${XIO_INCLUDE} 
					${SRC_COMMON} 
					${SRC_SESSION}
					${XIO_INC} 
					${XIO_COM} 
					${XIO_COM}/sys 
					${XIO_OS}
					${XIO_XIO}
					${TPYE_SRC}/xio
					${TPYE_SRC}/transport
					${TPYE_SRC}/transport/tcp)
 
 
#设定链接库的路径（一般使用第三方非系统目录下的库）
set(LINK_LIB_PATH ${DPENDENCY_LIB_PATH})
LINK_DIRECTORIES(${LIBRARY_OUTPUT_PATH} ${LINK_LIB_PATH})
 
#动态库
add_library(arpc SHARED ${SOURCE_FILES})

# 使动态库和静态库同时存在
set_target_properties(arpc PROPERTIES CLEAN_DIRECT_OUTPUT 1)

INSTALL(TARGETS arpc LIBRARY DESTINATION ${LINK_LIB_PATH})

#子项目
add_subdirectory("${ARPC_DEMO_PATH}/client_file_send")
add_subdirectory("${ARPC_DEMO_PATH}/server_file_rev")
add_subdirectory("${ARPC_DEMO_PATH}/arpc_client_test")
add_subdirectory("${ARPC_DEMO_PATH}/arpc_server_test")