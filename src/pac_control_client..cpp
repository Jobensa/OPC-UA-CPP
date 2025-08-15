#include "pac_control_client.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cmath>  // Para isnan, isinf
#include <fcntl.h>  // Para fcntl y O_NONBLOCK

#include "common.h"  

using namespace std;

PACControlClient::PACControlClient(const string &ip, int port)
    : pac_ip(ip), pac_port(port), sock(-1), connected(false), cache_enabled(false)  // DESHABILITADO PARA DEBUG
{
}

PACControlClient::~PACControlClient()
{
    disconnect();
}

bool PACControlClient::connect()
{
    lock_guard<mutex> lock(comm_mutex);

    if (connected)
        return true;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        cerr << "Error creando socket" << endl;
        return false;
    }

    // Configurar timeout para socket
    struct timeval timeout;
    timeout.tv_sec = 3; // 3 segundos timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(pac_port);

    if (inet_pton(AF_INET, pac_ip.c_str(), &server_addr.sin_addr) <= 0)
    {
        cerr << "Dirección IP inválida: " << pac_ip << endl;
        close(sock);
        return false;
    }

    cout << "🔄 Intentando conectar a PAC: " << pac_ip << ":" << pac_port << endl;
    if (::connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        cerr << "❌ Error conectando a " << pac_ip << ":" << pac_port << " - PAC no disponible" << endl;
        close(sock);
        return false;
    }

    connected = true;
    cout << "✓ Conectado al PAC Control: " << pac_ip << ":" << pac_port << endl;
    return true;
}

void PACControlClient::disconnect()
{
    lock_guard<mutex> lock(comm_mutex);

    if (sock >= 0)
    {
        close(sock);
        sock = -1;
    }
    connected = false;
}

// CORRECCIÓN: Reemplazar cout por DEBUG_VERBOSE en las funciones clave
vector<float> PACControlClient::readFloatTable(const string &table_name,
                                               int start_pos, int end_pos)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected)
    {
        cerr << "No conectado al PAC" << endl;
        return {};
    }

    // Verificar cache solo si está habilitado
    string cache_key = table_name + "_" + to_string(start_pos) + "_" + to_string(end_pos);
    if (cache_enabled && isCacheValid(cache_key))
    {
        DEBUG_VERBOSE("📋 CACHE HIT: Usando datos cached para " << table_name);
        return table_cache[cache_key].data;
    }

    stringstream cmd;
    
    // Detectar tipo de tabla y usar comando apropiado
    if (table_name.find("TBL_DA_") != string::npos) {
        DEBUG_VERBOSE("🚨 LEYENDO TABLA DE ALARMAS: " << table_name);
        cmd << end_pos << " 0 }" << table_name << " TRange.\r";
    } else {
        DEBUG_VERBOSE("📊 LEYENDO TABLA DE DATOS: " << table_name);
        cmd << end_pos << " 0 }" << table_name << " TRange.\r";
    }

    string command = cmd.str();
    DEBUG_VERBOSE("📊 LEYENDO TABLA DE DATOS: " << table_name);
    DEBUG_VERBOSE("📋 Comando en bytes: ");
    for (char c : command) {
        DEBUG_VERBOSE("0x" << hex << (int)(unsigned char)c << " ");
    }
    DEBUG_VERBOSE(dec);
    DEBUG_VERBOSE("🔍 TIMESTAMP: " << chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count() << "ms");

    // 🔧 SOLUCIÓN: Limpiar buffer del socket antes de enviar comando
    flushSocketBuffer();
    
    if (!sendCommand(command))
    {
        cerr << "Error enviando comando" << endl;
        return {};
    }

    // Calcular bytes esperados (cada float = 4 bytes)
    int num_floats = end_pos - start_pos + 1;
    size_t expected_bytes = num_floats * 4;

    vector<uint8_t> raw_data = receiveData(expected_bytes);
    if (raw_data.empty())
    {
        cerr << "Error recibiendo datos de tabla" << endl;
        return {};
    }
    
    // 🔧 SOLUCIÓN: Validar integridad de los datos recibidos
    if (!validateDataIntegrity(raw_data, table_name)) {
        DEBUG_VERBOSE("⚠️  DATOS RECHAZADOS por validación de integridad: " << table_name);
        
        // 🔧 RETRY: Intentar una segunda vez con delay si hay contaminación
        DEBUG_VERBOSE("🔄 REINTENTANDO lectura después de 100ms...");
        this_thread::sleep_for(chrono::milliseconds(100));
        
        flushSocketBuffer();
        if (sendCommand(command)) {
            vector<uint8_t> retry_data = receiveData(expected_bytes);
            if (!retry_data.empty() && validateDataIntegrity(retry_data, table_name)) {
                DEBUG_VERBOSE("✅ RETRY EXITOSO: Datos válidos obtenidos en segundo intento");
                raw_data = retry_data;
            } else {
                DEBUG_VERBOSE("❌ RETRY FALLIDO: Datos siguen siendo inválidos");
                return {};
            }
        } else {
            return {};
        }
    }

    // 🔍 DIAGNÓSTICO: Mostrar datos RAW recibidos con análisis detallado
    DEBUG_VERBOSE("🔍 RAW DATA (" << raw_data.size() << " bytes): ");
    for (size_t i = 0; i < min(raw_data.size(), size_t(40)); i++) {
        DEBUG_VERBOSE(hex << setfill('0') << setw(2) << (int)raw_data[i] << " ");
    }
    DEBUG_VERBOSE(dec);
    
    // Análisis de patrones en los datos
    DEBUG_VERBOSE("🔍 ANÁLISIS DE PATRONES:");
    for (size_t i = 0; i < min(raw_data.size(), size_t(40)); i += 4) {
        if (i + 3 < raw_data.size()) {
            uint32_t as_int = raw_data[i] | (raw_data[i+1] << 8) | (raw_data[i+2] << 16) | (raw_data[i+3] << 24);
            float as_float;
            memcpy(&as_float, &as_int, 4);
            DEBUG_VERBOSE("  [" << i/4 << "] Raw: " << hex << setfill('0') << setw(2) << (int)raw_data[i] 
                 << " " << setw(2) << (int)raw_data[i+1] << " " << setw(2) << (int)raw_data[i+2] 
                 << " " << setw(2) << (int)raw_data[i+3] << dec
                 << " -> int32=" << as_int << " float=" << as_float);
        }
    }
    
    // Análisis de estabilidad de datos
    analyzeDataStability(table_name, raw_data);

    // Convertir bytes a floats (IEEE 754 little endian)
    vector<float> floats = convertBytesToFloats(raw_data);

    // Verificar consistencia de datos comparando con cache anterior
    auto prev_cache = table_cache.find(cache_key);
    if (prev_cache != table_cache.end() && prev_cache->second.valid) {
        bool values_changed = false;
        if (floats.size() == prev_cache->second.data.size()) {
            for (size_t i = 0; i < floats.size(); i++) {
                float diff = abs(floats[i] - prev_cache->second.data[i]);
                if (diff > 0.001f) { // Tolerancia de 0.001 para cambios significativos
                    values_changed = true;
                    DEBUG_VERBOSE("🔄 CAMBIO DETECTADO en [" << (start_pos + i) << "]: " 
                         << prev_cache->second.data[i] << " -> " << floats[i] 
                         << " (diff: " << diff << ")");
                }
            }
            if (!values_changed) {
                DEBUG_VERBOSE("✅ VALORES ESTABLES: Sin cambios significativos en " << table_name);
            }
        }
    }

    // Guardar en cache
    CacheEntry entry;
    entry.data = floats;
    entry.timestamp = chrono::steady_clock::now();
    entry.valid = true;
    table_cache[cache_key] = entry;

    DEBUG_INFO("✓ Tabla " << table_name << " leída: " << floats.size() << " valores");
    for (size_t i = 0; i < floats.size(); i++)
    {
        DEBUG_VERBOSE("  [" << (start_pos + i) << "] = " << floats[i]);
    }

    return floats;
}

