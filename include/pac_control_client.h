#ifndef PAC_CONTROL_CLIENT_H
#define PAC_CONTROL_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <mutex>
#include <chrono>

using namespace std; 
// Estructuras para manejo de alarmas PAC Control
struct BitsAlarm_t {
    uint32_t value; // Valor entero de 32 bits
    
    // Métodos para acceder a bits específicos
    bool getInput()

 const { return (value & 0x0001) != 0; }
    bool getModeNCNO() const { return (value & 0x0002) != 0; }
    bool getLogicNC() const { return (value & 0x0004) != 0; }
    bool getLogicNO() const { return (value & 0x0008) != 0; }
    bool getEnableSIM() const { return (value & 0x0010) != 0; }
    bool getSimValue() const { return (value & 0x0020) != 0; }
    bool getHMIBypass() const { return (value & 0x0040) != 0; }
    bool getLogicBypass() const { return (value & 0x0080) != 0; }
    bool getDisableAlarm() const { return (value & 0x0100) != 0; }
    bool getAlarm() const { return (value & 0x0200) != 0; }
    bool getLatch() const { return (value & 0x0400) != 0; }
    bool getDisableFirstOut() const { return (value & 0x0800) != 0; }
    bool getACK() const { return (value & 0x1000) != 0; }
    bool getReset() const { return (value & 0x8000) != 0; }
 
    // Métodos para establecer bits
    void setInput(bool b) { value = b ? (value | 0x0001) : (value & ~0x0001); }
    void setAlarm(bool b) { value = b ? (value | 0x0200) : (value & ~0x0200); }
    void setACK(bool b) { value = b ? (value | 0x1000) : (value & ~0x1000); }
    void setReset(bool b) { value = b ? (value | 0x8000) : (value & ~0x8000); }
    
    BitsAlarm_t() : value(0) {}
    BitsAlarm_t(uint32_t val) : value(val) {}
};

struct TBL_ALARM_t {
    uint32_t TBL[10];   // Array de 10 enteros que contiene los datos de alarma
    
    // Métodos para acceder a alarmas específicas
    BitsAlarm_t getHH() const { return BitsAlarm_t(TBL[0]); }
    BitsAlarm_t getH() const { return BitsAlarm_t(TBL[1]); }
    BitsAlarm_t getL() const { return BitsAlarm_t(TBL[2]); }
    BitsAlarm_t getLL() const { return BitsAlarm_t(TBL[3]); }
    uint32_t getColor() const { return TBL[4]; }
    
    void setHH(const BitsAlarm_t& alarm) { TBL[0] = alarm.value; }
    void setH(const BitsAlarm_t& alarm) { TBL[1] = alarm.value; }
    void setL(const BitsAlarm_t& alarm) { TBL[2] = alarm.value; }
    void setLL(const BitsAlarm_t& alarm) { TBL[3] = alarm.value; }
    void setColor(uint32_t color) { TBL[4] = color; }
    
    TBL_ALARM_t() { memset(TBL, 0, sizeof(TBL)); }
};

/**
 * Cliente PAC Control para comunicación con controlador Opto22
 * Implementa el protocolo reverse-engineered completamente
 */
class PACControlClient {
private:
    string pac_ip;
    int pac_port;
    int sock;
    bool connected;
    bool cache_enabled; // Control del sistema de cache
    mutex comm_mutex;
    
    // Cache para optimizar lecturas
    struct CacheEntry {
        vector<float> data;
        chrono::steady_clock::time_point timestamp;
        bool valid;
    };
    map<string, CacheEntry> table_cache;
    const int CACHE_TIMEOUT_MS = 5000; // 5 segundos cache timeout para valores estables
     // ...existing methods...
    
    // NUEVAS FUNCIONES para manejo ASCII
    vector<uint8_t> receiveASCIIResponse();
    string convertBytesToASCII(const vector<uint8_t>& bytes);
    string cleanASCIINumber(const string& ascii_str);
    // Historial para análisis de estabilidad
    map<string, vector<vector<uint8_t>>> read_history;
public:
    PACControlClient(const string& ip, int port = 22001);
    ~PACControlClient();
    
