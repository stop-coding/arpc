###############################################################
#*【项目】CA
#*【描述】
#*【作者】hongchunhua
#*【时间】2020.07.22
###############################################################

	#依赖库
set(DPENDENCY_LIB_PATH "${COM_ROOT_PATH}/lib/")
	#开源
set(OPENSRC_PATH "${COM_ROOT_PATH}/open_src")

	#APP 应用目录
set(COM_APP_PATH "${COM_ROOT_PATH}/demo")
	#源码目录
set(COM_SRC_PATH "${COM_ROOT_PATH}/src")

#设定编译参数
if (DEFINED CLANG)
	SET (CMAKE_C_COMPILER             "/usr/bin/clang")
	SET (CMAKE_C_FLAGS                "-Wall -std=c99")
	SET (CMAKE_C_FLAGS_DEBUG          "-g")
	SET (CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
	SET (CMAKE_C_FLAGS_RELEASE        "-O4 -DNDEBUG")
	SET (CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")

	SET (CMAKE_CXX_COMPILER             "/usr/bin/clang++")
	SET (CMAKE_CXX_FLAGS                "-Wall")
	SET (CMAKE_CXX_FLAGS_DEBUG          "-g")
	SET (CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
	SET (CMAKE_CXX_FLAGS_RELEASE        "-O4 -DNDEBUG")
	SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

	SET (CMAKE_AR      "/usr/bin/llvm-ar")
	SET (CMAKE_LINKER  "/usr/bin/llvm-ld")
	SET (CMAKE_NM      "/usr/bin/llvm-nm")
	SET (CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
	SET (CMAKE_RANLIB  "/usr/bin/llvm-ranlib")
else()
	set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
	set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
	set(CMAKE_C_FLAGS_DEBUG "$ENV{CFLAGS} -O0 -Wall -g -ggdb3 -Werror -Wdeclaration-after-statement")
	set(CMAKE_C_FLAGS_RELEASE "$ENV{CFLAGS} -O3 -Wall")

	if (CMAKE_BUILD_TYPE STREQUAL Release)
		message("NOTE: project to build on [Release] version.")
		set(CMAKE_BUILD_TYPE "Release")
		set(DEBUG_FLAG ${CMAKE_C_FLAGS_RELEASE})
	else()
		message("WARNING: project to build on [Debug] version.")
		set(CMAKE_BUILD_TYPE "Debug")
		set(DEBUG_FLAG ${CMAKE_C_FLAGS_DEBUG})
	endif()
	SET(CA_WARNINGS_SETTING "-Wno-missing-field-initializers -Wno-deprecated -fno-omit-frame-pointer -Wno-unused-parameter -Wno-deprecated-declarations -Wno-unused-function -Wno-unused-variable")
	SET(C_CPP_FLAGS_ "${C_CPP_FLAGS_} -DPIC -fPIC ${DEBUG_FLAG} -D_GNU_SOURCE -DUSE_COMMON_LIB ${OS_FLAG} ${CA_WARNINGS_SETTING}")

	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_CPP_FLAGS_}")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_CPP_FLAGS_}")
endif()

#设置输出路径
SET(EXECUTABLE_OUTPUT_PATH ${COM_ROOT_PATH}/${CMAKE_BUILD_TYPE}_build_out/bin)       #设置可执行文件的输出目录
SET(LIBRARY_OUTPUT_PATH ${COM_ROOT_PATH}/${CMAKE_BUILD_TYPE}_build_out/lib)           #设置库文件的输出目录

message("--cur path: ${CMAKE_CURRENT_SOURCE_DIR}")
message("--project : ${PROJECT_NAME}")
message("--out path: ${COM_ROOT_PATH}/${CMAKE_BUILD_TYPE}_build_out.")