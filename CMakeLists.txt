cmake_minimum_required(VERSION 3.28)
project(LinSys LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
	if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
		set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
	endif()
endif()

# the library: what LinSysLive consumes (as a submodule), as WinSysLive does WinSys
add_library(LinSys STATIC
	LinSys/Process.cpp
	LinSys/ProcFs.cpp
	LinSys/ProcessInfo.cpp
	LinSys/Services.cpp
	LinSys/SystemStats.cpp
	LinSys/Connections.cpp
	LinSys/Disks.cpp
	LinSys/ProcessDetails.cpp
	LinSys/Users.cpp
)
# consumers include it with the folder prefix: <LinSys/ProcessManager.h>
target_include_directories(LinSys PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(LinSys PUBLIC cxx_std_23)
target_compile_options(LinSys PRIVATE -Wall -Wextra -Wpedantic)
# systemd's services over D-Bus: sd-bus, from libsystemd (libsystemd-dev)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
target_link_libraries(LinSys PRIVATE PkgConfig::SYSTEMD)

# the tests and the console tool only when LinSys is built by itself, not as a submodule
if(PROJECT_IS_TOP_LEVEL)
	add_executable(Tests
		Tests/Main.cpp
		Tests/ConnectionsTests.cpp
		Tests/ProcFsTests.cpp
		Tests/ProcessManagerTests.cpp
		Tests/ProcessDetailsTests.cpp
		Tests/ProcessTests.cpp
		Tests/ServicesTests.cpp
		Tests/SystemStatsTests.cpp
	)
	target_link_libraries(Tests PRIVATE LinSys)
	target_compile_options(Tests PRIVATE -Wall -Wextra)

	add_executable(linsyscon LinSysCon/Main.cpp)
	target_link_libraries(linsyscon PRIVATE LinSys)
	target_compile_options(linsyscon PRIVATE -Wall -Wextra)

	enable_testing()
	add_test(NAME Tests COMMAND Tests -Live)
	add_test(NAME LiveTests COMMAND Tests Live)
endif()
