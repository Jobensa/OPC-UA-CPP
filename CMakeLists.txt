cmake_minimum_required(VERSION 3.10)
project(pac_to_opcua)

set(CMAKE_CXX_STANDARD 17)

# Buscar open62541
find_package(open62541 REQUIRED)

# Buscar nlohmann_json
find_package(nlohmann_json REQUIRED)

# Directorios de headers
include_directories(include)

# Archivos fuente
set(SOURCES
    src/main.cpp
    src/opcua_server.cpp
    src/pac_control_client.cpp
   # src/write_registration_manager.cpp  # ← AGREGAR ESTA LÍNEA
)

# Crear ejecutable
add_executable(${PROJECT_NAME} ${SOURCES})

# Enlazar librerías
target_link_libraries(${PROJECT_NAME} 
    open62541::open62541
    nlohmann_json::nlohmann_json
    pthread
)

message(STATUS "Open62541 libraries: ${OPEN62541_LIBRARIES}")
message(STATUS "Open62541 include dirs: ${OPEN62541_INCLUDE_DIRS}")