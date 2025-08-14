#include "pac_control_client.h"
#include "common.h"
#include <open62541/server.h>
#include <open62541/plugin/log_stdout.h>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <map>
#include <cmath>

using namespace std;
using json = nlohmann::json;

// Variables globales del servidor
bool server_running = true;
UA_Server *server = nullptr;
atomic<bool> running{true};
mutex serverMutex;

// Estructura para definir una propiedad de un TAG
struct TagProperty {
    std::string name;
    UA_Variant value;
};

// Cliente PAC Control global
unique_ptr<PACControlClient> pacClient;

// NUEVAS ESTRUCTURAS PARA VARIABLES INDIVIDUALES
struct FloatVariable {
    string name;         // Nombre en OPC-UA
    string pac_tag;      // Nombre real del TAG en el PAC
    string description;  // Descripción
};

struct Int32Variable {
    string name;         // Nombre en OPC-UA
    string pac_tag;      // Nombre real del TAG en el PAC
    string description;  // Descripción
};

// Estructuras para configuración dinámica
struct TagConfig {
    string name;
    string description;
    string value_table;
    string alarm_table;
    vector<string> variables;
    vector<string> alarms;
    vector<FloatVariable> float_variables;  // NUEVO
    vector<Int32Variable> int32_variables;  // NUEVO
};

struct PACConfig {
    string ip = "192.168.1.30";
    int port = 22001;
    int timeout_ms = 5000;
};

struct OPCUAVariable {
    string full_name;        // ej: "TT_11001.PV"
    string tag_name;         // ej: "TT_11001"
    string var_name;         // ej: "PV"
    string table_name;       // ej: "TBL_TT_11001"
    string type;             // "float", "int32", "single_float", "single_int32"
    string pac_tag;          // Para variables individuales
    UA_NodeId nodeId;
    bool has_nodeId = false;
};

struct DynamicConfig {
    PACConfig pac_config;
    int opcua_port = 4840;
    int update_interval_ms = 2000;
    vector<TagConfig> tags;
    vector<OPCUAVariable> variables;
    vector<FloatVariable> global_float_variables;   // NUEVO: Variables float globales
    vector<Int32Variable> global_int32_variables;   // NUEVO: Variables int32 globales
} config;

// Funciones para cargar configuración
bool loadConfigFromFiles();
void createDynamicMappings();
void updateSingleVariables();

// Funciones auxiliares para actualización de datos
int getVariableIndex(const string& varName);
void updateTableVariables(const string& table_name, const vector<int>& indices);
void updateFloatVariables(const string& table_name, const vector<float>& values, int min_index);
void updateInt32Variables(const string& table_name, const vector<int32_t>& values, int min_index);

