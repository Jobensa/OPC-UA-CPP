#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include "common.h"
#include "opcua_server.h"

using namespace std;

// Variables externas del servidor
extern std::atomic<bool> running;
extern std::atomic<bool> server_running;

// Handler para Ctrl+C
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n🛑 Señal de terminación recibida. Deteniendo servidor..." << std::endl;
        running = false;
        server_running = false;
    }
}



int main() {
    cout << "PAC to OPC-UA Server - PetroSantander SCADA" << endl;

    // Registrar handler de señales
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        // Inicializar servidor
        if (!ServerInit()) {
            LOG_ERROR("Fallo en inicialización del servidor");
            cleanupAndExit();
            return 1;
        }

        if (running) {
            LOG_INFO("🚀 Iniciando servidor OPC-UA...");
            
            // Ejecutar servidor
            UA_StatusCode result = runServer();
            
            if (result != UA_STATUSCODE_GOOD) {
                LOG_ERROR("Error ejecutando servidor: " << result);
            }
        }

    } catch (const exception& e) {
        LOG_ERROR("Excepción en main: " << e.what());
    }

    // Limpieza final
    cleanupAndExit();
    return 0;
}