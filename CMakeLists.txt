cmake_minimum_required(VERSION 3.10)
project(pac_to_opcua)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🔧 BUSCAR OPEN62541 (después de reinstalación correcta)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPEN62541 REQUIRED open62541)

# Incluir directorios
include_directories(include)
include_directories(${OPEN62541_INCLUDE_DIRS})

# Archivos fuente
set(SOURCES
    src/main.cpp
    src/opcua_server.cpp
    src/pac_control_client.cpp
)

# Crear ejecutable
add_executable(${PROJECT_NAME} ${SOURCES})

# Enlazar librerías
target_link_libraries(${PROJECT_NAME} 
    ${OPEN62541_LIBRARIES}
    pthread
    m
    dl
    rt
)

# RPATH para encontrar librerías
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "/usr/local/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
)

message(STATUS "Open62541 libraries: ${OPEN62541_LIBRARIES}")
message(STATUS "Open62541 include dirs: ${OPEN62541_INCLUDE_DIRS}")