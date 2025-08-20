#include "opcua_server.h"
#include "pac_control_client.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace std;
using json = nlohmann::json;

// ============== VARIABLES GLOBALES ==============
UA_Server *server = nullptr;
std::atomic<bool> running{true};
bool server_running = true;
std::unique_ptr<PACControlClient> pacClient;
Config config;
std::mutex update_mutex;

// ============== FUNCIONES AUXILIARES ==============

int getVariableIndex(const std::string& varName) {
    // Variables estándar de instrumentos
    if (varName == "Input") return 0;
    if (varName == "SetHH") return 1;
    if (varName == "SetH") return 2;
    if (varName == "SetL") return 3;
    if (varName == "SetLL") return 4;
    if (varName == "SIM_Value") return 5;
    if (varName == "PV") return 6;
    if (varName == "Min") return 7;
    if (varName == "Max") return 8;
    if (varName == "Percent") return 9;
    
    // Variables de alarma
    if (varName == "ALARM_HH") return 0;
    if (varName == "ALARM_H") return 1;
    if (varName == "ALARM_L") return 2;
    if (varName == "ALARM_LL") return 3;
    if (varName == "COLOR") return 4;
    
    return -1;
}

bool isWritableVariable(const std::string& varName) {
    return varName.find("SET") == 0 || 
           varName.find("Set") == 0 || 
           varName.find("SIM_") == 0 ||
           varName.find("E_") == 0;
}

// ============== CONFIGURACIÓN ==============

bool loadConfig() {
    cout << "📄 Cargando configuración..." << endl;
    
    try {
        ifstream configFile("pac_config copy.json");
        if (!configFile.is_open()) {
            cout << "❌ No se pudo abrir pac_config copy.json" << endl;
            return false;
        }
        
        json configJson;
        configFile >> configJson;
        
        // Configuración del PAC
        if (configJson.contains("pac_config")) {
            auto& pac = configJson["pac_config"];
            config.pac_ip = pac.value("ip", "192.168.1.30");
            config.pac_port = pac.value("port", 22001);
        }
        
        // Configuración del servidor
        if (configJson.contains("server_config")) {
            auto& srv = configJson["server_config"];
            config.opcua_port = srv.value("opcua_port", 4840);
            config.update_interval_ms = srv.value("update_interval_ms", 2000);
        }
        
        // Limpiar configuración anterior
        config.tags.clear();
        config.variables.clear();
        
        // Cargar TAGs
        if (configJson.contains("tbL_tags")) {
            for (const auto& tagJson : configJson["tbL_tags"]) {
                Tag tag;
                tag.name = tagJson.value("name", "");
                tag.value_table = tagJson.value("value_table", "");
                tag.alarm_table = tagJson.value("alarm_table", "");
                
                if (tagJson.contains("variables")) {
                    for (const auto& var : tagJson["variables"]) {
                        tag.variables.push_back(var);
                    }
                }
                
                if (tagJson.contains("alarms")) {
                    for (const auto& alarm : tagJson["alarms"]) {
                        tag.alarms.push_back(alarm);
                    }
                }
                
                config.tags.push_back(tag);
            }
        }
        
        // Crear variables desde TAGs
        for (const auto& tag : config.tags) {
            // Variables de valor (float)
            for (const auto& varName : tag.variables) {
                Variable var;
                var.opcua_name = tag.name + "." + varName;
                var.tag_name = tag.name;
                var.var_name = varName;
                var.pac_source = tag.value_table + ":" + to_string(getVariableIndex(varName));
                var.type = Variable::FLOAT;
                var.writable = isWritableVariable(varName);
                config.variables.push_back(var);
            }
            
            // Variables de alarma (int32)
            for (const auto& alarmName : tag.alarms) {
                Variable var;
                var.opcua_name = tag.name + "." + alarmName;
                var.tag_name = tag.name;
                var.var_name = alarmName;
                var.pac_source = tag.alarm_table + ":" + to_string(getVariableIndex(alarmName));
                var.type = Variable::INT32;
                var.writable = false; // Las alarmas son solo lectura
                config.variables.push_back(var);
            }
        }
        
        cout << "✓ Configuración cargada: " << config.tags.size() << " tags, " 
             << config.variables.size() << " variables" << endl;
        return true;
        
    } catch (const exception& e) {
        cout << "❌ Error cargando configuración: " << e.what() << endl;
        return false;
    }
}

// ============== CALLBACK DE ESCRITURA ==============

