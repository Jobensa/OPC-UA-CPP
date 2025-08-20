#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>
#include "common.h"

using namespace std;

// Declaraciones del servidor integrado
#include <open62541/server.h>
#include "pac_control_client.h"
#include "opcua_server_integrated.h"


// Variables externas del servidor


// Funciones del servidor
extern void ServerInit();
extern UA_StatusCode runServer();
extern void cleanupAndExit();
extern bool server_running;
extern UA_Server *server;
extern std::atomic<bool> running;
extern std::mutex serverMutex;
extern std::unique_ptr<PACControlClient> pacClient;

void signalHandler(int signal)
{
    cout << "\n🛑 Recibida señal de interrupción (" << signal << ")" << endl;
    cout << "🔄 Iniciando terminación controlada del servidor..." << endl;

    running = false;
    server_running = false;

    // Dar tiempo para limpieza
    this_thread::sleep_for(chrono::milliseconds(500));
}

int main()
{
    cout << "🚀 SERVIDOR OPC UA + CLIENTE PAC CONTROL INTEGRADO" << endl;
    cout << "==================================================" << endl;
    cout << "📅 PetroSantander SCADA System" << endl;
    cout << "🔧 Cliente PACControlClient + Protocolo PAC Control" << endl;
    cout << "🌐 Arquitectura: PAC ←→ PACControlClient ←→ OPC UA" << endl;
    cout << endl;

    // Configurar manejador de señales
    signal(SIGINT, signalHandler);  // Ctrl+C
    signal(SIGTERM, signalHandler); // Terminación del sistema

    // Inicializar servidor
    ServerInit();

    // Mostrar información del sistema
    if (running)
    {
        cout << "\n🔌 CLIENTE PAC CONTROL INTEGRADO:" << endl;

        // Aquí podríamos verificar estado de conexión si tuviéramos acceso
        // Por simplicidad, mostramos información estática
        cout << "❌ Cliente PACControlClient: DESCONECTADO" << endl;
        cout << "⚠️  Verificar configuración en pac_config.json" << endl;

        cout << "\n📋 FUNCIONALIDADES IMPLEMENTADAS:" << endl;
        cout << "✓ Lectura de tablas flotantes PAC (TBL_TT_*)" << endl;
        cout << "✓ Lectura de tablas de alarmas (TBL_TA_*)" << endl;
        cout << "✓ Escritura de variables PAC" << endl;
        cout << "✓ Mapeo automático a nodos OPC UA" << endl;
        cout << "✓ Cache optimizado para performance" << endl;
        cout << "✓ Actualización en tiempo real" << endl;
        cout << "✓ Gestión de estados de alarma" << endl;

        cout << "\n🎯 VARIABLES MAPEADAS:" << endl;
        cout << "• TT_11001: Valores (TBL_TT_11001) + Alarmas (TBL_TA_11001)" << endl;
        cout << "• PT_20001: Valores (TBL_TT_20001) + Alarmas (TBL_TA_20001)" << endl;
        cout << "• FT_30001: Valores (TBL_TT_30001) + Alarmas (TBL_TA_30001)" << endl;

        cout << "\n🌐 CONEXIÓN:" << endl;
        cout << "• OPC UA: opc.tcp://localhost:4840" << endl;
        cout << "• PAC Control: 192.168.0.30:22001" << endl;

        cout << "\n⚡ Presiona Ctrl+C para detener el servidor" << endl;
        cout << "================================================" << endl;

        // Ejecutar servidor
        runServer();
    }

    // Limpieza y salida
    cleanupAndExit();
    return 0;
}