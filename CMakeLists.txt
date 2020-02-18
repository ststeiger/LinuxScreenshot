cmake_minimum_required(VERSION 3.15)
project(LinuxScreenshot C)

# set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD 11)

# https://pabloariasal.github.io/2018/02/19/its-time-to-do-cmake-right/
set(SOURCE_LIST "main.c" bmp.h)

# add_executable(LinuxScreenshot main.c)
add_executable(${PROJECT_NAME} ${SOURCE_LIST})

# https://stackoverflow.com/questions/40802280/linking-with-cmake-icc-and-pthreads-linux
# target must be declared before being used...  $PROJECT_NAME = LinuxScreenshot
# target_link_libraries(LinuxScreenshot pthread X11 Xfixes Xext )
target_link_libraries(${PROJECT_NAME} pthread X11 Xfixes Xext )