static void writeCallback(UA_Server *server,
                         const UA_NodeId *sessionId,
                         void *sessionContext,
                         const UA_NodeId *nodeId,
                         void *nodeContext,
                         const UA_NumericRange *range,
                         const UA_DataValue *data) {
    
    if (nodeId->identifierType != UA_NODEIDTYPE_STRING) return;
    
    string nodeIdStr = string((char*)nodeId->identifier.string.data, 
                             nodeId->identifier.string.length);
    
    cout << "✍️ Escritura OPC-UA: " << nodeIdStr << endl;
    
    if (!pacClient || !pacClient->isConnected()) {
        cout << "❌ Sin conexión PAC" << endl;
        return;
    }
    
    // Buscar variable
    Variable* var = nullptr;
    for (auto& v : config.variables) {
        if (v.opcua_name == nodeIdStr) {
            var = &v;
            break;
        }
    }
    
    if (!var || !var->writable) {
        cout << "❌ Variable no escribible: " << nodeIdStr << endl;
        return;
    }
    
    // Escribir al PAC
    bool success = false;
    if (var->type == Variable::FLOAT && data->value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
        float value = *(UA_Float*)data->value.data;
        
        // Extraer tabla e índice del pac_source
        size_t pos = var->pac_source.find(':');
        if (pos != string::npos) {
            string table = var->pac_source.substr(0, pos);
            int index = stoi(var->pac_source.substr(pos + 1));
            success = pacClient->writeFloatTableIndex(table, index, value);
            cout << "📝 Escribiendo " << table << "[" << index << "] = " << value << endl;
        }
    }
    else if (var->type == Variable::INT32 && data->value.type == &UA_TYPES[UA_TYPES_INT32]) {
        int32_t value = *(UA_Int32*)data->value.data;
        
        // Extraer tabla e índice del pac_source
        size_t pos = var->pac_source.find(':');
        if (pos != string::npos) {
            string table = var->pac_source.substr(0, pos);
            int index = stoi(var->pac_source.substr(pos + 1));
            success = pacClient->writeInt32TableIndex(table, index, value);
            cout << "📝 Escribiendo " << table << "[" << index << "] = " << value << endl;
        }
    }
    
    if (success) {
        cout << "✅ Escritura exitosa" << endl;
    } else {
        cout << "❌ Error en escritura" << endl;
    }
}

// ============== CREACIÓN DE NODOS ==============

void createNodes() {
    cout << "🏗️ Creando nodos OPC-UA..." << endl;
    
    // Agrupar variables por TAG
    map<string, vector<Variable*>> tagVars;
    for (auto& var : config.variables) {
        tagVars[var.tag_name].push_back(&var);
    }
    
    // Crear cada TAG con sus variables
    for (const auto& [tagName, variables] : tagVars) {
        // Crear nodo TAG
        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char*>(tagName.c_str()));
        
        UA_NodeId tagNodeId;
        UA_StatusCode result = UA_Server_addObjectNode(
            server,
            UA_NODEID_STRING(1, const_cast<char*>(tagName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(1, const_cast<char*>(tagName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            oAttr, nullptr, &tagNodeId
        );
        
        if (result != UA_STATUSCODE_GOOD) {
            cout << "❌ Error creando TAG: " << tagName << endl;
            continue;
        }
        
        // Crear variables del TAG
        for (auto var : variables) {
            UA_VariableAttributes vAttr = UA_VariableAttributes_default;
            vAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char*>(var->var_name.c_str()));
            
            // Configurar tipo de dato y valor inicial
            UA_Variant value;
            UA_Variant_init(&value);
            
            if (var->type == Variable::FLOAT) {
                float initial = 0.0f;
                UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_FLOAT]);
            } else if (var->type == Variable::INT32) {
                int32_t initial = 0;
                UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_INT32]);
            }
            
            vAttr.value = value;
            vAttr.accessLevel = var->writable ? 
                (UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE) : UA_ACCESSLEVELMASK_READ;
            
            UA_NodeId varNodeId = UA_NODEID_STRING(1, const_cast<char*>(var->opcua_name.c_str()));
            
            result = UA_Server_addVariableNode(
                server,
                varNodeId,
                tagNodeId,
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                UA_QUALIFIEDNAME(1, const_cast<char*>(var->var_name.c_str())),
                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                vAttr, nullptr, nullptr
            );
            
            if (result == UA_STATUSCODE_GOOD) {
                var->has_node = true;
                
                // Agregar callback de escritura si es necesario
                if (var->writable) {
                    UA_ValueCallback callback;
                    callback.onRead = nullptr;
                    callback.onWrite = writeCallback;
                    UA_Server_setVariableNode_valueCallback(server, varNodeId, callback);
                }
                
                cout << "  ✓ " << var->opcua_name << 
                        (var->writable ? " (R/W)" : " (R)") << endl;
            } else {
                cout << "  ❌ Error creando variable: " << var->opcua_name << endl;
            }
        }
    }
    
    cout << "✓ Nodos creados" << endl;
}

// ============== ACTUALIZACIÓN DE DATOS ==============