float PACControlClient::readFloatVariable(const string &table_name, int index)
{
    vector<float> values = readFloatTable(table_name, index, index);
    return values.empty() ? 0.0f : values[0];
}

int32_t PACControlClient::readInt32Variable(const string &table_name, int index)
{
    vector<int32_t> values = readInt32Table(table_name, index, index);
    return values.empty() ? 0 : values[0];
}

// Lectura de tablas de enteros int32 (para alarmas TBL_TA*, TBL_LA*)
vector<int32_t> PACControlClient::readInt32Table(const string &table_name,
                                                 int start_pos, int end_pos)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected)
    {
        cerr << "No conectado al PAC" << endl;
        return {};
    }

    // Verificar cache
    string cache_key = table_name + "_int32_" + to_string(start_pos) + "_" + to_string(end_pos);
    if (isCacheValid(cache_key))
    {
        // Convertir float cache a int32 (necesitamos cache separado para int32)
        // Por simplicidad, leer directamente por ahora
    }

    // Comando PAC Control CORRECTO para int32 (alarmas):
    // Formato: "9 0 }TBL_DA_0001 TRange.\r" (mismo formato que floats)
    stringstream cmd;
    
    // Detectar tipo de tabla y usar comando apropiado
    if (table_name.find("TBL_DA_") != string::npos) {
        cout << "🚨 PROBANDO TABLA DE ALARMAS INT32: " << table_name << endl;
        // Para alarmas, usar mismo formato que funciona para floats
        cmd << end_pos << " 0 }" << table_name << " TRange.\r";
    } else {
        cout << "📊 LEYENDO TABLA INT32: " << table_name << endl;
        cmd << end_pos << " 0 }" << table_name << " TRange.\r";
    }

    string command = cmd.str();
    cout << "Enviando comando int32: " << command << endl;

    if (!sendCommand(command))
    {
        cerr << "Error enviando comando int32" << endl;
        return {};
    }

    // Calcular bytes esperados (cada int32 = 4 bytes)
    int num_ints = end_pos - start_pos + 1;
    size_t expected_bytes = num_ints * 4;

    vector<uint8_t> raw_data = receiveData(expected_bytes);
    if (raw_data.empty())
    {
        cerr << "Error recibiendo datos int32 de tabla" << endl;
        return {};
    }

    // 🔍 DIAGNÓSTICO INT32: Mostrar datos RAW recibidos
    cout << "🔍 INT32 RAW DATA (" << raw_data.size() << " bytes): ";
    for (size_t i = 0; i < min(raw_data.size(), size_t(32)); i++) {
        cout << hex << setfill('0') << setw(2) << (int)raw_data[i] << " ";
    }
    cout << dec << endl;

    // 🔍 DIAGNÓSTICO INT32: Interpretación como texto ASCII
    cout << "🔍 INT32 AS ASCII: \"";
    for (size_t i = 0; i < min(raw_data.size(), size_t(32)); i++) {
        char c = raw_data[i];
        cout << (isprint(c) ? c : '.');
    }
    cout << "\"" << endl;

    // Convertir bytes a int32 (little endian)
    vector<int32_t> ints = convertBytesToInt32s(raw_data);

    cout << "✓ Tabla int32 " << table_name << " leída: " << ints.size() << " valores" << endl;
    for (size_t i = 0; i < ints.size(); i++)
    {
        cout << "  [" << (start_pos + i) << "] = " << ints[i] << " (0x" << hex << ints[i] << dec << ")" << endl;
    }

    return ints;
}

