#ifndef COMMON_H
#define COMMON_H

#include <open62541/server.h>
#include "pac_control_client.h"
#include <iostream>
using namespace std; 

// ============== MACROS DE DEBUG ==============
// Descomenta la siguiente línea para habilitar debug completo
// #define ENABLE_DEBUG

// Niveles de debug
#define ENABLE_INFO     // Mensajes informativos siempre activos
// #define ENABLE_VERBOSE  // Debug detallado de variables (deshabilitado por defecto)

#ifdef ENABLE_DEBUG
    #define DEBUG_PRINT(msg) cout << msg << endl
    #define DEBUG_INFO(msg) cout << msg << endl
    #define DEBUG_VERBOSE(msg) cout << msg << endl
#else
    #ifdef ENABLE_INFO
        #define DEBUG_INFO(msg) cout << msg << endl
    #else
        #define DEBUG_INFO(msg) do {} while(0)
    #endif
    
    #ifdef ENABLE_VERBOSE
        #define DEBUG_VERBOSE(msg) cout << msg << endl
    #else
        #define DEBUG_VERBOSE(msg) do {} while(0)
    #endif
    
    #define DEBUG_PRINT(msg) do {} while(0)
#endif

// ============== DECLARACIONES ==============
// Declaración de funciones
void ServerInit();
UA_StatusCode runServer();
void StopServer();
bool getPACConnectionStatus(); // Nueva función para verificar estado PAC

#endif // COMMON_H