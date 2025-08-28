#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include "common.h"
#include "opcua_server.h"

using namespace std;

// Variables externas del servidor
extern std::atomic<bool> running;
extern bool server_running;

// Handler para Ctrl+C
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n🛑 Señal de terminación recibida. Deteniendo servidor..." << std::endl;
        running = false;
    }
}

int main() {
    cout << "PAC to OPC-UA Server - PetroSantander SCADA" << endl;

    // Registrar handler de señales
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Inicializar servidor
    ServerInit();

    if (running) {
        // Ejecutar servidor
        runServer();
    }

    // Limpieza
    cleanupAndExit();
    return 0;
}