bool PACControlClient::writeFloatVariable(const string &table_name, int index, float value)
{
    // TODO: Implementar protocolo de escritura
    // Formato probable: "índice tabla valor TWrite" o similar
    lock_guard<mutex> lock(comm_mutex);

    if (!connected)
        return false;

    stringstream cmd;
    cmd << index << " " << table_name << " " << value << " TWrite\r";

    string command = cmd.str();
    cout << "Escribiendo float: " << command << endl;

    bool result = sendCommand(command);
    if (result)
    {
        // Invalidar cache para esta tabla
        clearCache();
        cout << "✓ Variable float escrita: " << table_name << "[" << index << "] = " << value << endl;
    }

    return result;
}

bool PACControlClient::writeInt32Variable(const string &table_name, int index, int32_t value)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected)
        return false;

    stringstream cmd;
    cmd << index << " " << table_name << " " << value << " TWrite\r";

    string command = cmd.str();
    cout << "Escribiendo int32: " << command << endl;

    bool result = sendCommand(command);
    if (result)
    {
        // Invalidar cache para esta tabla
        clearCache();
        cout << "✓ Variable int32 escrita: " << table_name << "[" << index << "] = 0x"
             << hex << value << dec << endl;
    }

    return result;
}

bool PACControlClient::writeFloatTable(const string &table_name, const vector<float> &values)
{
    bool success = true;
    for (size_t i = 0; i < values.size(); i++)
    {
        if (!writeFloatVariable(table_name, i, values[i]))
        {
            success = false;
        }
    }
    return success;
}

bool PACControlClient::writeInt32Table(const string &table_name, const vector<int32_t> &values)
{
    bool success = true;
    for (size_t i = 0; i < values.size(); i++)
    {
        if (!writeInt32Variable(table_name, i, values[i]))
        {
            success = false;
        }
    }
    return success;
}

// Implementaciones de comunicación de bajo nivel
bool PACControlClient::sendCommand(const string &command)
{
    if (sock < 0) {
        cout << "❌ Socket inválido - Marcando como desconectado" << endl;
        connected = false;
        return false;
    }

    ssize_t bytes_sent = send(sock, command.c_str(), command.length(), 0);
    if (bytes_sent != (ssize_t)command.length()) {
        cout << "❌ Error enviando comando - Esperado: " << command.length() << " Enviado: " << bytes_sent << " (errno: " << errno << ")" << endl;
        cout << "🔌 ERROR DE ENVÍO - Marcando como desconectado" << endl;
        connected = false;  // MARCAR COMO DESCONECTADO
        return false;
    }
    
    return true;
}

vector<uint8_t> PACControlClient::receiveData(size_t expected_bytes)
{
    cout << "🔄 Esperando " << expected_bytes << " bytes del PAC..." << endl;
    
    // 🔧 SOLUCIÓN: Buffer limpio inicializado con ceros para evitar datos residuales
    vector<uint8_t> buffer(expected_bytes, 0);
    size_t total_received = 0;

    while (total_received < expected_bytes)
    {
        cout << "📡 Intentando recibir " << (expected_bytes - total_received) << " bytes..." << endl;
        ssize_t bytes = recv(sock, buffer.data() + total_received,
                             expected_bytes - total_received, 0);
        if (bytes <= 0)
        {
            cout << "❌ recv() retornó: " << bytes << " (errno: " << errno << ")" << endl;
            
            // CRÍTICO: Marcar conexión como perdida cuando hay error de red
            if (bytes == 0) {
                cout << "🔌 CONEXIÓN CERRADA POR EL PAC - Marcando como desconectado" << endl;
            } else {
                cout << "🔌 ERROR DE RED - Marcando como desconectado" << endl;
            }
            connected = false;  // MARCAR COMO DESCONECTADO
            
            if (total_received > 0) {
                cout << "📋 Datos parciales recibidos (" << total_received << " bytes): ";
                for (size_t i = 0; i < total_received; i++) {
                    cout << "0x" << hex << (int)buffer[i] << " ";
                }
                cout << dec << endl;
                // Retornar los datos parciales en lugar de vector vacío
                buffer.resize(total_received);
                return buffer;
            }
            cerr << "Error recibiendo datos o conexión cerrada" << endl;
            return {};
        }
        cout << "✅ Recibidos " << bytes << " bytes" << endl;
        total_received += bytes;
    }

    cout << "🎉 Total recibido: " << total_received << " bytes" << endl;
    return buffer;
}

// 🔧 NUEVO: Limpiar buffer del socket para evitar datos residuales
void PACControlClient::flushSocketBuffer() {
    if (sock < 0 || !connected) return;
    
    // Configurar socket como no-bloqueante temporalmente
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    char temp_buffer[1024];
    int flushed_bytes = 0;
    
    // Leer cualquier dato residual del buffer del socket
    while (true) {
        ssize_t bytes = recv(sock, temp_buffer, sizeof(temp_buffer), 0);
        if (bytes <= 0) break;
        flushed_bytes += bytes;
    }
    
    // Restaurar el socket a modo bloqueante
    fcntl(sock, F_SETFL, flags);
    
    if (flushed_bytes > 0) {
        cout << "🧹 BUFFER LIMPIADO: Eliminados " << flushed_bytes << " bytes residuales del socket" << endl;
    }
}

