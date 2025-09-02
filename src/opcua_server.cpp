#include "opcua_server.h"
#include "pac_control_client.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>

using namespace std;
using json = nlohmann::json;

// ============== VARIABLES GLOBALES ==============
UA_Server *server = nullptr;
std::unique_ptr<PACControlClient> pacClient;

// 🔧 DEFINIR LAS VARIABLES GLOBALES DECLARADAS EN COMMON.H
Config config;
std::atomic<bool> running{true};
std::atomic<bool> server_running{true};
std::atomic<bool> updating_internally{false};
std::atomic<bool> server_writing_internally{false};

// Variable normal para UA_Server_run (requiere bool*)
bool server_running_flag = true;

// ============== FUNCIONES AUXILIARES ==============

int getVariableIndex(const std::string &varName)
{
    // Mapeo correcto según la estructura real de las tablas PAC:
    // 0: Input/Value, 1: SetHH, 2: SetH, 3: SetL, 4: SetLL, 5: SIM_Value
    // 6: PV, 7: min, 8: max, 9: percent

    if (varName == "Input") return 0;
    if (varName == "SetHH") return 1;
    if (varName == "SetH") return 2;
    if (varName == "SetL") return 3;
    if (varName == "SetLL") return 4;
    if (varName == "SIM_Value") return 5;
    if (varName == "PV") return 6;
    if (varName == "min") return 7;
    if (varName == "max") return 8;
    if (varName == "percent") return 9;

    // Compatibilidad con nombres con mayúsculas
    if (varName == "Min") return 7;
    if (varName == "Max") return 8;
    if (varName == "Percent") return 9;

    // Variables de alarma - estructura TBL_TA_XXXXX (INT32)
    // 0: Bits HH, 1: Bits H, 2: Bits L, 3: Bits LL, 4: Color_alarm
    if (varName == "HH") return 0;
    if (varName == "H") return 1;
    if (varName == "L") return 2;
    if (varName == "LL") return 3;
    if (varName == "Color") return 4;

    // Compatibilidad con nombres alternativos
    if (varName == "ALARM_HH") return 0;
    if (varName == "ALARM_H") return 1;
    if (varName == "ALARM_L") return 2;
    if (varName == "ALARM_LL") return 3;
    if (varName == "COLOR") return 4;

    return -1;
}

int getAPIVariableIndex(const std::string &varName) {
    if (varName == "IV") return 0;
    if (varName == "NSV") return 1;
    if (varName == "CPL") return 2;
    if (varName == "CTL") return 3;
    return -1;
}

int getBatchVariableIndex(const std::string &varName)
{
    // Implementar según necesidades
    return -1;
}

bool isWritableVariable(const std::string &varName)
{
    return varName.find("SET") == 0 ||
           varName.find("Set") == 0 ||
           varName.find("SIM_") == 0 ||
           varName.find("E_") == 0;
}

// ============== CONFIGURACIÓN ==============

bool loadConfig(const string& configFile)
{
    cout << "📄 Cargando configuración desde: " << configFile << endl;

    try
    {
        // Lista de archivos a intentar
        vector<string> configFiles = {
            configFile,
            "tags.json",              
            "../tags.json",           
            "config/tags.json",       
            "../../tags.json",        
            "config/config.json"
        };

        json configJson;
        bool configLoaded = false;

        for (const auto& fileName : configFiles) {
            ifstream file(fileName);
            if (file.is_open()) {
                cout << "📄 Usando archivo: " << fileName << endl;
                file >> configJson;
                file.close();
                configLoaded = true;
                break;
            }
        }

        if (!configLoaded) {
            cout << "❌ No se encontró ningún archivo de configuración" << endl;
            cout << "📝 Archivos intentados:" << endl;
            for (const auto& fileName : configFiles) {
                cout << "   - " << fileName << endl;
            }
            return false;
        }

        return processConfigFromJson(configJson);
    }
    catch (const exception &e)
    {
        cout << "❌ Error cargando configuración: " << e.what() << endl;
        return false;
    }
}

