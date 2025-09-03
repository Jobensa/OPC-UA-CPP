#ifndef OPCUA_SERVER_H
#define OPCUA_SERVER_H

#include <open62541/server.h>
#include "common.h"  // Contiene todas las estructuras
#include <memory>

// ============== FUNCIONES DEL SERVIDOR ==============
bool ServerInit();
UA_StatusCode runServer();
void shutdownServer();
void cleanupServer();
void cleanupAndExit();
bool getPACConnectionStatus();

// ============== FUNCIONES DE CONFIGURACIÓN ==============
bool loadConfig(const std::string& configFile);
bool processConfigFromJson(const nlohmann::json& configJson);
void processConfigIntoVariables();
void createNodes();
void updateData();

// ============== FUNCIONES AUXILIARES ==============
int getVariableIndex(const std::string &varName);
int getAPIVariableIndex(const std::string &varName);
int getBatchVariableIndex(const std::string &varName);
bool isWritableVariable(const std::string &varName);
void verifyAndFixNodeTypes();
void enableWriteCallbacks();

#endif // OPCUA_SERVER_H