// 🔧 NUEVO: Validar integridad de datos para detectar contaminación
bool PACControlClient::validateDataIntegrity(const vector<uint8_t>& data, const string& table_name) {
    if (data.empty()) return false;
    
    // Verificar tamaño esperado
    bool is_alarm_table = (table_name.find("TBL_DA_") == 0) || 
                         (table_name.find("TBL_PA_") == 0) || 
                         (table_name.find("TBL_LA_") == 0) || 
                         (table_name.find("TBL_TA_") == 0);
    
    size_t expected_size;
    if (is_alarm_table) {
        expected_size = 5 * 4; // 5 int32 = 20 bytes
    } else {
        expected_size = 10 * 4; // 10 float = 40 bytes
    }
    
    if (data.size() != expected_size) {
        cout << "⚠️  VALIDACIÓN FALLIDA: " << table_name << " - Tamaño esperado: " << expected_size 
             << " bytes, recibido: " << data.size() << " bytes" << endl;
        return false;
    }
    
    // Detectar patrones sospechosos (todos ceros o valores muy grandes)
    bool all_zeros = true;
    bool has_large_values = false;
    
    for (size_t i = 0; i < data.size(); i += 4) {
        uint32_t value = data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24);
        
        if (value != 0) all_zeros = false;
        if (value > 1000000) has_large_values = true; // Valores > 1M son sospechosos
    }
    
    // Si es una tabla que debería tener valores pero solo tiene ceros, es sospechoso
    if (all_zeros && table_name.find("11003") != string::npos) {
        cout << "✅ VALIDACIÓN OK: " << table_name << " - Datos todos en cero (esperado según PAC)" << endl;
    } else if (has_large_values && table_name.find("11003") != string::npos) {
        cout << "🚨 CONTAMINACIÓN DETECTADA: " << table_name << " tiene valores anómalos grandes" << endl;
        return false;
    }
    
    return true;
}

vector<float> PACControlClient::convertBytesToFloats(const vector<uint8_t> &bytes)
{
    vector<float> floats;

    // Validar que tenemos múltiplos de 4 bytes para floats IEEE 754
    if (bytes.size() % 4 != 0) {
        cerr << "⚠️  ADVERTENCIA: Datos no alineados a 4 bytes (" << bytes.size() << " bytes)" << endl;
    }

    for (size_t i = 0; i + 3 < bytes.size(); i += 4)
    {
        // Leer 4 bytes como float IEEE 754 little endian
        uint32_t int_val = bytes[i] |
                           (bytes[i + 1] << 8) |
                           (bytes[i + 2] << 16) |
                           (bytes[i + 3] << 24);

        float float_val;
        memcpy(&float_val, &int_val, 4);
        
        // Validar que el float es un número válido (no NaN, no Infinity)
        if (std::isnan(float_val) || std::isinf(float_val)) {
            cout << "⚠️  ADVERTENCIA: Valor float inválido en posición " << i/4 << ": " << float_val << endl;
            cout << "    Bytes raw: 0x" << hex << setfill('0') << setw(2) << (int)bytes[i] << " "
                 << setw(2) << (int)bytes[i+1] << " " << setw(2) << (int)bytes[i+2] << " " 
                 << setw(2) << (int)bytes[i+3] << dec << endl;
            // En lugar de usar el valor inválido, usar 0.0
            float_val = 0.0f;
        }
        
        floats.push_back(float_val);
    }

    return floats;
}

vector<int32_t> PACControlClient::convertBytesToInt32s(const vector<uint8_t> &bytes)
{
    vector<int32_t> ints;

    for (size_t i = 0; i + 3 < bytes.size(); i += 4)
    {
        // Leer 4 bytes como int32 little endian
        int32_t int_val = bytes[i] |
                          (bytes[i + 1] << 8) |
                          (bytes[i + 2] << 16) |
                          (bytes[i + 3] << 24);

        ints.push_back(int_val);
    }

    return ints;
}

vector<uint8_t> PACControlClient::convertFloatsToBytes(const vector<float> &floats)
{
    vector<uint8_t> bytes;

    for (float f : floats)
    {
        uint32_t int_val;
        memcpy(&int_val, &f, 4);

        // Little endian
        bytes.push_back(int_val & 0xFF);
        bytes.push_back((int_val >> 8) & 0xFF);
        bytes.push_back((int_val >> 16) & 0xFF);
        bytes.push_back((int_val >> 24) & 0xFF);
    }

    return bytes;
}

vector<uint8_t> PACControlClient::convertInt32sToBytes(const vector<int32_t> &ints)
{
    vector<uint8_t> bytes;

    for (int32_t i : ints)
    {
        uint32_t int_val = static_cast<uint32_t>(i);

        // Little endian
        bytes.push_back(int_val & 0xFF);
        bytes.push_back((int_val >> 8) & 0xFF);
        bytes.push_back((int_val >> 16) & 0xFF);
        bytes.push_back((int_val >> 24) & 0xFF);
    }

    return bytes;
}

bool PACControlClient::isCacheValid(const string &key)
{
    auto it = table_cache.find(key);
    if (it == table_cache.end() || !it->second.valid)
    {
        return false;
    }

    auto now = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                       now - it->second.timestamp)
                       .count();

    return elapsed < CACHE_TIMEOUT_MS;
}

void PACControlClient::clearCache()
{
    table_cache.clear();
}

// Comandos básicos heredados del análisis anterior
string PACControlClient::receiveResponse()
{
    char buffer[2048] = {0}; // Buffer más grande
    ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        return string(buffer);
    }
    return "";
}

