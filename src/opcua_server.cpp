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
bool updating_internally = true; // Bandera para evitar callback durante actualizaciones internas
std::unique_ptr<PACControlClient> pacClient;
Config config;
std::mutex update_mutex;

// ============== FUNCIONES AUXILIARES ==============

int getVariableIndex(const std::string &varName)
{
    // Mapeo correcto según la estructura real de las tablas PAC:
    // 0: Input/Value, 1: SetHH, 2: SetH, 3: SetL, 4: SetLL, 5: SIM_Value
    // 6: PV, 7: min, 8: max, 9: percent

    if (varName == "Input")
        return 0;
    if (varName == "SetHH")
        return 1;
    if (varName == "SetH")
        return 2;
    if (varName == "SetL")
        return 3;
    if (varName == "SetLL")
        return 4;
    if (varName == "SIM_Value")
        return 5;
    if (varName == "PV")
        return 6;
    if (varName == "min")
        return 7; // ¡CORREGIDO! minúscula
    if (varName == "max")
        return 8; // ¡CORREGIDO! minúscula
    if (varName == "percent")
        return 9; // ¡CORREGIDO! minúscula

    // Compatibilidad con nombres con mayúsculas
    if (varName == "Min")
        return 7;
    if (varName == "Max")
        return 8;
    if (varName == "Percent")
        return 9;

    // Variables de alarma - estructura TBL_TA_XXXXX (INT32)
    // 0: Bits HH, 1: Bits H, 2: Bits L, 3: Bits LL, 4: Color_alarm
    if (varName == "HH")
        return 0; // Bits de alarma HH
    if (varName == "H")
        return 1; // Bits de alarma H
    if (varName == "L")
        return 2; // Bits de alarma L
    if (varName == "LL")
        return 3; // Bits de alarma LL
    if (varName == "Color")
        return 4; // Color de alarma

    // Compatibilidad con nombres alternativos
    if (varName == "ALARM_HH")
        return 0;
    if (varName == "ALARM_H")
        return 1;
    if (varName == "ALARM_L")
        return 2;
    if (varName == "ALARM_LL")
        return 3;
    if (varName == "COLOR")
        return 4;

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

bool loadConfig()
{
    cout << "📄 Cargando configuración..." << endl;

    try
    {
        ifstream configFile("tags.json");
        if (!configFile.is_open())
        {
            cout << "❌ No se pudo abrir tags.json" << endl;
            return false;
        }

        json configJson;
        configFile >> configJson;

        // Configuración del PAC
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

        // Limpiar configuración anterior
        config.tags.clear();
        config.variables.clear();

        // Cargar TAGs
        if (configJson.contains("tbL_tags"))
        {
            for (const auto &tagJson : configJson["tbL_tags"])
            {
                Tag tag;
                tag.name = tagJson.value("name", "");
                tag.value_table = tagJson.value("value_table", "");
                tag.alarm_table = tagJson.value("alarm_table", "");

                if (tagJson.contains("variables"))
                {
                    for (const auto &var : tagJson["variables"])
                    {
                        tag.variables.push_back(var);
                    }
                }

                if (tagJson.contains("alarms"))
                {
                    for (const auto &alarm : tagJson["alarms"])
                    {
                        tag.alarms.push_back(alarm);
                    }
                }

                config.tags.push_back(tag);
            }
        }

        // Crear variables desde TAGs
        for (const auto &tag : config.tags)
        {
            // Variables de valor (float)
            for (const auto &varName : tag.variables)
            {
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
            for (const auto &alarmName : tag.alarms)
            {
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

        // Cargar variables simples individuales
        if (configJson.contains("simple_variables"))
        {
            for (const auto &varJson : configJson["simple_variables"])
            {
                Variable var;
                var.opcua_name = varJson.value("opcua_name", "");
                var.pac_source = varJson.value("pac_source", "");
                var.writable = varJson.value("writable", false);

                // Determinar tipo
                string typeStr = varJson.value("type", "FLOAT");
                if (typeStr == "INT32")
                {
                    var.type = Variable::INT32;
                }
                else
                {
                    var.type = Variable::FLOAT;
                }

                // Para variables simples, el tag_name y var_name son el opcua_name
                var.tag_name = "SimpleVariables";
                var.var_name = var.opcua_name;

                config.variables.push_back(var);
            }
        }

        cout << "✓ Configuración cargada: " << config.tags.size() << " tags, "
             << config.variables.size() << " variables" << endl;
        return true;
    }
    catch (const exception &e)
    {
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
    
    string nodeIdStr = string((char*)nodeId->identifier.string.data, 
                             nodeId->identifier.string.length);
    
    // 🔍 VERIFICAR SI LA ESCRITURA ESTÁ PRE-REGISTRADA
    bool is_registered = WriteRegistrationManager::isWriteRegistered(nodeIdStr);
    bool is_critical = WriteRegistrationManager::isCriticalWrite(nodeIdStr);
    
    // 🔍 AUTO-DETECCIÓN DE ESCRITURAS CRÍTICAS
    if (!is_registered && WriteRegistrationManager::isVariableCritical(nodeIdStr)) {
        WriteRegistrationManager::registerCriticalWrite(nodeIdStr, "Auto-detectado crítico");
        is_registered = true;
        is_critical = true;
        DEBUG_INFO("🔴 AUTO-REGISTRO CRÍTICO: " << nodeIdStr);
    }
    
    // Si NO está registrada, verificar si es actualización interna
    if (!is_registered) {
        // Verificaciones adicionales para detección automática
        bool hasValidSession = (sessionId != nullptr && 
                               sessionId->identifierType == UA_NODEIDTYPE_NUMERIC && 
                               sessionId->identifier.numeric > 0);
        
        if (!hasValidSession) {
            DEBUG_INFO("⚠️ Escritura NO registrada y sin sesión válida - IGNORADA: " << nodeIdStr);
            return;
        }
        
        // Auto-registrar escrituras no críticas con sesión válida
        WriteRegistrationManager::registerWrite(nodeIdStr, "Auto-detectado normal");
        DEBUG_INFO("🟡 Auto-registro de escritura: " << nodeIdStr);
    }
    
    // 🔴 PROCESAR ESCRITURA REGISTRADA
    string write_info = WriteRegistrationManager::getWriteInfo(nodeIdStr);
    DEBUG_INFO("✍️ PROCESANDO ESCRITURA: " << nodeIdStr 
              << " (Crítica: " << (is_critical ? "SÍ" : "NO") 
              << ", Info: " << write_info << ")");
    
    // 🔒 BLOQUEAR ACTUALIZACIONES DURANTE ESCRITURA CRÍTICA
    if (is_critical) {
        updating_internally = true;  // Bloqueo total para escrituras críticas
        DEBUG_INFO("🔴 MODO CRÍTICO ACTIVADO - Actualizaciones bloqueadas");
    }
    

    if (updating_internally)
    {
        // DEBUG_INFO("❌ WRITE REQUEST ignorado durante actualización interna");
        return; // Ignorar durante actualizaciones internas
    }

    // string sessionIdStr = string((char *)sessionId->identifier.string.data,
    //                              sessionId->identifier.string.length);

    DEBUG_INFO("✍️ ESCRITURA EXTERNA de cliente OPC-UA para: " << sessionId );

    // DEBUG_INFO("✍️ WRITE REQUEST para: " << nodeIdStr);

    if (nodeId->identifierType != UA_NODEIDTYPE_STRING)
    {
        // DEBUG_INFO("❌ WRITE BLOQUEADO: nodeId no es string");
        //updating_internally = false; // 🔓 Restaurar actualizaciones
        return;
    }

    if (!pacClient || !pacClient->isConnected())
    {
        DEBUG_INFO("❌ Sin conexión PAC");
        //updating_internally = false; // 🔓 Restaurar actualizaciones
        return;
    }

    // Buscar variable
    Variable *var = nullptr;
    for (auto &v : config.variables)
    {
        if (v.opcua_name == nodeIdStr)
        {
            var = &v;
            break;
        }
    }

    if (!var || !var->writable)
    {
        DEBUG_INFO("❌ Variable no escribible: " << nodeIdStr << " (encontrada: " << (var ? "sí" : "no") << ", escribible: " << (var ? (var->writable ? "sí" : "no") : "N/A") << ")");
        DEBUG_INFO("❌ Variable no escribible: " << nodeIdStr << endl);
        //updating_internally = false; // 🔓 Restaurar actualizaciones
        return;
    }

    // Escribir al PAC
    bool success = false;
    if (var->type == Variable::FLOAT && data->value.type == &UA_TYPES[UA_TYPES_FLOAT])
    {
        float value = *(UA_Float *)data->value.data;
        DEBUG_INFO("🔧 Escribiendo FLOAT: " << value << " a variable: " << var->opcua_name << " (PAC source: " << var->pac_source << ")");

        // Verificar si es variable de tabla o simple
        size_t pos = var->pac_source.find(':');
        if (pos != string::npos)
        {
            // Variable de tabla con índice específico
            string table = var->pac_source.substr(0, pos);
            int index = stoi(var->pac_source.substr(pos + 1));
            DEBUG_INFO("📝 Escribiendo a tabla: " << table << " índice: " << index << " valor: " << value);
            success = pacClient->writeFloatTableIndex(table, index, value);
        }
        else
        {
            // Variable simple individual
            DEBUG_INFO("📝 Escribiendo variable simple: " << var->pac_source << " valor: " << value);
            success = pacClient->writeSingleFloatVariable(var->pac_source, value);
        }
        DEBUG_INFO("✅ Resultado escritura FLOAT: " << (success ? "ÉXITO" : "FALLO"));
    }
    else if (var->type == Variable::INT32 && data->value.type == &UA_TYPES[UA_TYPES_INT32])
    {
        int32_t value = *(UA_Int32 *)data->value.data;
        DEBUG_INFO("🔧 Escribiendo INT32: " << value << " a variable: " << var->opcua_name << " (PAC source: " << var->pac_source << ")");

        // Verificar si es variable de tabla o simple
        size_t pos = var->pac_source.find(':');
        if (pos != string::npos)
        {
            // Variable de tabla con índice específico
            string table = var->pac_source.substr(0, pos);
            int index = stoi(var->pac_source.substr(pos + 1));
            DEBUG_INFO("📝 Escribiendo a tabla INT32: " << table << " índice: " << index << " valor: " << value);
            success = pacClient->writeInt32TableIndex(table, index, value);
        }
        else
        {
            // Variable simple individual
            DEBUG_INFO("📝 Escribiendo variable simple INT32: " << var->pac_source << " valor: " << value);
            success = pacClient->writeSingleInt32Variable(var->pac_source, value);
        }
        DEBUG_INFO("✅ Resultado escritura INT32: " << (success ? "ÉXITO" : "FALLO"));
    }
    else
    {
        DEBUG_INFO("❌ Tipo de datos incompatible - Variable tipo: " << (var->type == Variable::FLOAT ? "FLOAT" : "INT32") << ", Data tipo: " << data->value.type->typeName);
    }

    // ✅ CONSUMIR LA ESCRITURA DESPUÉS DE PROCESARLA
    WriteRegistrationManager::consumeWrite(nodeIdStr);
    
    // 🔓 RESTAURAR ACTUALIZACIONES DESPUÉS DE ESCRITURA CRÍTICA
    if (is_critical) {
        // Pequeña pausa para asegurar que la escritura se complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        updating_internally = false;
        DEBUG_INFO("🔴 MODO CRÍTICO DESACTIVADO - Actualizaciones restauradas");
    }
}

// ============== CREACIÓN DE NODOS ==============

void createNodes()
{
    DEBUG_INFO("🏗️ Creando nodos OPC-UA..." << endl);

    // Agrupar variables por TAG
    map<string, vector<Variable *>> tagVars;
    for (auto &var : config.variables)
    {
        tagVars[var.tag_name].push_back(&var);
    }

    // Crear cada TAG con sus variables
    for (const auto &[tagName, variables] : tagVars)
    {
        // Crear nodo TAG
        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char *>(tagName.c_str()));

        UA_NodeId tagNodeId;
        UA_StatusCode result = UA_Server_addObjectNode(
            server,
            UA_NODEID_STRING(1, const_cast<char *>(tagName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(1, const_cast<char *>(tagName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            oAttr, nullptr, &tagNodeId);

        if (result != UA_STATUSCODE_GOOD)
        {
            DEBUG_INFO("❌ Error creando TAG: " << tagName);
            continue;
        }

        // Crear variables del TAG
        for (auto var : variables)
        {
            UA_VariableAttributes vAttr = UA_VariableAttributes_default;
            vAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char *>(var->var_name.c_str()));

            // Configurar tipo de dato y valor inicial
            UA_Variant value;
            UA_Variant_init(&value);

            if (var->type == Variable::FLOAT)
            {
                float initial = 0.0f;
                UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_FLOAT]);
            }
            else if (var->type == Variable::INT32)
            {
                int32_t initial = 0;
                UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_INT32]);
            }

            vAttr.value = value;
            vAttr.accessLevel = var->writable ? (UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE) : UA_ACCESSLEVELMASK_READ;

            UA_NodeId varNodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

            result = UA_Server_addVariableNode(
                server,
                varNodeId,
                tagNodeId,
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                UA_QUALIFIEDNAME(1, const_cast<char *>(var->var_name.c_str())),
                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                vAttr, nullptr, nullptr);

            if (result == UA_STATUSCODE_GOOD)
            {
                var->has_node = true;

                // Agregar callback de escritura si es necesario
                if (var->writable)
                {
                    UA_ValueCallback callback;
                    callback.onRead = nullptr;
                    callback.onWrite = writeCallback;
                    UA_Server_setVariableNode_valueCallback(server, varNodeId, callback);
                }

                cout << "  ✓ " << var->opcua_name << (var->writable ? " (R/W)" : " (R)") << endl;
            }
            else
            {
                cout << "  ❌ Error creando variable: " << var->opcua_name << endl;
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
        if (pacClient && pacClient->isConnected())
        {
            // ⚠️ Solo actualizar si NO hay escritura en progreso
            updating_internally = true;

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
                    // Variable simple
                    simpleVars.push_back(&var);
                }
            }

            // 1. Actualizar variables simples individuales
            for (auto &var : simpleVars)
            {
                if (var->type == Variable::FLOAT)
                {
                    // Leer variable float simple
                    float value = pacClient->readSingleFloatVariableByTag(var->pac_source);
                    if (!isnan(value))
                    {
                        UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                        UA_Variant uaValue;
                        UA_Variant_init(&uaValue);
                        UA_Variant_setScalar(&uaValue, &value, &UA_TYPES[UA_TYPES_FLOAT]);

                        UA_Server_writeValue(server, nodeId, uaValue);
                    }
                }
                else if (var->type == Variable::INT32)
                {
                    // Leer variable int32 simple
                    int32_t value = pacClient->readSingleInt32VariableByTag(var->pac_source);
                    UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                    UA_Variant uaValue;
                    UA_Variant_init(&uaValue);
                    UA_Variant_setScalar(&uaValue, &value, &UA_TYPES[UA_TYPES_INT32]);

                    UA_Server_writeValue(server, nodeId, uaValue);
                }

                // Pequeña pausa entre variables simples
                this_thread::sleep_for(chrono::milliseconds(10));
            }

            // 2. Actualizar variables de tabla (código existente)
            for (const auto &[tableName, vars] : tableVars)
            {
                if (vars.empty())
                    continue;

                // Determinar rango de índices
                vector<int> indices;
                for (const auto &var : vars)
                {
                    size_t pos = var->pac_source.find(':');
                    if (pos != string::npos)
                    {
                        int index = stoi(var->pac_source.substr(pos + 1));
                        indices.push_back(index);
                    }
                }

                if (indices.empty())
                    continue;

                int minIndex = *min_element(indices.begin(), indices.end());
                int maxIndex = *max_element(indices.begin(), indices.end());

                // Leer datos del PAC
                bool isAlarmTable = tableName.find("TBL_DA_") == 0 ||
                                    tableName.find("TBL_PA_") == 0 ||
                                    tableName.find("TBL_LA_") == 0 ||
                                    tableName.find("TBL_TA_") == 0;

                if (isAlarmTable)
                {
                    // Leer tabla de alarmas (int32)
                    vector<int32_t> values = pacClient->readInt32Table(tableName, minIndex, maxIndex);

                    // Actualizar variables
                    for (const auto &var : vars)
                    {
                        if (var->type != Variable::INT32)
                            continue;

                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;

                        if (arrayIndex >= 0 && arrayIndex < (int)values.size())
                        {
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            int32_t newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);

                            UA_Server_writeValue(server, nodeId, value);
                        }
                    }
                }
                else
                {
                    // Leer tabla de valores (float)
                    vector<float> values = pacClient->readFloatTable(tableName, minIndex, maxIndex);

                    // // Debug específico para TBL_TT_11006
                    // if (tableName == "TBL_TT_11006") {
                    //     DEBUG_INFO("🔍 DETALLE TBL_TT_11006 - minIndex=" << minIndex << ", maxIndex=" << maxIndex);
                    //     DEBUG_INFO("📋 Valores leídos (" << values.size() << " elementos):");
                    //     for (size_t i = 0; i < values.size(); i++) {
                    //         DEBUG_INFO("  [" << (minIndex + i) << "]: " << values[i]);
                    //     }
                    // }

                    // Actualizar variables
                    for (const auto &var : vars)
                    {
                        if (var->type != Variable::FLOAT)
                            continue;

                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;

                        if (arrayIndex >= 0 && arrayIndex < (int)values.size())
                        {
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            float newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);

                            // // Debug específico para TT_11006
                            // if (tableName == "TBL_TT_11006") {
                            //     DEBUG_INFO("🎯 " << var->opcua_name << " = " << newValue << " (índice PAC: " << index << ", arrayIndex: " << arrayIndex << ")");
                            // }

                            UA_Server_writeValue(server, nodeId, value);
                        }
                    }
                }

                // Pequeña pausa entre tablas
                this_thread::sleep_for(chrono::milliseconds(50));
            }

            // Desactivar bandera de actualización interna
            updating_internally = false;
        }
        else if (updating_internally)
        {
            // 🔄 Escritura en progreso - esperar un poco antes de reintentar
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        else
        {
            // Sin conexión PAC - intentar reconectar cada 10 segundos
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::seconds>(now - lastReconnect).count() >= 10)
            {
                pacClient.reset();
                pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
                if (pacClient->connect())
                {
                    // Reconectado
                }
                else
                {
                    // Fallo en reconexión
                }
                lastReconnect = now;
            }
        }

        // Esperar intervalo - más corto si hay escritura en progreso
        if (updating_internally)
        {
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        else
        {
            this_thread::sleep_for(chrono::milliseconds(config.update_interval_ms));
        }
    }
}

// ============== FUNCIONES PRINCIPALES ==============

void ServerInit()
{
    cout << "🚀 Inicializando servidor OPC UA..." << endl;

    // Cargar configuración
    if (!loadConfig())
    {
        cout << "⚠️ Usando configuración por defecto" << endl;
    }

    cout << "📋 Servidor: " << config.server_name << endl;

    // Crear servidor OPC UA
    server = UA_Server_new();
    if (!server)
    {
        cout << "❌ Error creando servidor OPC UA" << endl;
        return;
    }

    // Configurar información del servidor usando el nombre de la configuración
    UA_ServerConfig *config_server = UA_Server_getConfig(server);

    // Configurar el nombre de la aplicación que aparece en los clientes OPC-UA
    UA_LocalizedText_clear(&config_server->applicationDescription.applicationName);
    config_server->applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC("es", config.server_name.c_str());

    // Configurar otros campos informativos
    UA_String_clear(&config_server->applicationDescription.applicationUri);
    config_server->applicationDescription.applicationUri = UA_STRING_ALLOC("urn:petrosantander:scada:opcua-server");

    UA_String_clear(&config_server->applicationDescription.productUri);
    config_server->applicationDescription.productUri = UA_STRING_ALLOC("https://petrosantander.com/scada");

    UA_String_clear(&config_server->buildInfo.productName);
    config_server->buildInfo.productName = UA_STRING_ALLOC(config.server_name.c_str());

    UA_String_clear(&config_server->buildInfo.manufacturerName);
    config_server->buildInfo.manufacturerName = UA_STRING_ALLOC("PetroSantander SCADA Systems");

    UA_String_clear(&config_server->buildInfo.softwareVersion);
    config_server->buildInfo.softwareVersion = UA_STRING_ALLOC("2.0.0");

    // Crear nodos
    createNodes();

    // Crear cliente PAC
    pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
    if (pacClient->connect())
    {
        cout << "✅ Conectado al PAC: " << config.pac_ip << ":" << config.pac_port << endl;
    }
    else
    {
        cout << "⚠️ Sin conexión PAC - funcionando en modo simulación" << endl;
    }

    cout << "✅ Servidor inicializado correctamente" << endl;
    // Inicializar configuración
}

UA_StatusCode runServer()
{
    cout << "▶️ Servidor OPC UA iniciado en puerto " << config.opcua_port << endl;

    // Iniciar hilo de actualización
    thread updateThread(updateData);

    // Ejecutar servidor
    UA_StatusCode retval = UA_Server_run(server, &server_running);

    // Esperar hilo de actualización
    if (updateThread.joinable())
    {
        updateThread.join();
    }

    return retval;
}

bool getPACConnectionStatus()
{
    return pacClient && pacClient->isConnected();
}

void cleanupAndExit()
{
    cout << "\n🧹 Limpieza y terminación..." << endl;

    running = false;
    server_running = false;

    if (pacClient)
    {
        pacClient.reset();
    }

    if (server)
    {
        UA_Server_delete(server);
        server = nullptr;
    }

    cout << "✓ Limpieza completada" << endl;
}
