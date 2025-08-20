#include "pac_control_client_simple.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>

using namespace std;

// Implementación del cliente PAC simplificado para demo
PACControlClient::PACControlClient(const string& host, int port) 
    : host(host), port(port), connected(false) {
}

bool PACControlClient::connect() {
    cout << "🔌 Intentando conectar a PAC " << host << ":" << port << endl;
    // Simulamos conexión exitosa para propósitos de demo
    this_thread::sleep_for(chrono::milliseconds(100));
    connected = true;
    cout << "✅ Conectado al PAC (simulado)" << endl;
    return true;
}

bool PACControlClient::isConnected() const {
    return connected;
}

void PACControlClient::disconnect() {
    connected = false;
    cout << "🔌 Desconectado del PAC" << endl;
}

// Lecturas simuladas para demo
vector<float> PACControlClient::readFloatTable(const string& table, int start, int end) {
    cout << "📊 Leyendo tabla float: " << table << "[" << start << "-" << end << "]" << endl;
    vector<float> result;
    for (int i = start; i <= end; i++) {
        // Generar valores simulados basados en el índice
        float value = 20.0f + (i % 10) * 5.0f + (rand() % 100) / 100.0f;
        result.push_back(value);
    }
    return result;
}

vector<int32_t> PACControlClient::readInt32Table(const string& table, int start, int end) {
    cout << "📊 Leyendo tabla int32: " << table << "[" << start << "-" << end << "]" << endl;
    vector<int32_t> result;
    for (int i = start; i <= end; i++) {
        // Generar valores de alarma simulados
        int32_t value = (i % 2 == 0) ? 0 : 1; // Alternar entre 0 y 1
        result.push_back(value);
    }
    return result;
}

// Escrituras simuladas
bool PACControlClient::writeFloatTableIndex(const string& table, int index, float value) {
    cout << "✍️ Escribiendo tabla float: " << table << "[" << index << "] = " << value << endl;
    this_thread::sleep_for(chrono::milliseconds(10));
    return true; // Simular éxito
}

bool PACControlClient::writeInt32TableIndex(const string& table, int index, int32_t value) {
    cout << "✍️ Escribiendo tabla int32: " << table << "[" << index << "] = " << value << endl;
    this_thread::sleep_for(chrono::milliseconds(10));
    return true; // Simular éxito
}

// Variables individuales (agregadas para completitud)
float PACControlClient::readSingleFloatVariableByTag(const string& tag) {
    cout << "📊 Leyendo variable float: " << tag << endl;
    return 25.5f + (rand() % 100) / 100.0f;
}

int32_t PACControlClient::readSingleInt32VariableByTag(const string& tag) {
    cout << "📊 Leyendo variable int32: " << tag << endl;
    return rand() % 2;
}

bool PACControlClient::writeSingleFloatVariable(const string& tag, float value) {
    cout << "✍️ Escribiendo variable float: " << tag << " = " << value << endl;
    return true;
}

bool PACControlClient::writeSingleInt32Variable(const string& tag, int32_t value) {
    cout << "✍️ Escribiendo variable int32: " << tag << " = " << value << endl;
    return true;
}