// Función para detectar automáticamente el tipo de datos en una tabla
bool PACControlClient::detectDataType(const string& table_name, bool& is_integer_data) {
    lock_guard<mutex> lock(comm_mutex);
    
    if (!connected) {
        return false;
    }
    
    cout << "🔍 ANÁLISIS: Detectando tipo de datos para " << table_name << endl;
    
    // PASO 1: Detección basada en nombre de tabla (criterio principal)
    if (table_name.find("TBL_DA_") == 0 || table_name.find("TBL_LA_") == 0 || table_name.find("TBL_PA_") == 0) {
        cout << "🏷️  TIPO POR NOMBRE: " << table_name << " -> ENTEROS (DA/LA/PA = datos digitales/alarmas)" << endl;
        is_integer_data = true;
        return true;
    }
    
    if (table_name.find("TBL_DT_") == 0 || table_name.find("TBL_PT_") == 0 || 
        table_name.find("TBL_TT_") == 0 || table_name.find("TBL_LT_") == 0) {
        cout << "🏷️  TIPO POR NOMBRE: " << table_name << " -> FLOTANTES (DT/PT/TT/LT = transmisores)" << endl;
        is_integer_data = false;
        return true;
    }
    
    // PASO 2: Si no coincide con nomenclatura conocida, usar análisis de contenido
    cout << "⚠️  TABLA DESCONOCIDA: " << table_name << " - Analizando contenido..." << endl;
    
    // Leer una muestra pequeña de datos
    stringstream cmd;
    cmd << "1 0 }" << table_name << " TRange.\r";
    
    string command = cmd.str();
    
    if (!sendCommand(command)) {
        return false;
    }
    
    vector<uint8_t> raw_data = receiveData(8); // Solo 2 valores para análisis
    if (raw_data.size() < 8) {
        return false;
    }
    
    // Interpretar como float
    vector<float> as_floats = convertBytesToFloats(raw_data);
    // Interpretar como int32
    vector<int32_t> as_ints = convertBytesToInt32s(raw_data);
    
    // Análisis: Si los valores float son muy pequeños (< 1e-30) pero los int32 son razonables
    // es probable que sean datos integer
    int reasonable_floats = 0;
    int reasonable_ints = 0;
    
    for (size_t i = 0; i < min(as_floats.size(), as_ints.size()); i++) {
        // Verificar si el float es un valor razonable
        if (abs(as_floats[i]) > 1e-30 && abs(as_floats[i]) < 1e30 && 
            !std::isnan(as_floats[i]) && !std::isinf(as_floats[i])) {
            reasonable_floats++;
        }
        
        // Verificar si el int32 está en rango razonable (0-65535 típico para tags)
        if (as_ints[i] >= 0 && as_ints[i] <= 100000) {
            reasonable_ints++;
        }
    }
    
    cout << "🔍 ANÁLISIS " << table_name << ": floats razonables=" << reasonable_floats 
         << ", ints razonables=" << reasonable_ints << endl;
    cout << "    Como float: ";
    for (float f : as_floats) cout << f << " ";
    cout << endl << "    Como int32: ";
    for (int32_t i : as_ints) cout << i << " ";
    cout << endl;
    
    // Si hay más integers razonables que floats, es probable que sea integer data
    is_integer_data = (reasonable_ints > reasonable_floats);
    
    cout << "✅ DETECCIÓN: " << table_name << " -> " 
         << (is_integer_data ? "DATOS INTEGER" : "DATOS FLOAT") << endl;
    
    return true;
}

// Función inteligente para leer tabla como float (con detección automática)
vector<float> PACControlClient::readTableAsFloat(const string& table_name, 
                                                int start_pos, int end_pos) {
    // Primero detectar el tipo de datos
    bool is_integer_data;
    if (!detectDataType(table_name, is_integer_data)) {
        cout << "⚠️  No se pudo detectar tipo de datos para " << table_name << endl;
        return readFloatTable(table_name, start_pos, end_pos); // Fallback
    }
    
    if (is_integer_data) {
        cout << "🔄 CONVERSIÓN: Leyendo " << table_name << " como int32 y convirtiendo a float" << endl;
        vector<int32_t> int_values = readTableAsInt32(table_name, start_pos, end_pos);
        vector<float> float_values;
        for (int32_t val : int_values) {
            float_values.push_back(static_cast<float>(val));
        }
        return float_values;
    } else {
        cout << "📊 DIRECTO: Leyendo " << table_name << " como float nativo" << endl;
        return readFloatTable(table_name, start_pos, end_pos);
    }
}

