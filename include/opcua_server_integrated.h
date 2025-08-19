#ifndef OPCUA_SERVER_INTEGRATED_H
#define OPCUA_SERVER_INTEGRATED_H


#include "common.h"
#include <open62541/server.h>
#include <open62541/plugin/log_stdout.h>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <map>
#include <cmath>
#include <set>  // Para std::set

// Forward declarations
class PACControlClient;

// ============== ESTRUCTURAS DE DATOS ==============

// Estructura para definir una propiedad de un TAG
struct TagProperty {
    std::string name;
    UA_Variant value;
};

// Estructuras para variables individuales
struct FloatVariable {
    std::string name;         // Nombre en OPC-UA
    std::string pac_tag;      // Nombre real del TAG en el PAC
    std::string description;  // Descripción
};

struct Int32Variable {
    std::string name;         // Nombre en OPC-UA
    std::string pac_tag;      // Nombre real del TAG en el PAC
    std::string description;  // Descripción
};

// Configuración de TAG
struct TagConfig {
    std::string name;
    std::string description;
    std::string value_table;
    std::string alarm_table;
    std::vector<std::string> variables;
    std::vector<std::string> alarms;
    std::vector<FloatVariable> float_variables;   // Variables float individuales del TAG
    std::vector<Int32Variable> int32_variables;   // Variables int32 individuales del TAG
};

// Configuración del PAC
struct PACConfig {
    std::string ip = "192.168.1.30";
    int port = 22001;
    int timeout_ms = 5000;
};

// Variable mapeada en OPC UA
struct OPCUAVariable {
    std::string full_name;        // ej: "TT_11001.PV"
    std::string tag_name;         // ej: "TT_11001"
    std::string var_name;         // ej: "PV"
    std::string table_name;       // ej: "TBL_TT_11001"
    std::string type;             // "float", "int32", "single_float", "single_int32"
    std::string pac_tag;          // Para variables individuales
    UA_NodeId nodeId;
    bool has_nodeId = false;
};

// Configuración dinámica completa
struct DynamicConfig {
    PACConfig pac_config;
    int opcua_port = 4840;
    int update_interval_ms = 2000;
    std::vector<TagConfig> tags;
    std::vector<OPCUAVariable> variables;
    std::vector<FloatVariable> global_float_variables;   // Variables float globales
    std::vector<Int32Variable> global_int32_variables;   // Variables int32 globales
};

// ============== VARIABLES GLOBALES DECLARADAS ==============

// extern bool server_running;
// extern UA_Server *server;
// extern std::atomic<bool> running;
// extern std::mutex serverMutex;
// extern std::unique_ptr<PACControlClient> pacClient;
// extern DynamicConfig config;

// ============== FUNCIONES PRINCIPALES ==============

// Funciones del ciclo de vida del servidor
void ServerInit();
UA_StatusCode runServer();
void cleanupAndExit();
bool getPACConnectionStatus();

// ============== FUNCIONES DE CONFIGURACIÓN ==============

// Carga configuración desde archivos JSON
bool loadConfigFromFiles();

// Crea mapeos dinámicos de variables desde configuración
void createDynamicMappings();

// Crea nodos OPC UA con capacidad de escritura
void createOPCUANodes();

// ============== FUNCIONES DE CREACIÓN DE NODOS ==============

// Crea nodo TAG con propiedades (solo lectura)
void addTagNodeWithProperties(UA_Server *server, 
                              const std::string &tagName, 
                              const std::vector<TagProperty> &properties);

// Crea nodo TAG con propiedades y capacidad de escritura
void addTagNodeWithPropertiesWritable(UA_Server *server, 
                                     const std::string &tagName, 
                                     const std::vector<TagProperty> &properties);

// ============== FUNCIONES DE ACTUALIZACIÓN DE DATOS ==============

// Hilo principal de actualización de datos desde PAC
void updateDataFromPAC();

// Actualiza variables individuales (float/int32)
void updateSingleVariables();

// Actualiza variables de una tabla específica
void updateTableVariables(const std::string& table_name, const std::vector<int>& indices);

// Actualiza variables float de tabla
void updateFloatVariables(const std::string& table_name, 
                         const std::vector<float>& values, 
                         int min_index);

// Actualiza variables int32 de tabla
void updateInt32Variables(const std::string& table_name, 
                         const std::vector<int32_t>& values, 
                         int min_index);

// ============== FUNCIONES DE CALIDAD Y ESTADO ==============

// Establece estado de calidad de una variable OPC UA
void setVariableQuality(const std::string& nodeId, UA_StatusCode quality);

// Marca todas las variables como "BAD" cuando no hay conexión PAC
void markAllVariablesBad();

// ============== FUNCIONES AUXILIARES ==============

// Obtiene índice de variable en tabla TBL_ANALOG_TAG



// Funciones para cargar configuración
bool loadConfigFromFiles();
void createDynamicMappings();
void updateSingleVariables();

// Funciones auxiliares para actualización de datos
int getVariableIndex(const string& varName);
void updateTableVariables(const string& table_name, const vector<int>& indices);
void updateFloatVariables(const string& table_name, const vector<float>& values, int min_index);
void updateInt32Variables(const string& table_name, const vector<int32_t>& values, int min_index);

// ============== CALLBACKS OPC UA ==============

// Callback para manejar escritura de variables OPC UA

static void writeValueCallback(UA_Server *server,
                              const UA_NodeId *sessionId,
                              void *sessionContext,
                              const UA_NodeId *nodeId,
                              void *nodeContext,
                              const UA_NumericRange *range,
                              const UA_DataValue *data);


#endif // OPCUA_SERVER_INTEGRATED_H