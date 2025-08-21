#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

#include "common.h"
#include <open62541/server.h>
#include <vector>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <map>

// Forward declarations
class PACControlClient;

// ============== ESTRUCTURAS SIMPLIFICADAS ==============

// Variable simple unificada
struct Variable {
    std::string opcua_name;      // Nombre completo en OPC-UA (ej: "TT_11001.PV")
    std::string tag_name;        // Nombre del TAG (ej: "TT_11001")
    std::string var_name;        // Nombre de variable (ej: "PV")
    std::string pac_source;      // Fuente en PAC: tabla+índice o tag directo
    enum Type { FLOAT, INT32, SINGLE_FLOAT, SINGLE_INT32 } type;
    bool writable = false;       // Si se puede escribir
    bool has_node = false;       // Si ya se creó el nodo OPC-UA
};

// Configuración del TAG
struct Tag {
    std::string name;
    std::string value_table;
    std::string alarm_table;
    std::vector<std::string> variables;
    std::vector<std::string> alarms;
};

// Configuración simplificada
struct Config {
    std::string pac_ip = "192.168.1.30";
    int pac_port = 22001;
    int opcua_port = 4840;
    int update_interval_ms = 2000;
    std::string server_name = "PAC Control SCADA Server";
    std::vector<Tag> tags;
    std::vector<Variable> variables;
};

// ============== FUNCIONES PRINCIPALES ==============

// Funciones del ciclo de vida del servidor
void ServerInit();
UA_StatusCode runServer();
void cleanupAndExit();
bool getPACConnectionStatus();

// Funciones de configuración y datos
bool loadConfig();
void createNodes();
void updateData();

// Funciones auxiliares
int getVariableIndex(const std::string& varName);
bool isWritableVariable(const std::string& varName);

#endif // OPCUA_SERVER_H