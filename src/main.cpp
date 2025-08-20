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
    cout << "\n🛑 Señal de interrupción recibida (" << signal << ")" << endl;
    cout << "🔄 Terminando servidor..." << endl;
    
    running = false;
    server_running = false;
    
    this_thread::sleep_for(chrono::milliseconds(500));
}

int main() {
    cout << "🚀 SERVIDOR OPC UA + PAC CONTROL" << endl;
    cout << "===============================" << endl;
    cout << "📅 PetroSantander SCADA System" << endl;
    cout << "🔧 Versión simplificada y optimizada" << endl;
    cout << endl;

    // Configurar señales
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Inicializar servidor
    ServerInit();

    if (running) {
        cout << "\n📋 FUNCIONALIDADES:" << endl;
        cout << "✓ Lectura de tablas PAC" << endl;
        cout << "✓ Escritura de variables de setpoint" << endl;
        cout << "✓ Mapeo automático OPC UA" << endl;
        cout << "✓ Reconexión automática" << endl;

        cout << "\n🌐 CONEXIONES:" << endl;
        cout << "• OPC UA: opc.tcp://localhost:4840" << endl;
        cout << "• PAC Control: 192.168.1.30:22001" << endl;

        cout << "\n⚡ Presiona Ctrl+C para detener" << endl;
        cout << "===============================" << endl;

        // Ejecutar servidor
        runServer();
    }

    // Limpieza
    cleanupAndExit();
    return 0;
}