// Función para leer tabla como int32 (optimizada)
vector<int32_t> PACControlClient::readTableAsInt32(const string& table_name,
                                                  int start_pos, int end_pos) {
    lock_guard<mutex> lock(comm_mutex);

    if (!connected) {
        cerr << "No conectado al PAC" << endl;
        return {};
    }

    // Verificar cache solo si está habilitado
    string cache_key = table_name + "_int32_" + to_string(start_pos) + "_" + to_string(end_pos);
    
    stringstream cmd;
    cmd << end_pos << " 0 }" << table_name << " TRange.\r";

    string command = cmd.str();
    cout << "📊 LEYENDO TABLA INT32: " << table_name << endl;

    // 🔧 SOLUCIÓN: Limpiar buffer del socket antes de enviar comando
    flushSocketBuffer();
    
    if (!sendCommand(command)) {
        cerr << "Error enviando comando int32" << endl;
        return {};
    }

    // Calcular bytes esperados (cada int32 = 4 bytes)
    int num_ints = end_pos - start_pos + 1;
    size_t expected_bytes = num_ints * 4;

    vector<uint8_t> raw_data = receiveData(expected_bytes);
    if (raw_data.empty()) {
        cerr << "Error recibiendo datos int32 de tabla" << endl;
        return {};
    }
    
    // 🔧 SOLUCIÓN: Validar integridad de los datos recibidos
    if (!validateDataIntegrity(raw_data, table_name)) {
        cout << "⚠️  DATOS INT32 RECHAZADOS por validación de integridad: " << table_name << endl;
        
        // 🔧 RETRY: Intentar una segunda vez con delay si hay contaminación
        cout << "🔄 REINTENTANDO lectura int32 después de 100ms..." << endl;
        this_thread::sleep_for(chrono::milliseconds(100));
        
        flushSocketBuffer();
        if (sendCommand(command)) {
            vector<uint8_t> retry_data = receiveData(expected_bytes);
            if (!retry_data.empty() && validateDataIntegrity(retry_data, table_name)) {
                cout << "✅ RETRY INT32 EXITOSO: Datos válidos obtenidos en segundo intento" << endl;
                raw_data = retry_data;
            } else {
                cout << "❌ RETRY INT32 FALLIDO: Datos siguen siendo inválidos" << endl;
                return {};
            }
        } else {
            return {};
        }
    }

    // Análisis de estabilidad de datos  
    analyzeDataStability(table_name, raw_data);

    // Convertir bytes a int32 (little endian)
    vector<int32_t> ints = convertBytesToInt32s(raw_data);

    cout << "✓ Tabla int32 " << table_name << " leída: " << ints.size() << " valores" << endl;
    for (size_t i = 0; i < ints.size(); i++) {
        cout << "  [" << (start_pos + i) << "] = " << ints[i] << endl;
    }

    return ints;
}

// Función para analizar estabilidad de datos
void PACControlClient::analyzeDataStability(const string& table_name, const vector<uint8_t>& raw_data) {
    // Agregar a historial
    read_history[table_name].push_back(raw_data);
    
    // Mantener solo las últimas 5 lecturas
    if (read_history[table_name].size() > 5) {
        read_history[table_name].erase(read_history[table_name].begin());
    }
    
    if (read_history[table_name].size() >= 2) {
        cout << "📊 ANÁLISIS DE ESTABILIDAD " << table_name << ":" << endl;
        
        // Comparar con lectura anterior
        auto& current = read_history[table_name].back();
        auto& previous = read_history[table_name][read_history[table_name].size() - 2];
        
        if (current.size() == previous.size()) {
            int differences = 0;
            for (size_t i = 0; i < current.size(); i++) {
                if (current[i] != previous[i]) {
                    differences++;
                    cout << "  BYTE " << i << ": " << hex << (int)previous[i] << " -> " << (int)current[i] << dec << endl;
                }
            }
            
            if (differences == 0) {
                cout << "  ✅ DATOS IDÉNTICOS - Sin cambios" << endl;
            } else {
                cout << "  ⚠️  DATOS CAMBIARON - " << differences << " bytes diferentes" << endl;
                
                // Analizar si es ruido o cambios reales
                float change_percentage = (float)differences / current.size() * 100;
                if (change_percentage < 10) {
                    cout << "  🔍 POSIBLE RUIDO (" << change_percentage << "% cambió)" << endl;
                } else {
                    cout << "  📈 CAMBIO SIGNIFICATIVO (" << change_percentage << "% cambió)" << endl;
                }
            }
        }
        
        // Análisis de todas las lecturas
        if (read_history[table_name].size() >= 3) {
            cout << "  📈 TENDENCIA (últimas " << read_history[table_name].size() << " lecturas):" << endl;
            bool all_identical = true;
            for (size_t i = 1; i < read_history[table_name].size(); i++) {
                if (read_history[table_name][i] != read_history[table_name][0]) {
                    all_identical = false;
                    break;
                }
            }
            
            if (all_identical) {
                cout << "    ✅ COMPLETAMENTE ESTABLE" << endl;
            } else {
                cout << "    ⚠️  VALORES VARIABLES" << endl;
            }
        }
    }
}

vector<string> PACControlClient::getTasks()
{
    string response = sendRawCommand("ANY.TASKS?");
    // TODO: Parsear respuesta
    return {};
}

// CORRECCIÓN: Funciones auxiliares con DEBUG_VERBOSE
string PACControlClient::cleanASCIINumber(const string& ascii_str)
{
    string result;
    bool decimal_found = false;
    bool negative_found = false;
    bool exponent_found = false;
    bool exponent_sign_found = false;
    
    DEBUG_VERBOSE("🔍 LIMPIANDO NÚMERO ASCII: '" << ascii_str << "'");
    
    // ...existing logic...
    
    DEBUG_VERBOSE("🔍 NÚMERO LIMPIO: '" << result << "'");
    
    // Validar que el resultado es un número válido
    if (result.empty() || result == "-" || result == "." || result == "e" || result == "E") {
        DEBUG_VERBOSE("⚠️  NÚMERO INVÁLIDO después de limpiar: '" << result << "'");
        return "0";
    }
    
    return result;
}