void addTagNodeWithProperties(UA_Server *server, 
                              const std::string &tagName, 
                              const std::vector<TagProperty> &properties) 
{
    cout << "🌳 CREANDO ÁRBOL OPC-UA: " << tagName << " con " << properties.size() << " propiedades" << endl;
    
    // Crear nodo objeto para el TAG
    UA_NodeId tagNodeId;
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = UA_LOCALIZEDTEXT("es-ES", const_cast<char*>(tagName.c_str()));
    
    UA_StatusCode result = UA_Server_addObjectNode(
        server,
        UA_NODEID_STRING(1, const_cast<char*>(tagName.c_str())), // NodeId explícito para el TAG
        UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
        UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
        UA_QUALIFIEDNAME(1, const_cast<char*>(tagName.c_str())),
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
        oAttr, NULL, &tagNodeId
    );
    
    if (result != UA_STATUSCODE_GOOD) {
        cout << "❌ ERROR creando nodo TAG: " << tagName << " - código: " << result << endl;
        return;
    }
    
    cout << "✅ Nodo TAG creado: " << tagName << endl;

    // Agregar cada propiedad como variable hija con NodeId explícito
    for(const auto& prop : properties) {
        UA_VariableAttributes vAttr = UA_VariableAttributes_default;
        vAttr.displayName = UA_LOCALIZEDTEXT("es-ES", const_cast<char*>(prop.name.c_str()));
        vAttr.value = prop.value;
        vAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
        
        // NodeId explícito: "TagName.PropertyName"
        std::string nodeIdStr = tagName + "." + prop.name;
        UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char*>(nodeIdStr.c_str()));
        
        UA_StatusCode propResult = UA_Server_addVariableNode(
            server,
            nodeId, // <-- NodeId explícito
            tagNodeId, // Padre: el nodo del TAG
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(1, const_cast<char*>(prop.name.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            vAttr, NULL, NULL
        );
        
        if (propResult != UA_STATUSCODE_GOOD) {
            cout << "❌ ERROR creando propiedad: " << nodeIdStr << " - código: " << propResult << endl;
        } else {
            cout << "  ✅ Propiedad creada: " << nodeIdStr << endl;
        }
    }
    
    cout << "🎯 TAG COMPLETADO: " << tagName << " con estructura de árbol" << endl;
}

// Función para establecer estado de calidad de una variable OPC UA
void setVariableQuality(const std::string& nodeId, UA_StatusCode quality) {
    UA_NodeId varNodeId = UA_NODEID_STRING(1, const_cast<char*>(nodeId.c_str()));
    
    // Leer valor actual
    UA_Variant value;
    UA_Variant_init(&value);
    UA_Server_readValue(server, varNodeId, &value);
    
    // Crear DataValue con estado de calidad
    UA_DataValue dataValue;
    UA_DataValue_init(&dataValue);
    dataValue.value = value;
    dataValue.hasValue = true;
    dataValue.hasStatus = true;
    dataValue.status = quality;
    dataValue.hasSourceTimestamp = true;
    dataValue.sourceTimestamp = UA_DateTime_now();
    
    // Escribir con estado de calidad
    UA_Server_writeDataValue(server, varNodeId, dataValue);
    
    // No limpiar value ya que se comparte con dataValue
    dataValue.hasValue = false;
    UA_DataValue_clear(&dataValue);
}

// Función para marcar todas las variables como "BAD" cuando no hay conexión PAC
void markAllVariablesBad() {
    DEBUG_INFO("⚠️  Marcando variables como BAD - Sin conexión PAC");
    
    for (const auto& var : config.variables) {
        if (var.has_nodeId) {
            setVariableQuality(var.full_name, UA_STATUSCODE_BADCOMMUNICATIONERROR);
        }
    }
}

// Función para cargar configuración desde archivos
bool loadConfigFromFiles() {
    DEBUG_INFO("📄 Cargando configuración dinámica desde pac_config copy.json...");
    
    try {
        // Cargar pac_config copy.json
        ifstream configFile("pac_config copy.json");
        if (!configFile.is_open()) {
            DEBUG_INFO("❌ Error: No se pudo abrir pac_config copy.json");
            return false;
        }
        
        json configJson;
        configFile >> configJson;
        configFile.close();
        
        // Extraer configuración del PAC
        if (configJson.contains("pac_config")) {
            auto& pacConfig = configJson["pac_config"];
            config.pac_config.ip = pacConfig.value("ip", "192.168.1.30");
            config.pac_config.port = pacConfig.value("port", 22001);
            config.pac_config.timeout_ms = pacConfig.value("timeout_ms", 5000);
        }
        
        // Extraer configuración del servidor OPC UA
        if (configJson.contains("server_config")) {
            auto& serverConfig = configJson["server_config"];
            config.opcua_port = serverConfig.value("opcua_port", 4840);
            config.update_interval_ms = serverConfig.value("update_interval_ms", 2000);
            DEBUG_INFO("📊 Configuración del servidor: Puerto OPC UA = " << config.opcua_port 
                 << ", Intervalo actualización = " << config.update_interval_ms << "ms");
        } else {
            config.update_interval_ms = 2000;
            DEBUG_INFO("📊 Usando configuración por defecto: Intervalo actualización = " << config.update_interval_ms << "ms");
        }
        
        // Limpiar configuración anterior
        config.tags.clear();
        config.variables.clear();
        config.global_float_variables.clear();
        config.global_int32_variables.clear();
        
        // Extraer configuración de tags (cambiar "tags" por "tbL_tags")
        if (configJson.contains("tbL_tags")) {
            DEBUG_INFO("📋 Procesando " << configJson["tbL_tags"].size() << " tags encontrados en JSON...");
            for (const auto& tagJson : configJson["tbL_tags"]) {
                TagConfig tag;
                tag.name = tagJson.value("name", "");
                tag.description = tagJson.value("description", "");
                tag.value_table = tagJson.value("value_table", "");
                tag.alarm_table = tagJson.value("alarm_table", "");
                
                DEBUG_INFO("📋 Procesando TAG: " << tag.name << " con tabla valores: " << tag.value_table);
                
                // Cargar variables de tabla
                if (tagJson.contains("variables")) {
                    for (const auto& var : tagJson["variables"]) {
                        tag.variables.push_back(var);
                    }
                    DEBUG_INFO("   - Variables de tabla: " << tag.variables.size());
                }
                
                // Cargar alarmas
                if (tagJson.contains("alarms")) {
                    for (const auto& alarm : tagJson["alarms"]) {
                        tag.alarms.push_back(alarm);
                    }
                    DEBUG_INFO("   - Alarmas: " << tag.alarms.size());
                }
                
                // NUEVO: Cargar variables float individuales del TAG
                if (tagJson.contains("float_variables")) {
                    for (const auto& var_json : tagJson["float_variables"]) {
                        FloatVariable float_var;
                        float_var.name = var_json["name"];
                        float_var.pac_tag = var_json["pac_tag"];
                        float_var.description = var_json["description"];
                        tag.float_variables.push_back(float_var);
                    }
                    DEBUG_INFO("   - Variables float individuales: " << tag.float_variables.size());
                }
                
                // NUEVO: Cargar variables int32 individuales del TAG
                if (tagJson.contains("int32_variables")) {
                    for (const auto& var_json : tagJson["int32_variables"]) {
                        Int32Variable int32_var;
                        int32_var.name = var_json["name"];
                        int32_var.pac_tag = var_json["pac_tag"];
                        int32_var.description = var_json["description"];
                        tag.int32_variables.push_back(int32_var);
                    }
                    DEBUG_INFO("   - Variables int32 individuales: " << tag.int32_variables.size());
                }
                
                config.tags.push_back(tag);
            }
        }
        
        // NUEVO: Cargar variables float globales
        if (configJson.contains("float_variables")) {
            DEBUG_INFO("📊 Procesando " << configJson["float_variables"].size() << " variables float globales...");
            for (const auto& var_json : configJson["float_variables"]) {
                FloatVariable float_var;
                float_var.name = var_json["name"];
                float_var.pac_tag = var_json["pac_tag"];
                float_var.description = var_json["description"];
                config.global_float_variables.push_back(float_var);
                DEBUG_INFO("   ✓ Variable float global: " << float_var.name << " -> " << float_var.pac_tag);
            }
        }
        
        // NUEVO: Cargar variables int32 globales
        if (configJson.contains("int32_variables")) {
            DEBUG_INFO("📊 Procesando " << configJson["int32_variables"].size() << " variables int32 globales...");
            for (const auto& var_json : configJson["int32_variables"]) {
                Int32Variable int32_var;
                int32_var.name = var_json["name"];
                int32_var.pac_tag = var_json["pac_tag"];
                int32_var.description = var_json["description"];
                config.global_int32_variables.push_back(int32_var);
                DEBUG_INFO("   ✓ Variable int32 global: " << int32_var.name << " -> " << int32_var.pac_tag);
            }
        }
        
        DEBUG_INFO("✓ Configuración cargada: " << config.tags.size() << " tags");
        DEBUG_INFO("✓ Variables float globales: " << config.global_float_variables.size());
        DEBUG_INFO("✓ Variables int32 globales: " << config.global_int32_variables.size());
        
        return true;
        
    } catch (const exception& e) {
        DEBUG_INFO("❌ Error cargando configuración: " << e.what());
        return false;
    }
}

void createDynamicMappings() {
    DEBUG_INFO("🏗️  Creando mapeos dinámicos de variables...");
    DEBUG_INFO("📊 Número de TAGs a procesar: " << config.tags.size());
    
    config.variables.clear();
    
    // Mapear variables de TAGs
    for (const auto& tag : config.tags) {
        DEBUG_INFO("🏷️  Procesando TAG: " << tag.name);
        
        // Crear variables de valores (float) - de tablas
        for (const auto& varName : tag.variables) {
            OPCUAVariable var;
            var.full_name = tag.name + "." + varName;
            var.tag_name = tag.name;
            var.var_name = varName;
            var.table_name = tag.value_table;
            var.type = "float";
            var.pac_tag = "";  // CORRECCIÓN: Inicializar explícitamente
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Variable tabla creada: " << var.full_name);
        }
        
        // Crear variables de alarmas (int32) - de tablas
        for (const auto& alarmName : tag.alarms) {
            OPCUAVariable var;
            var.full_name = tag.name + "." + alarmName;
            var.tag_name = tag.name;
            var.var_name = alarmName;
            var.table_name = tag.alarm_table;
            var.type = "int32";
            var.pac_tag = "";  // CORRECCIÓN: Inicializar explícitamente
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Alarma tabla creada: " << var.full_name);
        }
        
        // NUEVO: Crear variables float individuales del TAG
        for (const auto& floatVar : tag.float_variables) {
            OPCUAVariable var;
            var.full_name = tag.name + "." + floatVar.name;
            var.tag_name = tag.name;
            var.var_name = floatVar.name;
            var.table_name = "";  // CORRECCIÓN: Variables individuales no tienen tabla
            var.pac_tag = floatVar.pac_tag;
            var.type = "single_float";
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Variable float individual creada: " << var.full_name << " -> " << var.pac_tag);
        }
        
        // NUEVO: Crear variables int32 individuales del TAG
        for (const auto& int32Var : tag.int32_variables) {
            OPCUAVariable var;
            var.full_name = tag.name + "." + int32Var.name;
            var.tag_name = tag.name;
            var.var_name = int32Var.name;
            var.table_name = "";  // CORRECCIÓN: Variables individuales no tienen tabla
            var.pac_tag = int32Var.pac_tag;
            var.type = "single_int32";
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Variable int32 individual creada: " << var.full_name << " -> " << var.pac_tag);
        }
    }
    
    // NUEVO: Mapear variables globales como TAG "Sistema_General"
    if (!config.global_float_variables.empty() || !config.global_int32_variables.empty()) {
        DEBUG_INFO("🌐 Creando TAG global: Sistema_General");
        
        for (const auto& floatVar : config.global_float_variables) {
            OPCUAVariable var;
            var.full_name = "Sistema_General." + floatVar.name;
            var.tag_name = "Sistema_General";
            var.var_name = floatVar.name;
            var.table_name = "";  // CORRECCIÓN: Variables globales no tienen tabla
            var.pac_tag = floatVar.pac_tag;
            var.type = "single_float";
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Variable float global creada: " << var.full_name << " -> " << var.pac_tag);
        }
        
        for (const auto& int32Var : config.global_int32_variables) {
            OPCUAVariable var;
            var.full_name = "Sistema_General." + int32Var.name;
            var.tag_name = "Sistema_General";
            var.var_name = int32Var.name;
            var.table_name = "";  // CORRECCIÓN: Variables globales no tienen tabla
            var.pac_tag = int32Var.pac_tag;
            var.type = "single_int32";
            var.has_nodeId = false;
            config.variables.push_back(var);
            DEBUG_INFO("   ✓ Variable int32 global creada: " << var.full_name << " -> " << var.pac_tag);
        }
    }
    
    DEBUG_INFO("✓ Variables mapeadas dinámicamente: " << config.variables.size());
}

// CORRECCIÓN 2: Función para crear nodos OPC UA con gestión segura de memoria
void createOPCUANodes() {
    DEBUG_INFO("🏗️  Creando nodos OPC UA...");
    
    // Mapa para agrupar variables por TAG
    std::map<std::string, std::vector<TagProperty>> tagProperties;
    
    // Agrupar variables por TAG
    for (auto& var : config.variables) {
        TagProperty prop;
        prop.name = var.var_name;
        
        // CORRECCIÓN: Configurar tipo de dato según el mapeo de forma segura
        UA_Variant_init(&prop.value);
        if (var.type == "float" || var.type == "single_float") {
            UA_Float* valorInicial = (UA_Float*)UA_malloc(sizeof(UA_Float));
            *valorInicial = 0.0f;
            UA_Variant_setScalar(&prop.value, valorInicial, &UA_TYPES[UA_TYPES_FLOAT]);
        }
        else if (var.type == "int32" || var.type == "single_int32") {
            UA_Int32* valorInicial = (UA_Int32*)UA_malloc(sizeof(UA_Int32));
            *valorInicial = 0;
            UA_Variant_setScalar(&prop.value, valorInicial, &UA_TYPES[UA_TYPES_INT32]);
        }
        
        tagProperties[var.tag_name].push_back(prop);
    }
    
    // Crear TAGs con sus propiedades usando la función existente
    for (const auto& tagPair : tagProperties) {
        const std::string& tagName = tagPair.first;
        const std::vector<TagProperty>& properties = tagPair.second;
        
        DEBUG_INFO("🏷️  Creando TAG: " << tagName << " con " << properties.size() << " propiedades");
        addTagNodeWithProperties(server, tagName, properties);
        
        // Actualizar las NodeId en las variables para referencia posterior
        for (auto& var : config.variables) {
            if (var.tag_name == tagName) {
                var.has_nodeId = true;
                DEBUG_INFO("   ✓ Propiedad: " << var.var_name << " (" << var.type << ")");
            }
        }
        
        // CORRECCIÓN: Limpiar memoria de las propiedades después de usarlas
        for (const auto& prop : properties) {
            UA_Variant_clear(const_cast<UA_Variant*>(&prop.value));
        }
    }
    
    DEBUG_INFO("✓ Creados " << tagProperties.size() << " TAGs con estructura jerárquica");
}

// CORRECCIÓN 3: Función segura para actualizar variables individuales
void updateSingleVariables() {
    if (!pacClient || !pacClient->isConnected()) {
        return;
    }
    
    DEBUG_INFO("🔄 Iniciando actualización de variables individuales...");
    
    for (const auto& var : config.variables) {
        // Solo procesar variables individuales
        if (var.type != "single_float" && var.type != "single_int32") {
            continue;
        }
        
        if (!var.has_nodeId || var.pac_tag.empty()) {
            DEBUG_INFO("⚠️  Saltando variable " << var.full_name << " - sin nodeId o pac_tag vacío");
            continue;
        }
        
        try {
            if (var.type == "single_float") {
                // Usar comando ^<TAG> @@f\r para float
                DEBUG_INFO("📊 Leyendo variable float: " << var.pac_tag);
                float value = pacClient->readSingleFloatVariableByTag(var.pac_tag);
                
                // CORRECCIÓN: Gestión segura de memoria para UA_Variant
                UA_Variant variant;
                UA_Variant_init(&variant);
                
                // Crear copia del valor en memoria dinámica
                UA_Float* floatValue = (UA_Float*)UA_malloc(sizeof(UA_Float));
                *floatValue = static_cast<UA_Float>(value);
                UA_Variant_setScalar(&variant, floatValue, &UA_TYPES[UA_TYPES_FLOAT]);
                
                // Crear NodeId como string temporal
                std::string nodeIdStr = var.full_name;
                UA_NodeId node = UA_NODEID_STRING(1, const_cast<char*>(nodeIdStr.c_str()));
                
                UA_StatusCode result = UA_Server_writeValue(server, node, variant);
                
                if (result == UA_STATUSCODE_GOOD) {
                    DEBUG_INFO("✅ Variable FLOAT individual actualizada: " << var.full_name << " = " << value);
                } else {
                    cout << "❌ ERROR actualizando variable FLOAT individual: " << var.full_name << " código: " << result << endl;
                }
                
                // CORRECCIÓN: Limpiar memoria de forma segura
                UA_Variant_clear(&variant);
                
            } else if (var.type == "single_int32") {
                // Usar comando ^<TAG> @@\r para int32
                DEBUG_INFO("📊 Leyendo variable int32: " << var.pac_tag);
                int32_t value = pacClient->readSingleInt32VariableByTag(var.pac_tag);
                
                // CORRECCIÓN: Gestión segura de memoria para UA_Variant
                UA_Variant variant;
                UA_Variant_init(&variant);
                
                // Crear copia del valor en memoria dinámica
                UA_Int32* intValue = (UA_Int32*)UA_malloc(sizeof(UA_Int32));
                *intValue = static_cast<UA_Int32>(value);
                UA_Variant_setScalar(&variant, intValue, &UA_TYPES[UA_TYPES_INT32]);
                
                // Crear NodeId como string temporal
                std::string nodeIdStr = var.full_name;
                UA_NodeId node = UA_NODEID_STRING(1, const_cast<char*>(nodeIdStr.c_str()));
                
                UA_StatusCode result = UA_Server_writeValue(server, node, variant);
                
                if (result == UA_STATUSCODE_GOOD) {
                    DEBUG_INFO("✅ Variable INT32 individual actualizada: " << var.full_name << " = " << value);
                } else {
                    cout << "❌ ERROR actualizando variable INT32 individual: " << var.full_name << " código: " << result << endl;
                }
                
                // CORRECCIÓN: Limpiar memoria de forma segura
                UA_Variant_clear(&variant);
            }
        } catch (const exception& e) {
            cout << "❌ EXCEPCIÓN leyendo variable individual " << var.pac_tag << ": " << e.what() << endl;
        }
        
        // Delay entre variables para no saturar el PAC
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    DEBUG_INFO("✓ Actualización de variables individuales completada");
}
// Hilo de actualización de datos desde PAC Control
void updateDataFromPAC() {
    cout << "🔄 Iniciando hilo de actualización PAC Control..." << endl;
    bool pac_was_connected = false;
    
    while (running && server_running) {
        bool pac_is_connected = pacClient && pacClient->isConnected();
        
        // Detectar cambio de estado de conexión
        if (pac_was_connected && !pac_is_connected) {
            cout << "❌ Conexión PAC perdida - Marcando variables como BAD" << endl;
            markAllVariablesBad();
        } else if (!pac_was_connected && pac_is_connected) {
            cout << "✅ Conexión PAC restablecida - Variables funcionando" << endl;
        }
        
        pac_was_connected = pac_is_connected;
        
        if (pac_is_connected) {
            // Agrupar variables por tabla para lectura eficiente (solo variables de tabla)
            map<string, vector<int>> table_indices;
            
            for (const auto& var : config.variables) {
                // Solo procesar variables de tabla (no individuales)
                if (var.type != "float" && var.type != "int32") {
                    continue;
                }
                
                if (var.has_nodeId && !var.table_name.empty()) {
                    int index = getVariableIndex(var.var_name);
                    if (index >= 0) {
                        table_indices[var.table_name].push_back(index);
                        DEBUG_VERBOSE("🔍 DEBUG: Variable tabla " << var.full_name << " -> Tabla: " << var.table_name << " Índice: " << index << " Tipo: " << var.type);
                    }
                }
            }
            
            // Leer cada tabla y actualizar variables correspondientes
            for (const auto& table_pair : table_indices) {
                if (!running || !server_running) break;
                
                const string& table_name = table_pair.first;
                const vector<int>& indices = table_pair.second;
                
                if (!indices.empty()) {
                    DEBUG_VERBOSE("📋 DEBUG: Procesando tabla " << table_name << " con " << indices.size() << " indices");
                    updateTableVariables(table_name, indices);
                }
            }
            
            // NUEVO: Actualizar variables individuales
            cout << "🔄 Actualizando variables individuales..." << endl;
            updateSingleVariables();
            
        } else {
            // Intentar reconectar cada 10 segundos
            static auto last_reconnect_attempt = chrono::steady_clock::now();
            auto now = chrono::steady_clock::now();
            auto seconds_since_attempt = chrono::duration_cast<chrono::seconds>(now - last_reconnect_attempt).count();
            
            if (seconds_since_attempt >= 10) {
                cout << "🔄 Intentando reconectar a PAC Control..." << endl;
                pacClient.reset();
                pacClient = make_unique<PACControlClient>(config.pac_config.ip, config.pac_config.port);
                if (pacClient->connect()) {
                    cout << "✅ Reconectado a PAC Control exitosamente" << endl;
                } else {
                    cout << "❌ Fallo en reconexión a PAC Control" << endl;
                }
                last_reconnect_attempt = now;
            }
            
            // Sin conexión PAC, pausa más larga
            this_thread::sleep_for(chrono::milliseconds(5000));
        }
        
        // Esperar intervalo de actualización
        for (int i = 0; i < config.update_interval_ms/100 && running && server_running; i++) {
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
    
    cout << "🔄 Hilo de actualización PAC terminado" << endl;
}

void ServerInit() {
    cout << "🚀 Inicializando servidor OPC UA con PAC Control..." << endl;
    
    // Cargar configuración dinámica
    if (!loadConfigFromFiles()) {
        cout << "❌ Error cargando configuración - usando valores por defecto" << endl;
        // Valores por defecto mínimos para que funcione
        config.pac_config.ip = "192.168.0.30";
        config.pac_config.port = 22001;
        config.opcua_port = 4840;
    }
    
    // Crear mapeos dinámicos de variables
    createDynamicMappings();
    
    // Crear servidor OPC UA con configuración por defecto
    server = UA_Server_new();
    
    if (!server) {
        cout << "❌ Error: No se pudo crear el servidor OPC UA" << endl;
        return;
    }
    
    cout << "✓ Servidor OPC UA creado correctamente" << endl;
    cout << "🔧 Usando puerto por defecto 4840" << endl;
    
    // Crear nodos OPC UA inmediatamente (sin esperar conexión PAC)
    cout << "🏗️  Creando nodos OPC UA..." << endl;
    createOPCUANodes();
    
    // Crear cliente PAC Control
    cout << "🔌 Inicializando cliente PAC Control..." << endl;
    pacClient = make_unique<PACControlClient>(config.pac_config.ip, config.pac_config.port);
    
    // Intentar conectar al PAC (no crítico para funcionamiento)
    cout << "🔌 Intentando conectar al PAC Control..." << endl;
    bool pac_connected = pacClient->connect();
    
    if (pac_connected) {
        cout << "✅ Conectado al PAC Control: " << config.pac_config.ip << ":" << config.pac_config.port << endl;
    } else {
        cout << "⚠️  Sin conexión PAC - Servidor funcionará en modo simulación" << endl;
        cout << "   Variables marcadas como BAD hasta conectar PAC" << endl;
        markAllVariablesBad();
    }
    
    cout << "✅ Servidor OPC UA inicializado correctamente" << endl;
    
    // Mostrar resumen de variables
    int table_vars = 0, float_vars = 0, int32_vars = 0;
    for (const auto& var : config.variables) {
        if (var.type == "float" || var.type == "int32") table_vars++;
        else if (var.type == "single_float") float_vars++;
        else if (var.type == "single_int32") int32_vars++;
    }
    cout << "📊 Variables de tabla: " << table_vars << endl;
    cout << "📊 Variables float individuales: " << float_vars << endl;
    cout << "📊 Variables int32 individuales: " << int32_vars << endl;
}

UA_StatusCode runServer() {
    cout << "🔧 Variables mapeadas: " << config.variables.size() << endl;
    cout << "▶️  Ejecutando servidor OPC UA..." << endl;
    cout << "📡 URL: opc.tcp://localhost:4840" << endl;
    
    // Iniciar hilo de actualización de datos PAC
    cout << "🔄 Iniciando hilo de actualización PAC Control..." << endl;
    thread updateThread(updateDataFromPAC);
    
    cout << "🔄 Iniciando servidor OPC UA..." << endl;
    UA_StatusCode retval = UA_Server_run(server, &server_running);
    
    // Esperar a que termine el hilo de actualización
    if (updateThread.joinable()) {
        updateThread.join();
    }
    
    cout << "🔄 Servidor OPC UA detenido - código: " << retval << endl;
    
    // Limpiar servidor
    if (server) {
        UA_Server_delete(server);
        server = nullptr;
    }
    
    cout << "✓ Servidor detenido correctamente" << endl;
    
    return retval;
}

// Función para verificar estado de conexión PAC desde main
bool getPACConnectionStatus() {
    return pacClient && pacClient->isConnected();
}

// Función de limpieza y salida
void cleanupAndExit() {
    cout << "\n🧹 LIMPIEZA Y TERMINACIÓN:" << endl;
    
    // Detener threads y limpiar recursos
    running = false;
    server_running = false;
    
    // Limpiar cliente PAC
    if (pacClient) {
        cout << "• Desconectando cliente PAC..." << endl;
        pacClient.reset();
    }
    
    // Limpiar servidor OPC UA
    if (server) {
        cout << "• Liberando recursos del servidor OPC UA..." << endl;
        UA_Server_delete(server);
        server = nullptr;
    }
    
    cout << "✓ Limpieza completada" << endl;
    cout << "👋 ¡Hasta luego!" << endl;
}

// Función auxiliar para obtener índice de variable en tabla TBL_ANALOG_TAG
int getVariableIndex(const string& varName) {
    // Basado en la estructura TBL_ANALOG_TAG
    if (varName == "Input") return 0;        // Input del sensor
    if (varName == "SetHH") return 1;        // Setpoint alarma muy alta
    if (varName == "SetH") return 2;         // Setpoint alarma alta
    if (varName == "SetL") return 3;         // Setpoint alarma baja
    if (varName == "SetLL") return 4;        // Setpoint alarma muy baja
    if (varName == "SIM_Value") return 5;    // Valor de simulación
    if (varName == "PV") return 6;           // Process Value (Input or SIM_Value)
    if (varName == "Min") return 7;          // Valor mínimo
    if (varName == "Max") return 8;          // Valor máximo
    if (varName == "Percent") return 9;      // Porcentaje del PV
    
    // Variables de alarma (tabla int32)
    if (varName == "ALARM_HH") return 0;
    if (varName == "ALARM_H") return 1;
    if (varName == "ALARM_L") return 2;
    if (varName == "ALARM_LL") return 3;
    if (varName == "COLOR") return 4;
    return -1;
}

// Función auxiliar para actualizar variables de una tabla
void updateTableVariables(const string& table_name, const vector<int>& indices) {
    if (indices.empty()) return;
    
    int min_index = *min_element(indices.begin(), indices.end());
    int max_index = *max_element(indices.begin(), indices.end());
    
    // Determinar si es tabla de floats o int32 basado en el nombre
    bool is_alarm_table = (table_name.find("TBL_DA_") == 0) || 
                         (table_name.find("TBL_PA_") == 0) || 
                         (table_name.find("TBL_LA_") == 0) || 
                         (table_name.find("TBL_TA_") == 0);
    
    cout << "🔍 DEBUG: updateTableVariables - Tabla: " << table_name << " es_alarma: " << (is_alarm_table ? "SI" : "NO") << " min_idx: " << min_index << " max_idx: " << max_index << endl;
    
    if (is_alarm_table) {
        // Leer tabla de alarmas (int32)
        cout << "📊 DEBUG: Leyendo tabla int32: " << table_name << endl;
        vector<int32_t> values = pacClient->readInt32Table(table_name, min_index, max_index);
        if (!values.empty()) {
            cout << "🎯 DATOS LEÍDOS de " << table_name << ": ";
            for (size_t i = 0; i < values.size(); i++) {
                cout << "[" << (min_index + i) << "]=" << values[i] << " ";
            }
            cout << endl;
            updateInt32Variables(table_name, values, min_index);
        } else {
            cout << "⚠️  DEBUG: Tabla int32 vacía: " << table_name << endl;
        }
    } else {
        // Leer tabla de valores (float)
        cout << "📊 DEBUG: Leyendo tabla float: " << table_name << endl;
        vector<float> values = pacClient->readFloatTable(table_name, min_index, max_index);
        if (!values.empty()) {
            cout << "🎯 DATOS LEÍDOS de " << table_name << ": ";
            for (size_t i = 0; i < values.size(); i++) {
                cout << "[" << (min_index + i) << "]=" << values[i] << " ";
            }
            cout << endl;
            updateFloatVariables(table_name, values, min_index);
        } else {
            cout << "⚠️  DEBUG: Tabla float vacía: " << table_name << endl;
        }
        
        this_thread::sleep_for(chrono::milliseconds(50));
    }
}

// CORRECCIÓN 4: Función mejorada para actualizar variables float de tabla
void updateFloatVariables(const string& table_name, const vector<float>& values, int min_index) {
    DEBUG_INFO("🔧 updateFloatVariables: tabla=" << table_name << " values.size()=" << values.size() << " min_index=" << min_index);
    
    for (auto& var : config.variables) {
        if (var.has_nodeId && var.table_name == table_name && var.type == "float") {
            int var_index = getVariableIndex(var.var_name);
            if (var_index >= 0) {
                int array_index = var_index - min_index;
                if (array_index >= 0 && array_index < (int)values.size()) {
                    float new_value = values[array_index];
                    
                    DEBUG_INFO("🔍 MAPEO: " << var.full_name << " <- tabla[" << array_index << "] = " << new_value);
                    
                    // CORRECCIÓN: Usar NodeId temporal para evitar problemas de memoria
                    std::string nodeIdString = var.tag_name + "." + var.var_name;
                    UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char*>(nodeIdString.c_str()));
                    
                    // Leer valor actual para comparar
                    UA_Variant current_value;
                    UA_Variant_init(&current_value);
                    UA_StatusCode read_result = UA_Server_readValue(server, nodeId, &current_value);
                    
                    bool value_changed = true;
                    if (read_result == UA_STATUSCODE_GOOD && current_value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
                        float current_float = *(UA_Float*)current_value.data;
                        value_changed = fabs(current_float - new_value) > 0.001f;
                    }
                    
                    UA_Variant_clear(&current_value);
                    
                    if (value_changed) {
                        // CORRECCIÓN: Crear valor con memoria dinámica
                        UA_Variant value;
                        UA_Variant_init(&value);
                        UA_Float* floatPtr = (UA_Float*)UA_malloc(sizeof(UA_Float));
                        *floatPtr = new_value;
                        UA_Variant_setScalar(&value, floatPtr, &UA_TYPES[UA_TYPES_FLOAT]);
                        
                        UA_StatusCode write_result = UA_Server_writeValue(server, nodeId, value);
                        
                        if (write_result == UA_STATUSCODE_GOOD) {
                            DEBUG_VERBOSE("🔄 CAMBIO: " << var.full_name << " = " << new_value);
                        } else {
                            cout << "❌ ERROR ESCRIBIENDO: " << var.full_name << " código: " << write_result << endl;
                        }
                        
                        // CORRECCIÓN: Limpiar memoria
                        UA_Variant_clear(&value);
                    } else {
                        DEBUG_VERBOSE("✅ SIN CAMBIO: " << var.full_name << " = " << new_value);
                    }
                } else {
                    cout << "❌ ÍNDICE FUERA DE RANGO: " << var.full_name << " array_index=" << array_index << " values.size()=" << values.size() << endl;
                }
            } else {
                cout << "❌ ÍNDICE VARIABLE INVÁLIDO: " << var.full_name << " var_index=" << var_index << endl;
            }
        }
    }
}

// CORRECCIÓN 5: Función mejorada para actualizar variables int32 de tabla
void updateInt32Variables(const string& table_name, const vector<int32_t>& values, int min_index) {
    for (auto& var : config.variables) {
        if (var.has_nodeId && var.table_name == table_name && var.type == "int32") {
            int var_index = getVariableIndex(var.var_name);
            if (var_index >= 0) {
                int array_index = var_index - min_index;
                if (array_index >= 0 && array_index < (int)values.size()) {
                    int32_t new_value = values[array_index];
                    
                    // CORRECCIÓN: Crear NodeId temporal
                    std::string nodeIdString = var.tag_name + "." + var.var_name;
                    UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char*>(nodeIdString.c_str()));
                    
                    UA_Variant current_value;
                    UA_Variant_init(&current_value);
                    UA_StatusCode read_result = UA_Server_readValue(server, nodeId, &current_value);
                    
                    bool value_changed = true;
                    if (read_result == UA_STATUSCODE_GOOD && current_value.type == &UA_TYPES[UA_TYPES_INT32]) {
                        int32_t current_int = *(UA_Int32*)current_value.data;
                        value_changed = (current_int != new_value);
                    }
                    
                    UA_Variant_clear(&current_value);
                    
                    if (value_changed) {
                        // CORRECCIÓN: Crear valor con memoria dinámica
                        UA_Variant value;
                        UA_Variant_init(&value);
                        UA_Int32* intPtr = (UA_Int32*)UA_malloc(sizeof(UA_Int32));
                        *intPtr = new_value;
                        UA_Variant_setScalar(&value, intPtr, &UA_TYPES[UA_TYPES_INT32]);
                        
                        UA_Server_writeValue(server, nodeId, value);
                        DEBUG_VERBOSE("🔄 CAMBIO: " << var.full_name << " = " << new_value);
                        
                        // CORRECCIÓN: Limpiar memoria
                        UA_Variant_clear(&value);
                    } else {
                        DEBUG_VERBOSE("✅ SIN CAMBIO: " << var.full_name << " = " << new_value);
                    }
                }
            }
        }
    }
}
