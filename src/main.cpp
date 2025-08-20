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

void signalHandler(int signal) {
    running = false;
    server_running = false;
    this_thread::sleep_for(chrono::milliseconds(500));
}

int main() {
    cout << "PAC to OPC-UA Server - PetroSantander SCADA" << endl;

    // Configurar señales
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