cmake_minimum_required(VERSION 3.28)
project(LinSys LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# the library: what the Linux app will consume, as WinSysLive does WinSys
add_library(LinSys STATIC
	LinSys/Process.cpp
	LinSys/ProcFs.cpp
	LinSys/ProcessInfo.cpp
	LinSys/SystemStats.cpp
	LinSys/Users.cpp
)
# consumers include it with the folder prefix: <LinSys/ProcessManager.h>
target_include_directories(LinSys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(LinSys PRIVATE -Wall -Wextra -Wpedantic)

add_executable(Tests
	Tests/Main.cpp
	Tests/ProcFsTests.cpp
	Tests/ProcessManagerTests.cpp
	Tests/ProcessTests.cpp
)
target_link_libraries(Tests PRIVATE LinSys)
target_compile_options(Tests PRIVATE -Wall -Wextra)

add_executable(linsyscon LinSysCon/Main.cpp)
target_link_libraries(linsyscon PRIVATE LinSys)
target_compile_options(linsyscon PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME Tests COMMAND Tests -Live)
add_test(NAME LiveTests COMMAND Tests Live)
