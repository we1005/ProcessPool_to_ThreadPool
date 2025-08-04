cmake_minimum_required(VERSION 3.16)
project(ProcessPool VERSION 2.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 编译选项
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG -Wall -Wextra -Wpedantic -Wformat=2 -Wno-unused-parameter")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG")

# 默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()

# 选项
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_BENCHMARKS "Build benchmark programs" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_LTO "Enable Link Time Optimization" OFF)

# 查找依赖
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

# 检查系统特性
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)

check_include_file("sys/epoll.h" HAVE_EPOLL)
check_include_file("sys/eventfd.h" HAVE_EVENTFD)
check_include_file("sys/signalfd.h" HAVE_SIGNALFD)
check_include_file("sys/timerfd.h" HAVE_TIMERFD)
check_include_file("sys/pidfd.h" HAVE_PIDFD)
check_function_exists("epoll_create1" HAVE_EPOLL_CREATE1)
check_function_exists("eventfd" HAVE_EVENTFD_FUNC)
check_function_exists("signalfd" HAVE_SIGNALFD_FUNC)
check_function_exists("timerfd_create" HAVE_TIMERFD_CREATE)
check_function_exists("pidfd_open" HAVE_PIDFD_OPEN)
check_symbol_exists("EPOLL_CLOEXEC" "sys/epoll.h" HAVE_EPOLL_CLOEXEC)
check_symbol_exists("EFD_CLOEXEC" "sys/eventfd.h" HAVE_EFD_CLOEXEC)
check_symbol_exists("SFD_CLOEXEC" "sys/signalfd.h" HAVE_SFD_CLOEXEC)
check_symbol_exists("TFD_CLOEXEC" "sys/timerfd.h" HAVE_TFD_CLOEXEC)

# 检查原子操作支持
check_include_file("stdatomic.h" HAVE_STDATOMIC_H)
if(NOT HAVE_STDATOMIC_H)
    message(FATAL_ERROR "stdatomic.h is required but not found")
endif()

# 检查必需的Linux特性
if(NOT HAVE_EPOLL)
    message(FATAL_ERROR "epoll is required but not found")
endif()

if(NOT HAVE_EVENTFD)
    message(FATAL_ERROR "eventfd is required but not found")
endif()

# 生成配置头文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/config.h"
)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# 源文件
set(PROCESS_POOL_SOURCES
    src/core/pool_manager.c
    src/core/worker.c
    src/core/task_manager.c
    src/core/event_loop.c
    src/ipc/lockfree_queue.c
    src/ipc/shared_memory.c
    src/ipc/eventfd_utils.c
    src/utils/utils.c
    src/utils/metrics.c
)

# 头文件
set(PROCESS_POOL_HEADERS
    include/process_pool.h
    include/internal.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/config.h
)

# 创建库
if(BUILD_SHARED_LIBS)
    add_library(processpool SHARED ${PROCESS_POOL_SOURCES})
    set_target_properties(processpool PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${PROCESS_POOL_HEADERS}"
    )
else()
    add_library(processpool STATIC ${PROCESS_POOL_SOURCES})
    set_target_properties(processpool PROPERTIES
        PUBLIC_HEADER "${PROCESS_POOL_HEADERS}"
    )
endif()

# 链接库
target_link_libraries(processpool
    PUBLIC
        Threads::Threads
    PRIVATE
        rt  # for shm_open, sem_open
        m   # for math functions
)

# 编译器特定选项
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(processpool PRIVATE
        -fPIC
        -fvisibility=hidden
        -fstack-protector-strong
    )
    
    if(ENABLE_LTO)
        set_property(TARGET processpool PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# 调试和分析工具
if(ENABLE_ASAN)
    target_compile_options(processpool PRIVATE -fsanitize=address)
    target_link_options(processpool PRIVATE -fsanitize=address)
endif()

if(ENABLE_TSAN)
    target_compile_options(processpool PRIVATE -fsanitize=thread)
    target_link_options(processpool PRIVATE -fsanitize=thread)
endif()

if(ENABLE_UBSAN)
    target_compile_options(processpool PRIVATE -fsanitize=undefined)
    target_link_options(processpool PRIVATE -fsanitize=undefined)
endif()

if(ENABLE_COVERAGE)
    target_compile_options(processpool PRIVATE --coverage)
    target_link_options(processpool PRIVATE --coverage)
endif()

# 安装配置
include(GNUInstallDirs)

install(TARGETS processpool
    EXPORT ProcessPoolTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/processpool
)

# 安装CMake配置文件
install(EXPORT ProcessPoolTargets
    FILE ProcessPoolTargets.cmake
    NAMESPACE ProcessPool::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProcessPool
)

# 生成配置文件
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProcessPoolConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ProcessPoolConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProcessPool
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ProcessPoolConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ProcessPoolConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ProcessPoolConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProcessPool
)

# pkg-config文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/processpool.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/processpool.pc"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/processpool.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# 测试
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 示例
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 基准测试
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# 文档
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

# 打包
set(CPACK_PACKAGE_NAME "processpool")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern Linux Process Pool Library")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "ProcessPool Developers")

set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\.git/"
    "/build/"
    "/\\.vscode/"
    "/\\.idea/"
    "\\.*~$"
)

if(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    
    # DEB包配置
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libpthread-stubs0-dev")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    
    # RPM包配置
    set(CPACK_RPM_PACKAGE_GROUP "System Environment/Libraries")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17")
endif()

include(CPack)

# 显示配置摘要
message(STATUS "")
message(STATUS "ProcessPool Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  Shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  Examples: ${BUILD_EXAMPLES}")
message(STATUS "  Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  AddressSanitizer: ${ENABLE_ASAN}")
message(STATUS "  ThreadSanitizer: ${ENABLE_TSAN}")
message(STATUS "  UBSanitizer: ${ENABLE_UBSAN}")
message(STATUS "  Coverage: ${ENABLE_COVERAGE}")
message(STATUS "  LTO: ${ENABLE_LTO}")
message(STATUS "")
message(STATUS "System Features:")
message(STATUS "  epoll: ${HAVE_EPOLL}")
message(STATUS "  eventfd: ${HAVE_EVENTFD}")
message(STATUS "  signalfd: ${HAVE_SIGNALFD}")
message(STATUS "  timerfd: ${HAVE_TIMERFD}")
message(STATUS "  pidfd: ${HAVE_PIDFD}")
message(STATUS "")