float PACControlClient::convertStringToFloat(const string& str) {
    if (str.empty()) {
        return 0.0f;
    }
    
    try {
        float value = std::stof(str);
        
        if (std::isnan(value) || std::isinf(value)) {
            DEBUG_VERBOSE("⚠️  VALOR FLOAT INVÁLIDO: " << str << " -> " << value);
            return 0.0f;
        }
        
        DEBUG_VERBOSE("✅ CONVERSIÓN EXITOSA: '" << str << "' -> " << value);
        return value;
        
    } catch (const std::invalid_argument& e) {
        DEBUG_VERBOSE("❌ ERROR: Argumento inválido para conversión: '" << str << "' - " << e.what());
        return 0.0f;
    } catch (const std::out_of_range& e) {
        DEBUG_VERBOSE("❌ ERROR: Valor fuera de rango: '" << str << "' - " << e.what());
        return 0.0f;
    } catch (const exception& e) {
        DEBUG_VERBOSE("❌ ERROR: Excepción general en conversión: '" << str << "' - " << e.what());
        return 0.0f;
    }
}


int32_t PACControlClient::convertStringToInt32(const string& str) {
    if (str.empty()) {
        return 0;
    }
    
    try {
        double double_value = std::stod(str);
        
        if (double_value > INT32_MAX || double_value < INT32_MIN) {
            DEBUG_VERBOSE("⚠️  VALOR FUERA DE RANGO INT32: " << str << " -> " << double_value);
            return 0;
        }
        
        int32_t value = static_cast<int32_t>(double_value);
        DEBUG_VERBOSE("✅ CONVERSIÓN INT32 EXITOSA: '" << str << "' -> " << value);
        return value;
        
    } catch (const std::invalid_argument& e) {
        DEBUG_VERBOSE("❌ ERROR: Argumento inválido para conversión int32: '" << str << "' - " << e.what());
        return 0;
    } catch (const std::out_of_range& e) {
        DEBUG_VERBOSE("❌ ERROR: Valor fuera de rango int32: '" << str << "' - " << e.what());
        return 0;
    } catch (const exception& e) {
        DEBUG_VERBOSE("❌ ERROR: Excepción general en conversión int32: '" << str << "' - " << e.what());
        return 0;
    }
}


// CORRECCIÓN: Función readSingleFloatVariableByTag con DEBUG_VERBOSE
float PACControlClient::readSingleFloatVariableByTag(const string& tag_name)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected) {
        cerr << "No conectado al PAC" << endl;
        return 0.0f;
    }

    DEBUG_INFO("📊 LEYENDO VARIABLE FLOAT INDIVIDUAL: " << tag_name);

    // COMANDO CORRECTO confirmado por pruebas Python
    string command = "^" + tag_name + " @@ F.\r";
    
    DEBUG_VERBOSE("📋 Enviando comando: '" << command.substr(0, command.length()-1) << "\\r'");

    flushSocketBuffer();
    
    if (!sendCommand(command)) {
        cerr << "❌ Error enviando comando variable float individual" << endl;
        return 0.0f;
    }

    // Recibir respuesta ASCII terminada en espacio 0x20
    vector<uint8_t> raw_data = receiveASCIIResponse();
    if (raw_data.empty()) {
        cerr << "❌ Error: respuesta vacía para variable float individual" << endl;
        return 0.0f;
    }

    // 🔍 DIAGNÓSTICO: Mostrar datos RAW recibidos
    DEBUG_VERBOSE("🔍 VARIABLE FLOAT RAW DATA (" << raw_data.size() << " bytes): ");
    for (size_t i = 0; i < raw_data.size(); i++) {
        DEBUG_VERBOSE(hex << setfill('0') << setw(2) << (int)raw_data[i] << " ");
    }
    DEBUG_VERBOSE(dec);

    // Convertir bytes a string ASCII limpio
    string ascii_response = convertBytesToASCII(raw_data);
    DEBUG_VERBOSE("🔍 ASCII RESPONSE: '" << ascii_response << "'");
    
    // Limpiar y extraer número (ahora con soporte correcto para notación científica)
    string clean_value = cleanASCIINumber(ascii_response);
    DEBUG_VERBOSE("🔍 CLEAN VALUE: '" << clean_value << "'");
    
    if (clean_value.empty() || clean_value == "0") {
        DEBUG_VERBOSE("⚠️  ADVERTENCIA: Valor limpio vacío o cero para " << tag_name);
        return 0.0f;
    }
    
    // CORRECCIÓN: Usar función mejorada de conversión
    float value = convertStringToFloat(clean_value);
    
    DEBUG_INFO("✅ Variable float individual leída: " << tag_name << " = " << value);
    
    // Mostrar también en notación científica si es un número grande
    if (abs(value) >= 1000) {
        DEBUG_VERBOSE(" (científica: " << scientific << value << fixed << ")");
    }

    return value;
}


