# zlog 多线程多源文件示例 — CMakeLists.txt
#
# 用法（在 zlog 源码树内构建）：
#   cmake -DBUILD_EXAMPLES=ON -B build
#   cmake --build build -j4
#   cd build/bin && ./zlog_multi_thread_example
#
# 用法（独立构建，使用系统已安装的 zlog）：
#   cmake -DUSE_SYSTEM_ZLOG=ON -B build
#   cmake --build build -j4

cmake_minimum_required(VERSION 3.12)
project(zlog_multi_thread_example C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wstrict-prototypes -g")

# -----------------------------------------------------------------------
# 选择 zlog 来源：源码树内目标 vs 系统安装
# -----------------------------------------------------------------------
option(USE_SYSTEM_ZLOG "Link against system-installed zlog" OFF)

if (USE_SYSTEM_ZLOG)
    find_library(ZLOG_LIB zlog REQUIRED)
    find_path(ZLOG_INCLUDE_DIR zlog.h REQUIRED)
    message(STATUS "Using system zlog: lib=${ZLOG_LIB}  include=${ZLOG_INCLUDE_DIR}")
else ()
    # 从源码树根目录构建时，zlog 目标由父项目的 add_subdirectory(src) 提供
    set(ZLOG_LIB zlog)
    set(ZLOG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
    message(STATUS "Using in-tree zlog target")
endif ()

include_directories(${ZLOG_INCLUDE_DIR})

# -----------------------------------------------------------------------
# 可执行目标
# -----------------------------------------------------------------------
set(EXAMPLE_SRCS
    main.c
    network.c
    database.c
    worker.c
)

add_executable(zlog_multi_thread_example ${EXAMPLE_SRCS})
target_link_libraries(zlog_multi_thread_example ${ZLOG_LIB} pthread)

# 将 zlog.conf 复制到可执行文件所在目录，程序运行时在当前目录读取该文件
add_custom_command(TARGET zlog_multi_thread_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/zlog.conf"
        "$<TARGET_FILE_DIR:zlog_multi_thread_example>/zlog.conf"
    COMMENT "Copying zlog.conf next to the example binary"
)