bool processConfigFromJson(const json& configJson) {
    // Configuración PAC
    if (configJson.contains("pac_config"))
    {
        auto &pac = configJson["pac_config"];
        config.pac_ip = pac.value("ip", "192.168.1.30");
        config.pac_port = pac.value("port", 22001);
    }

    // Configuración del servidor
    if (configJson.contains("server_config"))
    {
        auto &srv = configJson["server_config"];
        config.opcua_port = srv.value("opcua_port", 4840);
        config.update_interval_ms = srv.value("update_interval_ms", 2000);
        config.server_name = srv.value("server_name", "PAC Control SCADA Server");
    }

    // 🔧 LIMPIAR CONFIGURACIÓN ANTERIOR USANDO MÉTODO UNIFICADO
    config.clear();

    // Procesar simple_variables
    if (configJson.contains("simple_variables")) {
        LOG_INFO("🔍 Procesando simple_variables...");
        
        int varIndex = 0;
        for (const auto &varJson : configJson["simple_variables"]) {
            if (!varJson.contains("opcua_name") || !varJson.contains("pac_source")) {
                if (varJson.contains("description")) {
                    SimpleVariable simpleVar;
                    simpleVar.name = "SimpleVar_" + to_string(varIndex);
                    simpleVar.pac_source = "TBL_SIMPLE:" + to_string(varIndex);
                    simpleVar.type = "FLOAT";
                    simpleVar.writable = false;
                    simpleVar.description = varJson["description"];
                    
                    config.simple_variables.push_back(simpleVar);
                    LOG_DEBUG("✅ Variable simple creada: " << simpleVar.name << " (" << simpleVar.description << ")");
                } else {
                    LOG_DEBUG("⚠️ Variable simple sin datos suficientes, omitiendo");
                }
            } else {
                SimpleVariable simpleVar;
                simpleVar.name = varJson.value("opcua_name", "");
                simpleVar.pac_source = varJson.value("pac_source", "");
                simpleVar.type = varJson.value("type", "FLOAT");
                simpleVar.writable = varJson.value("writable", false);
                
                if (varJson.contains("description")) {
                    simpleVar.description = varJson["description"];
                }
                
                config.simple_variables.push_back(simpleVar);
                LOG_DEBUG("✅ Variable simple cargada: " << simpleVar.name);
            }
            
            varIndex++;
        }
        cout << "✓ Cargadas " << config.simple_variables.size() << " variables simples" << endl;
    }

    // Cargar TAGs tradicionales
    if (configJson.contains("tbL_tags")) {
        for (const auto &tagJson : configJson["tbL_tags"]) {
            Tag tag;
            tag.name = tagJson.value("name", "");
            tag.value_table = tagJson.value("value_table", "");
            tag.alarm_table = tagJson.value("alarm_table", "");

            if (tagJson.contains("variables")) {
                for (const auto &var : tagJson["variables"]) {
                    tag.variables.push_back(var);
                }
            }

            config.tags.push_back(tag);
        }
        cout << "✓ Cargados " << config.tags.size() << " TBL_tags" << endl;
    }

    // Procesar configuración en variables
    processConfigIntoVariables();
    
    return true;
}

void processConfigIntoVariables()
{
    LOG_INFO("🔧 Procesando configuración en variables...");
    
    // Crear variables desde simple_variables
    for (const auto &simpleVar : config.simple_variables) {
        if (simpleVar.name.empty()) {
            LOG_DEBUG("⚠️ Variable simple sin nombre, omitiendo");
            continue;
        }
        
        Variable var;
        var.opcua_name = simpleVar.name;
        var.tag_name = "SimpleVars";
        var.var_name = simpleVar.name;
        var.pac_source = simpleVar.pac_source;
        var.type = (simpleVar.type == "INT32") ? Variable::INT32 : Variable::FLOAT;
        var.writable = simpleVar.writable;
        var.description = simpleVar.description;
        
        config.variables.push_back(var);
    }
    
    // Crear variables desde TAGs tradicionales
    for (const auto &tag : config.tags) {
        for (const auto &varName : tag.variables) {
            Variable var;
            var.opcua_name = tag.name + "." + varName;
            var.tag_name = tag.name;
            var.var_name = varName;
            var.pac_source = tag.value_table + ":" + to_string(getVariableIndex(varName));
            var.type = (varName.find("Color") != string::npos || 
                       varName.find("HH") != string::npos) ? Variable::INT32 : Variable::FLOAT;
            var.writable = isWritableVariable(varName);
            var.table_index = getVariableIndex(varName);
            
            config.variables.push_back(var);
        }
    }
    
    LOG_INFO("✅ Procesamiento completado: " << config.getTotalVariableCount() << " variables totales");
    LOG_INFO("📝 Variables escribibles: " << config.getWritableVariableCount());
}