// CORRECCIÓN: Función readSingleInt32VariableByTag con DEBUG_VERBOSE
int32_t PACControlClient::readSingleInt32VariableByTag(const string& tag_name)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected) {
        cerr << "No conectado al PAC" << endl;
        return 0;
    }

    DEBUG_INFO("📊 LEYENDO VARIABLE INT32 INDIVIDUAL: " << tag_name);

    // COMANDO CORRECTO para int32 (sin F)
    string command = "^" + tag_name + " @@ .\r";
    
    DEBUG_VERBOSE("📋 Enviando comando: '" << command.substr(0, command.length()-1) << "\\r'");

    flushSocketBuffer();
    
    if (!sendCommand(command)) {
        cerr << "❌ Error enviando comando variable int32 individual" << endl;
        return 0;
    }

    // Recibir respuesta ASCII terminada en espacio 0x20
    vector<uint8_t> raw_data = receiveASCIIResponse();
    if (raw_data.empty()) {
        cerr << "❌ Error: respuesta vacía para variable int32 individual" << endl;
        return 0;
    }

    // 🔍 DIAGNÓSTICO: Mostrar datos RAW recibidos
    DEBUG_VERBOSE("🔍 VARIABLE INT32 RAW DATA (" << raw_data.size() << " bytes): ");
    for (size_t i = 0; i < raw_data.size(); i++) {
        DEBUG_VERBOSE(hex << setfill('0') << setw(2) << (int)raw_data[i] << " ");
    }
    DEBUG_VERBOSE(dec);

    // Convertir bytes a string ASCII limpio
    string ascii_response = convertBytesToASCII(raw_data);
    DEBUG_VERBOSE("🔍 ASCII RESPONSE: '" << ascii_response << "'");
    
    string clean_value = cleanASCIINumber(ascii_response);
    DEBUG_VERBOSE("🔍 CLEAN VALUE: '" << clean_value << "'");
    
    if (clean_value.empty() || clean_value == "0") {
        DEBUG_VERBOSE("⚠️  ADVERTENCIA: Valor limpio vacío o cero para " << tag_name);
        return 0;
    }
    
    // CORRECCIÓN: Usar función mejorada de conversión
    int32_t value = convertStringToInt32(clean_value);
    
    DEBUG_INFO("✅ Variable int32 individual leída: " << tag_name << " = " << value 
         << " (0x" << hex << value << dec << ")");

    return value;
}

// CORRECCIÓN: Función receiveASCIIResponse con DEBUG_VERBOSE
vector<uint8_t> PACControlClient::receiveASCIIResponse()
{
    vector<uint8_t> raw_data;
    char byte;
    
    auto start_time = chrono::steady_clock::now();
    const int timeout_ms = 3000; // 3 segundos timeout

    DEBUG_VERBOSE("📋 Esperando respuesta ASCII (terminador: espacio 0x20)...");
    
    while (true) {
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            chrono::steady_clock::now() - start_time).count();
        
        if (elapsed > timeout_ms) {
            DEBUG_VERBOSE("⏰ TIMEOUT recibiendo respuesta ASCII después de " << elapsed << "ms");
            break;
        }
        
        ssize_t bytes = recv(sock, &byte, 1, 0); // Bloqueante para mejor estabilidad
        
        if (bytes == 1) {
            // CORRECCIÓN: Detectar terminador espacio 0x20
            if (static_cast<uint8_t>(byte) == 0x20) {
                DEBUG_VERBOSE("📋 Terminador espacio (0x20) detectado - Fin de respuesta");
                break;
            }
            
            // Agregar byte a la respuesta
            raw_data.push_back(static_cast<uint8_t>(byte));
            
            // Debug: mostrar cada byte recibido
            DEBUG_VERBOSE("📡 Byte recibido: 0x" << hex << setfill('0') << setw(2) 
                 << (int)(uint8_t)byte << dec);
            if (byte >= 32 && byte <= 126) {
                DEBUG_VERBOSE(" ('" << byte << "')");
            }
            
            // Protección contra respuestas muy largas
            if (raw_data.size() > 50) {
                DEBUG_VERBOSE("⚠️  Respuesta muy larga (>50 bytes), cortando");
                break;
            }
            
        } else if (bytes == 0) {
            DEBUG_VERBOSE("🔌 Conexión cerrada por el PAC");
            connected = false;
            break;
        } else if (bytes < 0) {
            DEBUG_VERBOSE("❌ Error en recv: errno=" << errno);
            connected = false;
            break;
        }
    }
    
    DEBUG_VERBOSE("📋 Respuesta ASCII completa recibida: " << raw_data.size() << " bytes");
    
    // Mostrar datos completos en hex
    DEBUG_VERBOSE("🔍 Datos hex: ");
    for (uint8_t b : raw_data) {
        DEBUG_VERBOSE(hex << setfill('0') << setw(2) << (int)b << " ");
    }
    DEBUG_VERBOSE(dec);
    
    return raw_data;
}

// Función para convertir bytes a string ASCII
string PACControlClient::convertBytesToASCII(const vector<uint8_t>& bytes)
{
    string result;
    for (uint8_t byte : bytes) {
        if (byte >= 32 && byte <= 126) { // Solo caracteres ASCII imprimibles
            result += static_cast<char>(byte);
        }
    }
    return result;
}

// NUEVA FUNCIÓN: Función auxiliar para validación de variable individual
bool PACControlClient::validateSingleVariableIntegrity(const vector<uint8_t>& data, 
                                                      const string& tag_name) {
    if (data.empty()) {
        cout << "⚠️  VALIDACIÓN INDIVIDUAL FALLIDA: " << tag_name << " - Datos vacíos" << endl;
        return false;
    }
    
    // Para variables individuales, esperamos datos ASCII válidos
    bool has_ascii_printable = false;
    for (uint8_t byte : data) {
        if (byte >= 32 && byte <= 126) { // Caracteres ASCII imprimibles
            has_ascii_printable = true;
            break;
        }
    }
    
    if (!has_ascii_printable) {
        cout << "⚠️  VALIDACIÓN INDIVIDUAL FALLIDA: " << tag_name << " - Sin caracteres ASCII válidos" << endl;
        return false;
    }
    
    cout << "✅ VALIDACIÓN INDIVIDUAL OK: " << tag_name << " - Datos ASCII válidos" << endl;
    return true;
}


string PACControlClient::sendRawCommand(const string &command)
{
    lock_guard<mutex> lock(comm_mutex);

    if (!connected)
        return "";

    string full_cmd = command + "\r";
    if (!sendCommand(full_cmd))
        return "";

    return receiveResponse();
}
