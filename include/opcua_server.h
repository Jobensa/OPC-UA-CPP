#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

#include "common.h"
#include <open62541/server.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>
#include <nlohmann/json.hpp>  // 🔧 AGREGAR ESTA LÍNEA

using namespace std;
using json = nlohmann::json;  // 🔧 AGREGAR ESTA LÍNEA

// Forward declarations
class PACControlClient;

// ============== SOLO DECLARACIONES DE FUNCIONES ==============

// Variables globales extern (definidas en opcua_server.cpp)
extern std::atomic<bool> running;
extern std::atomic<bool> server_running;
extern std::atomic<bool> updating_internally;
extern std::atomic<bool> server_writing_internally;

// Funciones del ciclo de vida del servidor
bool ServerInit();
UA_StatusCode runServer();
void shutdownServer();
void cleanupServer();
bool isServerRunning();
void runServerIteration();

// Funciones de configuración y datos
bool loadConfig(const string& configFile);
void processConfigIntoVariables();
void createNodes();
void updateData();
bool processConfigFromJson(const json& configJson);  // 🔧 AHORA FUNCIONARÁ

// Funciones específicas para procesar diferentes tipos
void processSimpleVariables();
void processTBLTags();
void processAPITags();

// Funciones auxiliares
int getVariableIndex(const std::string& varName);
bool isWritableVariable(const std::string& varName);
bool getPACConnectionStatus();
void cleanupAndExit();

// Funciones auxiliares específicas
int getAPIVariableIndex(const std::string &varName);
int getBatchVariableIndex(const std::string &varName);

// 🔧 AGREGAR DECLARACIÓN DEL CALLBACK
static UA_StatusCode writeCallback(UA_Server *server,
                                  const UA_NodeId *sessionId,
                                  void *sessionContext,
                                  const UA_NodeId *nodeId,
                                  void *nodeContext,
                                  const UA_NumericRange *range,
                                  const UA_DataValue *data);

#endif // OPCUA_SERVER_H