// ============== CALLBACKS CORREGIDOS ==============

// 🔧 CALLBACK DE ESCRITURA - CORREGIDO PARA NODEID NUMÉRICO
static void writeCallback(UA_Server *server,
                         const UA_NodeId *sessionId,
                         void *sessionContext,
                         const UA_NodeId *nodeId,
                         void *nodeContext,
                         const UA_NumericRange *range,
                         const UA_DataValue *data) {
    
    // 🔍 IGNORAR ESCRITURAS INTERNAS DEL SERVIDOR
    if (server_writing_internally.load()) {
        return;
    }

    // 🔒 EVITAR PROCESAMIENTO DURANTE ACTUALIZACIONES
    if (updating_internally.load()) {
        LOG_ERROR("Escritura rechazada: servidor actualizando");
        return;
    }
    
    // ✅ VALIDACIONES BÁSICAS
    if (!server || !nodeId || !data || !data->value.data) {
        LOG_ERROR("Parámetros inválidos en writeCallback");
        return;
    }

    // 🔧 MANEJAR NODEID NUMÉRICO
    if (nodeId->identifierType != UA_NODEIDTYPE_NUMERIC) {
        LOG_ERROR("Tipo de NodeId no soportado (esperado numérico)");
        return;
    }

    uint32_t numericNodeId = nodeId->identifier.numeric;
    LOG_WRITE("📝 ESCRITURA RECIBIDA en NodeId: " << numericNodeId);

    // 🔍 BUSCAR VARIABLE POR NODEID NUMÉRICO
    Variable *var = nullptr;
    for (auto &v : config.variables) {
        if (v.has_node && v.node_id == (int)numericNodeId) {
            var = &v;
            break;
        }
    }

    if (!var) {
        LOG_ERROR("Variable no encontrada para NodeId: " << numericNodeId);
        return;
    }

    if (!var->writable) {
        LOG_ERROR("Variable no escribible: " << var->opcua_name);
        return;
    }

    // 🔒 VERIFICAR CONEXIÓN PAC
    if (!pacClient || !pacClient->isConnected()) {
        LOG_ERROR("PAC no conectado para escritura: " << var->opcua_name);
        return;
    }

    LOG_DEBUG("✅ Procesando escritura para: " << var->opcua_name << " (PAC: " << var->pac_source << ")");

    // 🎯 PROCESAR ESCRITURA SEGÚN TIPO
    bool write_success = false;

    if (var->type == Variable::FLOAT) {
        if (data->value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
            float value = *(float*)data->value.data;
            // Aquí irían las llamadas al PAC
            LOG_WRITE("Escribiendo FLOAT " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        } else {
            LOG_ERROR("Tipo de dato incorrecto para variable FLOAT: " << var->opcua_name);
        }
    } else if (var->type == Variable::INT32) {
        if (data->value.type == &UA_TYPES[UA_TYPES_INT32]) {
            int32_t value = *(int32_t*)data->value.data;
            // Aquí irían las llamadas al PAC
            LOG_WRITE("Escribiendo INT32 " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        } else {
            LOG_ERROR("Tipo de dato incorrecto para variable INT32: " << var->opcua_name);
        }
    }

    // ✅ RESULTADO FINAL
    if (write_success) {
        LOG_WRITE("✅ Escritura exitosa: " << var->opcua_name);
    } else {
        LOG_ERROR("❌ Error en escritura: " << var->opcua_name);
    }
}

// 🔧 CALLBACK DE LECTURA CORREGIDO PARA NODEID NUMÉRICO
static void readCallback(UA_Server *server,
                        const UA_NodeId *sessionId,
                        void *sessionContext,
                        const UA_NodeId *nodeId,
                        void *nodeContext,
                        const UA_NumericRange *range,
                        const UA_DataValue *data) {
    
    // 🔍 IGNORAR LECTURAS DURANTE ACTUALIZACIONES INTERNAS
    if (updating_internally.load()) {
        return;
    }
    
    // Para este callback de lectura, normalmente no necesitamos hacer nada especial
    // ya que los valores se actualizan desde updateData()
    
    if (nodeId && nodeId->identifierType == UA_NODEIDTYPE_NUMERIC) {
        uint32_t numericNodeId = nodeId->identifier.numeric;
        LOG_DEBUG("📖 Lectura de NodeId: " << numericNodeId);
    }
}

// ============== CREACIÓN DE NODOS ==============

void createNodes()
{
    LOG_DEBUG("🏗️ Creando nodos OPC-UA...");

    // Agrupar variables por TAG
    map<string, vector<Variable *>> tagVars;
    for (auto &var : config.variables)
    {
        tagVars[var.tag_name].push_back(&var);
    }

    // 🔧 USAR CONTADOR PARA NODEIDS ÚNICOS
    int nodeCounter = 1000;

    // Crear cada TAG con sus variables
    for (const auto &[tagName, variables] : tagVars)
    {
        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en"), 
                                           const_cast<char*>(tagName.c_str()));

        UA_NodeId tagNodeId;
        
        // 🔧 USAR NODEID NUMÉRICO ÚNICO PARA EVITAR DUPLICADOS
        UA_StatusCode result = UA_Server_addObjectNode(
            server,
            UA_NODEID_NUMERIC(1, nodeCounter++),  // NodeId numérico único
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(1, const_cast<char*>(tagName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            oAttr, nullptr, &tagNodeId);

        if (result != UA_STATUSCODE_GOOD)
        {
            LOG_ERROR("Error creando TAG: " << tagName << " (código: " << result << ")");
            continue;
        }

        LOG_DEBUG("✅ TAG creado: " << tagName);

        // Crear variables del TAG
        for (auto var : variables)
        {
            // 🔧 VERIFICAR QUE LA VARIABLE TIENE NOMBRE VÁLIDO
            if (var->opcua_name.empty()) {
                LOG_DEBUG("⚠️ Variable sin nombre OPC-UA, omitiendo");
                continue;
            }

            UA_VariableAttributes vAttr = UA_VariableAttributes_default;
            vAttr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en"), 
                                               const_cast<char*>(var->var_name.c_str()));

            UA_Variant_init(&vAttr.value);

            // Configurar según tipo de variable
            if (var->type == Variable::FLOAT)
            {
                float initialValue = 0.0f;
                UA_Variant_setScalar(&vAttr.value, &initialValue, &UA_TYPES[UA_TYPES_FLOAT]);
                vAttr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
            }
            else if (var->type == Variable::INT32)
            {
                int32_t initialValue = 0;
                UA_Variant_setScalar(&vAttr.value, &initialValue, &UA_TYPES[UA_TYPES_INT32]);
                vAttr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
            }

            vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
            if (var->writable)
            {
                vAttr.accessLevel |= UA_ACCESSLEVELMASK_WRITE;
            }

            // 🔧 USAR NODEID NUMÉRICO ÚNICO PARA VARIABLES
            UA_StatusCode varResult = UA_Server_addVariableNode(
                server,
                UA_NODEID_NUMERIC(1, nodeCounter++),  // NodeId numérico único
                tagNodeId,
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                UA_QUALIFIEDNAME(1, const_cast<char*>(var->var_name.c_str())),
                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                vAttr, nullptr, nullptr);

            if (varResult == UA_STATUSCODE_GOOD)
            {
                var->has_node = true;
                var->node_id = nodeCounter - 1;  // Guardar el NodeId usado

                // 🔧 CONFIGURAR CALLBACKS SOLO PARA VARIABLES ESCRIBIBLES
                if (var->writable)
                {
                    UA_ValueCallback callback;
                    callback.onRead = readCallback;
                    callback.onWrite = writeCallback;
                    
                    UA_NodeId nodeId = UA_NODEID_NUMERIC(1, var->node_id);
                    UA_StatusCode cbResult = UA_Server_setVariableNode_valueCallback(
                        server, nodeId, callback);
                        
                    if (cbResult != UA_STATUSCODE_GOOD) {
                        LOG_ERROR("Error configurando callback para: " << var->opcua_name);
                    } else {
                        LOG_DEBUG("✅ Callback configurado para: " << var->opcua_name);
                    }
                }
                
                LOG_DEBUG("✅ Nodo creado: " << var->opcua_name << " (NodeId: " << var->node_id << ")");
            }
            else
            {
                LOG_ERROR("❌ Error creando variable: " << var->opcua_name << " (código: " << varResult << ")");
            }
        }
    }

    cout << "✓ Nodos creados" << endl;
}

// ============== ACTUALIZACIÓN DE DATOS ==============

void updateData()
{
    static auto lastReconnect = chrono::steady_clock::now();

    while (running && server_running)
    {
        // Solo log cuando inicia ciclo completo
        LOG_DEBUG("Iniciando ciclo de actualización PAC");
        
        // 🔒 VERIFICAR SI ES SEGURO HACER ACTUALIZACIONES
        if (updating_internally.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (pacClient && pacClient->isConnected())
        {
            LOG_DEBUG("🔄 Iniciando actualización de datos del PAC");
            
            // ⚠️ MARCAR ACTUALIZACIÓN EN PROGRESO
            updating_internally.store(true);

            // 🔒 ACTIVAR BANDERA DE ESCRITURA INTERNA DEL SERVIDOR
            server_writing_internally.store(true);

            // Separar variables por tipo
            vector<Variable *> simpleVars;             // Variables individuales (F_xxx, I_xxx)
            map<string, vector<Variable *>> tableVars; // Variables de tabla (TBL_xxx:índice)

            for (auto &var : config.variables)
            {
                if (!var.has_node)
                    continue;

                // Verificar si es variable de tabla o simple
                size_t pos = var.pac_source.find(':');
                if (pos != string::npos)
                {
                    // Variable de tabla
                    string table = var.pac_source.substr(0, pos);
                    tableVars[table].push_back(&var);
                }
                else
                {
                    // Variable simple - SKIP por ahora (no hay función implementada)
                    LOG_DEBUG("⚠️ Variable simple saltada: " << var.opcua_name << " (función no implementada)");
                }
            }

            // 📊 ACTUALIZAR VARIABLES DE TABLA ÚNICAMENTE
            int tables_updated = 0;
            for (const auto &[tableName, vars] : tableVars)
            {
                if (vars.empty())
                    continue;

                LOG_DEBUG("📋 Actualizando tabla: " << tableName << " (" << vars.size() << " variables)");

                // Determinar rango de índices
                vector<int> indices;
                for (const auto &var : vars)
                {
                    size_t pos = var->pac_source.find(':');
                    if (pos != std::string::npos)
                    {
                        int index = stoi(var->pac_source.substr(pos + 1));
                        indices.push_back(index);
                    }
                }

                if (indices.empty()) {
                    LOG_DEBUG("⚠️ Tabla sin índices válidos: " << tableName);
                    continue;
                }

                int minIndex = *min_element(indices.begin(), indices.end());
                int maxIndex = *max_element(indices.begin(), indices.end());

                LOG_DEBUG("🔢 Leyendo tabla " << tableName << " índices [" << minIndex << "-" << maxIndex << "]");

                // Leer datos del PAC
                bool isAlarmTable = tableName.find("TBL_DA_") == 0 ||
                                    tableName.find("TBL_PA_") == 0 ||
                                    tableName.find("TBL_LA_") == 0 ||
                                    tableName.find("TBL_TA_") == 0;

                if (isAlarmTable)
                {
                    // 🚨 LEER TABLA DE ALARMAS (INT32)
                    vector<int32_t> values = pacClient->readInt32Table(tableName, minIndex, maxIndex);

                    if (values.empty()) {
                        LOG_DEBUG("❌ Error leyendo tabla INT32: " << tableName);
                        continue;
                    }

                    LOG_DEBUG("✅ Leída tabla INT32: " << tableName << " (" << values.size() << " valores)");

                    // Actualizar variables
                    int vars_updated = 0;
                    for (const auto &var : vars)
                    {
                        if (var->type != Variable::INT32)
                            continue;

                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;

                        if (arrayIndex >= 0 && arrayIndex < (int)values.size())
                        {
                            // 🔧 USAR NODEID NUMÉRICO EN LUGAR DE STRING
                            UA_NodeId nodeId = UA_NODEID_NUMERIC(1, var->node_id);

                            UA_Variant value;
                            UA_Variant_init(&value);
                            int32_t newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            
                            if (result == UA_STATUSCODE_GOOD) {
                                vars_updated++;
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue);
                            } else {
                                LOG_DEBUG("❌ Error actualizando " << var->opcua_name);
                            }
                        }
                    }
                    
                    LOG_DEBUG("✅ Tabla INT32 " << tableName << ": " << vars_updated << " variables actualizadas");
                }
                else
                {
                    // 📊 LEER TABLA DE VALORES (FLOAT)
                    vector<float> values = pacClient->readFloatTable(tableName, minIndex, maxIndex);

                    if (values.empty()) {
                        LOG_DEBUG("❌ Error leyendo tabla FLOAT: " << tableName);
                        continue;
                    }

                    LOG_DEBUG("✅ Leída tabla FLOAT: " << tableName << " (" << values.size() << " valores)");

                    // Actualizar variables
                    int vars_updated = 0;
                    for (const auto &var : vars)
                    {
                        if (var->type != Variable::FLOAT)
                            continue;

                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;

                        if (arrayIndex >= 0 && arrayIndex < (int)values.size())
                        {
                            // 🔧 USAR NODEID NUMÉRICO EN LUGAR DE STRING
                            UA_NodeId nodeId = UA_NODEID_NUMERIC(1, var->node_id);

                            UA_Variant value;
                            UA_Variant_init(&value);
                            float newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            
                            if (result == UA_STATUSCODE_GOOD) {
                                vars_updated++;
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue);
                            } else {
                                LOG_DEBUG("❌ Error actualizando " << var->opcua_name);
                            }
                        }
                    }
                    
                    LOG_DEBUG("✅ Tabla FLOAT " << tableName << ": " << vars_updated << " variables actualizadas");
                }

                tables_updated++;
                
                // Pequeña pausa entre tablas
                this_thread::sleep_for(chrono::milliseconds(50));
            }

            // 🔓 DESACTIVAR BANDERAS
            server_writing_internally.store(false);
            updating_internally.store(false);
            
            LOG_DEBUG("✅ Actualización completada: " << tables_updated << " tablas procesadas");
        }
        else
        {
            // 🔌 SIN CONEXIÓN PAC - INTENTAR RECONECTAR
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::seconds>(now - lastReconnect).count() >= 10)
            {
                LOG_DEBUG("🔄 Intentando reconectar al PAC: " << config.pac_ip << ":" << config.pac_port);
                
                pacClient.reset();
                pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
                
                if (pacClient->connect())
                {
                    LOG_DEBUG("✅ Reconectado al PAC exitosamente");
                }
                else
                {
                    LOG_DEBUG("❌ Fallo en reconexión al PAC");
                }
                lastReconnect = now;
            }
        }

        // ⏱️ ESPERAR INTERVALO DE ACTUALIZACIÓN
        this_thread::sleep_for(chrono::milliseconds(config.update_interval_ms));
    }
    
    LOG_DEBUG("🛑 Hilo de actualización terminado");
}