void updateData() {
    cout << "🔄 Iniciando actualización de datos..." << endl;
    
    while (running && server_running) {
        if (pacClient && pacClient->isConnected()) {
            // Agrupar variables por tabla para lectura eficiente
            map<string, vector<Variable*>> tableVars;
            
            for (auto& var : config.variables) {
                if (!var.has_node) continue;
                
                // Extraer tabla del pac_source
                size_t pos = var.pac_source.find(':');
                if (pos != string::npos) {
                    string table = var.pac_source.substr(0, pos);
                    tableVars[table].push_back(&var);
                }
            }
            
            // Actualizar cada tabla
            for (const auto& [tableName, vars] : tableVars) {
                if (vars.empty()) continue;
                
                // Determinar rango de índices
                vector<int> indices;
                for (const auto& var : vars) {
                    size_t pos = var->pac_source.find(':');
                    if (pos != string::npos) {
                        int index = stoi(var->pac_source.substr(pos + 1));
                        indices.push_back(index);
                    }
                }
                
                if (indices.empty()) continue;
                
                int minIndex = *min_element(indices.begin(), indices.end());
                int maxIndex = *max_element(indices.begin(), indices.end());
                
                // Leer datos del PAC
                bool isAlarmTable = tableName.find("TBL_DA_") == 0 || 
                                   tableName.find("TBL_PA_") == 0 || 
                                   tableName.find("TBL_LA_") == 0 || 
                                   tableName.find("TBL_TA_") == 0;
                
                if (isAlarmTable) {
                    // Leer tabla de alarmas (int32)
                    vector<int32_t> values = pacClient->readInt32Table(tableName, minIndex, maxIndex);
                    
                    // Actualizar variables
                    for (const auto& var : vars) {
                        if (var->type != Variable::INT32) continue;
                        
                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;
                        
                        if (arrayIndex >= 0 && arrayIndex < (int)values.size()) {
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char*>(var->opcua_name.c_str()));
                            
                            UA_Variant value;
                            UA_Variant_init(&value);
                            int32_t newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);
                            
                            UA_Server_writeValue(server, nodeId, value);
                        }
                    }
                } else {
                    // Leer tabla de valores (float)
                    vector<float> values = pacClient->readFloatTable(tableName, minIndex, maxIndex);
                    
                    // Actualizar variables
                    for (const auto& var : vars) {
                        if (var->type != Variable::FLOAT) continue;
                        
                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;
                        
                        if (arrayIndex >= 0 && arrayIndex < (int)values.size()) {
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char*>(var->opcua_name.c_str()));
                            
                            UA_Variant value;
                            UA_Variant_init(&value);
                            float newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);
                            
                            UA_Server_writeValue(server, nodeId, value);
                        }
                    }
                }
                
                // Pequeña pausa entre tablas
                this_thread::sleep_for(chrono::milliseconds(50));
            }
        } else {
            // Sin conexión PAC - intentar reconectar cada 10 segundos
            static auto lastReconnect = chrono::steady_clock::now();
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::seconds>(now - lastReconnect).count() >= 10) {
                cout << "🔄 Intentando reconectar PAC..." << endl;
                pacClient.reset();
                pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
                if (pacClient->connect()) {
                    cout << "✅ Reconectado a PAC" << endl;
                } else {
                    cout << "❌ Fallo en reconexión" << endl;
                }
                lastReconnect = now;
            }
        }
        
        // Esperar intervalo de actualización
        this_thread::sleep_for(chrono::milliseconds(config.update_interval_ms));
    }
    
    cout << "🔄 Actualización de datos terminada" << endl;
}

// ============== FUNCIONES PRINCIPALES ==============

void ServerInit() {
    cout << "🚀 Inicializando servidor OPC UA..." << endl;
    
    // Cargar configuración
    if (!loadConfig()) {
        cout << "⚠️ Usando configuración por defecto" << endl;
    }
    
    // Crear servidor OPC UA
    server = UA_Server_new();
    if (!server) {
        cout << "❌ Error creando servidor OPC UA" << endl;
        return;
    }
    
    // Crear nodos
    createNodes();
    
    // Crear cliente PAC
    pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
    if (pacClient->connect()) {
        cout << "✅ Conectado al PAC: " << config.pac_ip << ":" << config.pac_port << endl;
    } else {
        cout << "⚠️ Sin conexión PAC - funcionando en modo simulación" << endl;
    }
    
    cout << "✅ Servidor inicializado correctamente" << endl;
    cout << "📊 Variables configuradas: " << config.variables.size() << endl;
}

UA_StatusCode runServer() {
    cout << "▶️ Ejecutando servidor OPC UA en puerto " << config.opcua_port << endl;
    cout << "📡 URL: opc.tcp://localhost:" << config.opcua_port << endl;
    
    // Iniciar hilo de actualización
    thread updateThread(updateData);
    
    // Ejecutar servidor
    UA_StatusCode retval = UA_Server_run(server, &server_running);
    
    // Esperar hilo de actualización
    if (updateThread.joinable()) {
        updateThread.join();
    }
    
    return retval;
}

bool getPACConnectionStatus() {
    return pacClient && pacClient->isConnected();
}

void cleanupAndExit() {
    cout << "\n🧹 Limpieza y terminación..." << endl;
    
    running = false;
    server_running = false;
    
    if (pacClient) {
        pacClient.reset();
    }
    
    if (server) {
        UA_Server_delete(server);
        server = nullptr;
    }
    
    cout << "✓ Limpieza completada" << endl;
}
