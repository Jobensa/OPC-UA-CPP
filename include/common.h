#ifndef COMMON_H
#define COMMON_H

#include <open62541/server.h>
#include <iostream>
using namespace std;

// ============== MACROS DEBUG ==============
// Macros para debug y logging compatibles con código original
#define DEBUG_INFO(msg) do { std::cout << "[INFO] " << msg << std::endl; } while(0)
#define DEBUG_VERBOSE(msg) do { std::cout << "[VERBOSE] " << msg << std::endl; } while(0)
#define DEBUG_ERROR(msg) do { std::cerr << "[ERROR] " << msg << std::endl; } while(0)
#define DEBUG_WARNING(msg) do { std::cout << "[WARNING] " << msg << std::endl; } while(0)

// ============== DECLARACIONES ==============
void ServerInit();
UA_StatusCode runServer();
void cleanupAndExit();
bool getPACConnectionStatus();

#endif // COMMON_H