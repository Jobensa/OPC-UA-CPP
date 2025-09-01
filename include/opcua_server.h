#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

#include "common.h"
#include <open62541/server.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>

using namespace std;
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
void cleanupServer();  // ✅ Asegurar que esté declarada
bool isServerRunning();
void runServerIteration();

// Funciones de configuración y datos
bool loadConfig(const string& configFile);
void processConfigIntoVariables();  // 🆕 NUEVA DECLARACIÓN
void createNodes();
void updateData();


// 🆕 Funciones específicas para procesar diferentes tipos
void processSimpleVariables();
void processTBLTags();
void processAPITags();  // Nueva para TBL_tags_api

// Funciones auxiliares
int getVariableIndex(const std::string& varName);
bool isWritableVariable(const std::string& varName);
bool getPACConnectionStatus();
void cleanupAndExit();  // Si la necesitas en main.cpp

// Función auxiliar para getVariableIndex específico de API
int getAPIVariableIndex(const std::string &varName);

int getBatchVariableIndex(const std::string &varName);

#endif // OPCUA_SERVER_H