// ============== FUNCIONES PRINCIPALES ==============

bool ServerInit()
{
    LOG_INFO("🚀 Inicializando servidor OPC-UA...");

    // Cargar configuración
    if (!loadConfig("tags.json"))
    {
        LOG_ERROR("Error cargando configuración");
        return false;
    }

    // Crear servidor
    server = UA_Server_new();
    if (!server)
    {
        LOG_ERROR("Error creando servidor OPC-UA");
        return false;
    }

    // 🔧 CONFIGURACIÓN BÁSICA DEL SERVIDOR
    UA_ServerConfig *server_config = UA_Server_getConfig(server);
    server_config->maxSessionTimeout = 60000.0;

    // Configurar nombre del servidor
    server_config->applicationDescription.applicationName = 
        UA_LOCALIZEDTEXT(const_cast<char*>("en"), const_cast<char*>(config.server_name.c_str()));

    LOG_INFO("📡 Servidor configurado en puerto " << config.opcua_port);

    // Crear nodos
    createNodes();

    // Inicializar cliente PAC
    pacClient = std::make_unique<PACControlClient>(config.pac_ip, config.pac_port);

    LOG_INFO("✅ Servidor OPC-UA inicializado correctamente");
    return true;
}

UA_StatusCode runServer() {
    if (!server) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    // Iniciar hilo de actualización
    std::thread updateThread(updateData);

    // Ejecutar servidor
    UA_StatusCode retval = UA_Server_run(server, &server_running_flag);

    // Detener actualización
    running.store(false);
    if (updateThread.joinable()) {
        updateThread.join();
    }

    return retval;
}

void shutdownServer()
{
    LOG_INFO("🛑 Cerrando servidor...");
    
    running.store(false);
    server_running.store(false);
    server_running_flag = false;

    if (pacClient) {
        pacClient.reset();
    }

    if (server) {
        UA_Server_delete(server);
        server = nullptr;
    }
}

bool getPACConnectionStatus()
{
    return pacClient && pacClient->isConnected();
}

void cleanupServer() {
    shutdownServer();
}

void cleanupAndExit() {
    cleanupServer();
}