    // Gestión de conexión
    bool connect();
    void disconnect();
    bool isConnected() const { return connected; }
    
    // Lectura de tablas completas (protocolo completamente descifrado)
    vector<float> readFloatTable(const string& table_name, 
                                     int start_pos = 0, int end_pos = 9);
    
    // Lectura inteligente de tablas (auto-detecta tipo de datos)
    vector<float> readTableAsFloat(const string& table_name, 
                                       int start_pos = 0, int end_pos = 9);
    vector<int32_t> readTableAsInt32(const string& table_name, 
                                         int start_pos = 0, int end_pos = 9);
    
    // Lectura de variables individuales (float e int32)
    float readFloatVariable(const string& table_name, int index);
    int32_t readInt32Variable(const string& table_name, int index);
    
    // Lectura de tablas completas
    vector<int32_t> readInt32Table(const string& table_name, int start_pos = 0, int end_pos = 9);
    
    // Escritura de variables (float e int32)
    bool writeFloatVariable(const string& table_name, int index, float value);
    bool writeInt32Variable(const string& table_name, int index, int32_t value);
    
    // Escritura de tablas completas
    bool writeFloatTable(const string& table_name, const vector<float>& values);
    bool writeInt32Table(const string& table_name, const vector<int32_t>& values);
    
    // Comandos básicos descubiertos del análisis anterior
    vector<string> getTasks();
    string queryVariable(const string& varName);
    string getVariableStatus(const string& varName);
    
    // Utilidades
    void clearCache();
    void setIP(const string& ip) { pac_ip = ip; }
    string getIP() const { return pac_ip; }
    string sendRawCommand(const string& command);
    
    // Control del cache para diagnóstico
    void enableCache(bool enabled = true) { cache_enabled = enabled; }
    bool isCacheEnabled() const { return cache_enabled; }
    
    // Análisis automático de tipo de datos
    bool detectDataType(const string& table_name, bool& is_integer_data);
    
    // Análisis de estabilidad de datos
    void analyzeDataStability(const string& table_name, const vector<uint8_t>& raw_data);

    // 🔧 NUEVOS MÉTODOS PARA EVITAR INTERFERENCIAS (públicos para testing)
    void flushSocketBuffer();  // Limpiar buffer del socket
    bool validateDataIntegrity(const vector<uint8_t>& data, const string& table_name);  // Validar integridad
    vector<float> convertBytesToFloats(const vector<uint8_t>& bytes);  // Expuesto para testing
    float readSingleFloatVariableByTag(const string& tag_name);
    int32_t readSingleInt32VariableByTag(const string& tag_name);
    map<string, float> readMultipleSingleVariables(const vector<pair<string, string>>& variables);
    bool writeSingleFloatVariable(const std::string& variable_name, float value);
    bool writeSingleInt32Variable(const std::string& variable_name, int32_t value);
    bool writeFloatTableIndex(const std::string& table_name, int index, float value);    
    bool writeInt32TableIndex(const std::string& table_name, int index, int32_t value);

private:
    bool sendCommand(const string& command);
    vector<uint8_t> receiveData(size_t expected_bytes);    
    vector<uint8_t> convertInt32sToBytes(const vector<int32_t>& ints);  
    vector<int32_t> convertBytesToInt32s(const vector<uint8_t>& bytes);
    vector<uint8_t> convertFloatsToBytes(const vector<float>& floats);
   

    // NUEVAS: Funciones de conversión robustas
    float convertStringToFloat(const string& str);           // Maneja notación científica
    int32_t convertStringToInt32(const string& str);  
    bool isCacheValid(const string& key);
    string receiveResponse();
    bool validateSingleVariableIntegrity(const vector<uint8_t>& data, 
                                        const string& tag_name);
};

#endif // PAC_CONTROL_CLIENT_H
