cmake_minimum_required(VERSION 2.8)
project(xio)

include("${COM_ROOT_PATH}/common.cmake")

set(XIO_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
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

message("## NOTE ##: project to build for [${XIO_TYPE}].")

#version文件
set(VERSION_FILE_PATH ${XIO_ROOT_PATH})

#版本
set(VERSION_NUM "xio_1.7.0_release")

set(VERSION_MSG "const char XIO_VERSION_STRING[] = \"${VERSION_NUM}\"\;")
file(WRITE ${VERSION_FILE_PATH}/version.c ${VERSION_MSG})

#设定源码
set(SOURCE_FILES ${VERSION_FILE_PATH}/version.c)
aux_source_directory(${TPYE_SRC}/transport SOURCE_FILES)
aux_source_directory(${TPYE_SRC}/transport/tcp SOURCE_FILES)
aux_source_directory(${TPYE_SRC}/xio SOURCE_FILES)
aux_source_directory(${XIO_COM} SOURCE_FILES)
 
#设定头文件路径
include_directories(${XIO_INC} 
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

 
#添加依赖项子目录

#动态库
add_library(xio SHARED ${SOURCE_FILES})
INSTALL(TARGETS xio LIBRARY DESTINATION ${LINK_LIB_PATH})
 

