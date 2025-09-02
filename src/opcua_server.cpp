#include "opcua_server.h"
#include "pac_control_client.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>
#include "common.h"

using namespace std;
using json = nlohmann::json;

// ============== DECLARACIONES FORWARD ==============


// ============== VARIABLES GLOBALES ==============
UA_Server *server = nullptr;
std::unique_ptr<PACControlClient> pacClient;
Config config;

// Variables atómicas para control de hilos
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

bool loadConfig(const string& configFile)
{
    cout << "📄 Cargando configuración desde: " << configFile << endl;

    try
    {
        ifstream file(configFile);
        if (!file.is_open())
        {
            // Intentar archivo alternativo
            ifstream fallbackFile("tags.json");
            if (!fallbackFile.is_open()) {
                cout << "❌ No se pudo abrir " << configFile << " ni tags.json" << endl;
                return false;
            }
            cout << "📄 Usando archivo alternativo: tags.json" << endl;
            
            json configJson;
            fallbackFile >> configJson;
            fallbackFile.close();
            
            return processConfigFromJson(configJson);
        }

        json configJson;
        file >> configJson;
        file.close();

        return processConfigFromJson(configJson);
    }
    catch (const exception &e)
    {
        cout << "❌ Error cargando configuración: " << e.what() << endl;
        return false;
    }
}

// 🆕 NUEVA FUNCIÓN: Procesar configuración desde JSON
bool processConfigFromJson(const json& configJson) {
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
    config.api_tags.clear();
    config.simple_variables.clear();
    config.variables.clear();

    // Cargar TAGs tradicionales
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
        cout << "✓ Cargados " << config.tags.size() << " TBL_tags" << endl;
    }

    // 🆕 Cargar TBL_tags_api
    if (configJson.contains("tbl_tags_api"))
    {
        for (const auto &apiJson : configJson["tbl_tags_api"])
        {
            APITag apiTag;
            apiTag.name = apiJson.value("name", "");
            apiTag.value_table = apiJson.value("value_table", "");
            
            if (apiJson.contains("variables"))
            {
                for (const auto &var : apiJson["variables"])
                {
                    apiTag.variables.push_back(var);
                }
            }

            config.api_tags.push_back(apiTag);
        }
        cout << "✓ Cargados " << config.api_tags.size() << " API_tags" << endl;
    }

    // Cargar variables simples individuales
    if (configJson.contains("simple_variables"))
    {
        for (const auto &varJson : configJson["simple_variables"])
        {
            SimpleVariable simpleVar;
            simpleVar.name = varJson.value("name", "");
            simpleVar.pac_source = varJson.value("pac_source", "");
            simpleVar.type = varJson.value("type", "FLOAT");
            simpleVar.writable = varJson.value("writable", false);
            config.simple_variables.push_back(simpleVar);
        }
        cout << "✓ Cargadas " << config.simple_variables.size() << " variables simples" << endl;
    }

    // Procesar todas las estructuras en variables unificadas
    processConfigIntoVariables();

    cout << "✓ Configuración cargada: " << config.tags.size() << " tags, "
         << config.api_tags.size() << " api_tags, "
         << config.variables.size() << " variables totales" << endl;
    return true;
}

void processConfigIntoVariables()
{
    LOG_INFO("🔧 Procesando configuración en variables...");
    
    // Crear variables desde TAGs tradicionales
    for (const auto &tag : config.tags)
    {
        LOG_DEBUG("📋 Procesando TAG: " << tag.name);
        
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
            var.has_node = false;
            
            config.variables.push_back(var);
            LOG_DEBUG("  📝 " << var.opcua_name << " → " << var.pac_source << (var.writable ? " (R/W)" : " (R)"));
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
            var.has_node = false;
            
            config.variables.push_back(var);
            LOG_DEBUG("  🚨 " << var.opcua_name << " → " << var.pac_source << " (R)");
        }
    }

    // 🆕 Crear variables desde API_tags
    for (const auto &apiTag : config.api_tags)
    {
        LOG_DEBUG("🔧 Procesando API TAG: " << apiTag.name);
        
        for (size_t i = 0; i < apiTag.variables.size(); i++)
        {
            Variable var;
            var.opcua_name = apiTag.name + "." + apiTag.variables[i];  // "API_11001.IV"
            var.tag_name = apiTag.name;  // "API_11001"
            var.var_name = apiTag.variables[i];  // "IV"
            var.pac_source = apiTag.value_table + ":" + to_string(i);  // "TBL_API_11001:0"
            var.type = Variable::FLOAT;  // API tags son FLOAT
            var.writable = true;  // API tags son escribibles
            var.has_node = false;
            
            // Campos específicos de API
            var.api_group = apiTag.name;
            var.variable_name = apiTag.variables[i];
            var.table_index = i;
            
            config.variables.push_back(var);
            LOG_DEBUG("  🆕 " << var.opcua_name << " → " << var.pac_source << " (R/W)");
        }
        
        LOG_INFO("✅ API " << apiTag.name << ": " << apiTag.variables.size() << " variables creadas");
    }

    // Crear variables desde simple_variables
    for (const auto &simpleVar : config.simple_variables)
    {
        LOG_DEBUG("📋 Procesando variable simple: " << simpleVar.name);
        
        Variable var;
        var.opcua_name = simpleVar.name;
        var.pac_source = simpleVar.pac_source;
        var.writable = simpleVar.writable;
        var.has_node = false;

        // Determinar tipo
        if (simpleVar.type == "INT32")
        {
            var.type = Variable::INT32;
        }
        else
        {
            var.type = Variable::FLOAT;
        }

        var.tag_name = "SimpleVariables";
        var.var_name = var.opcua_name;

        config.variables.push_back(var);
        LOG_DEBUG("  📌 " << var.opcua_name << " → " << var.pac_source << (var.writable ? " (R/W)" : " (R)"));
    }
    
    LOG_INFO("✅ Procesamiento completado: " << config.variables.size() << " variables totales");
    
    // Estadísticas por tipo
    int float_vars = 0, int_vars = 0, writable_vars = 0;
    for (const auto &var : config.variables) {
        if (var.type == Variable::FLOAT) float_vars++;
        if (var.type == Variable::INT32) int_vars++;
        if (var.writable) writable_vars++;
    }
    
    LOG_INFO("📊 Estadísticas: " << float_vars << " FLOAT, " << int_vars << " INT32, " << writable_vars << " escribibles");
}

// ============== CALLBACK DE ESCRITURA ==============

static UA_StatusCode writeCallback(UA_Server *server,
                                  const UA_NodeId *sessionId,
                                  void *sessionContext,
                                  const UA_NodeId *nodeId,
                                  void *nodeContext,
                                  const UA_NumericRange *range,
                                  const UA_DataValue *data) {
    
    // 🔍 IGNORAR ESCRITURAS INTERNAS DEL SERVIDOR
    if (server_writing_internally.load()) {
        return UA_STATUSCODE_GOOD;
    }

    // 🔒 EVITAR PROCESAMIENTO DURANTE ACTUALIZACIONES
    if (updating_internally.load()) {
        return UA_STATUSCODE_BADRESOURCEUNAVAILABLE;
    }
    
    // ✅ VALIDACIONES BÁSICAS
    if (!server || !nodeId || !data || !data->value.data) {
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    if (nodeId->identifierType != UA_NODEIDTYPE_STRING) {
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }

    // 🔒 CONVERSIÓN SEGURA DE NODEID
    std::string nodeIdStr;
    try {
        UA_String *nodeIdString = &nodeId->identifier.string;
        nodeIdStr = std::string((char*)nodeIdString->data, nodeIdString->length);
    } catch (...) {
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }
    
    DEBUG_INFO("📝 ESCRITURA RECIBIDA: " << nodeIdStr);

    // 🔍 BUSCAR VARIABLE EN CONFIGURACIÓN
    Variable *var = nullptr;
    for (auto &v : config.variables) {
        if (v.opcua_name == nodeIdStr) {
            var = &v;
            break;
        }
    }

    if (!var) {
        LOG_ERROR("Variable no encontrada: " << nodeIdStr);
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }

    if (!var->writable) {
        LOG_ERROR("Variable no escribible: " << nodeIdStr);
        return UA_STATUSCODE_BADNOTWRITABLE;
    }

    // 🔒 VERIFICAR CONEXIÓN PAC
    if (!pacClient || !pacClient->isConnected()) {
        LOG_ERROR("PAC no conectado para escritura: " << nodeIdStr);
        return UA_STATUSCODE_BADRESOURCEUNAVAILABLE;
    }

    DEBUG_INFO("✅ Procesando escritura para: " << var->opcua_name << " (PAC: " << var->pac_source << ")");

    // 🎯 PROCESAR ESCRITURA SEGÚN TIPO
    bool write_success = false;

    if (var->type == Variable::FLOAT) {
        if (data->value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
            float value = *(float*)data->value.data;
            // Aquí irían las llamadas al PAC
            LOG_WRITE("Escribiendo FLOAT " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        }
    } else if (var->type == Variable::INT32) {
        if (data->value.type == &UA_TYPES[UA_TYPES_INT32]) {
            int32_t value = *(int32_t*)data->value.data;
            // Aquí irían las llamadas al PAC
            LOG_WRITE("Escribiendo INT32 " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        }
    }

    // ✅ RESULTADO FINAL
    if (write_success) {
        LOG_WRITE("✅ Escritura exitosa: " << nodeIdStr);
        return UA_STATUSCODE_GOOD;
    } else {
        LOG_ERROR("❌ Error en escritura: " << nodeIdStr);
        return UA_STATUSCODE_BADTYPEMISMATCH;
    }
}

// ============== CREACIÓN DE NODOS ==============

void createNodes()
{
    DEBUG_INFO("🏗️ Creando nodos OPC-UA...");

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
        oAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("en", tagName.c_str());

        UA_NodeId tagNodeId;
        UA_StatusCode result = UA_Server_addObjectNode(
            server,
            UA_NODEID_STRING_ALLOC(1, tagName.c_str()),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME_ALLOC(1, tagName.c_str()),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
            oAttr, nullptr, &tagNodeId);

        if (result != UA_STATUSCODE_GOOD)
        {
            LOG_ERROR("Error creando TAG: " << tagName);
            continue;
        }

        // Crear variables del TAG
        for (auto var : variables)
        {
            UA_VariableAttributes vAttr = UA_VariableAttributes_default;
            vAttr.displayName = UA_LOCALIZEDTEXT_ALLOC("en", var->var_name.c_str());

            UA_Variant_init(&vAttr.value);

            // Configurar según tipo de variable
            if (var->type == Variable::FLOAT)
            {
                float initialValue = 0.0f;
                UA_Variant_setScalar(&vAttr.value, &initialValue, &UA_TYPES[UA_TYPES_FLOAT]);
            }
            else if (var->type == Variable::INT32)
            {
                int32_t initialValue = 0;
                UA_Variant_setScalar(&vAttr.value, &initialValue, &UA_TYPES[UA_TYPES_INT32]);
            }

            vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
            if (var->writable)
            {
                vAttr.accessLevel |= UA_ACCESSLEVELMASK_WRITE;
            }

            UA_NodeId varNodeId = UA_NODEID_STRING_ALLOC(1, var->opcua_name.c_str());
            
            UA_StatusCode varResult = UA_Server_addVariableNode(
                server,
                varNodeId,
                tagNodeId,
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                UA_QUALIFIEDNAME_ALLOC(1, var->var_name.c_str()),
                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                vAttr, nullptr, nullptr);

            if (varResult == UA_STATUSCODE_GOOD)
            {
                var->has_node = true;

                // 🔧 CONFIGURAR CALLBACK DE ESCRITURA CORRECTAMENTE
                if (var->writable)
                {
                    UA_ValueCallback callback;
                    callback.onRead = nullptr;
                    callback.onWrite = writeCallback;
                    
                    UA_StatusCode callbackResult = UA_Server_setVariableNode_valueCallback(
                        server, varNodeId, callback);
                        
                    if (callbackResult != UA_STATUSCODE_GOOD) {
                        LOG_ERROR("Error configurando callback para: " << var->opcua_name);
                    }
                }
                
                LOG_DEBUG("✅ Nodo creado: " << var->opcua_name);
            }
            else
            {
                LOG_ERROR("❌ Error creando variable: " << var->opcua_name);
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
        LOG_DEBUG("Iniciando ciclo de actualización PAC");
        
        // 🔒 VERIFICAR SI ES SEGURO HACER ACTUALIZACIONES - Usar load()
        if (updating_internally.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (pacClient && pacClient->isConnected())
        {
            DEBUG_INFO("🔄 Iniciando actualización de datos del PAC");
            
            // ⚠️ MARCAR ACTUALIZACIÓN EN PROGRESO - Usar store()
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
                    DEBUG_INFO("⚠️ Variable simple saltada: " << var.opcua_name << " (función no implementada)");
                }
            }

            // 📊 ACTUALIZAR VARIABLES DE TABLA ÚNICAMENTE
            int tables_updated = 0;
            for (const auto &[tableName, vars] : tableVars)
            {
                if (vars.empty())
                    continue;

                DEBUG_INFO("📋 Actualizando tabla: " << tableName << " (" << vars.size() << " variables)");

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
                    DEBUG_INFO("⚠️ Tabla sin índices válidos: " << tableName);
                    continue;
                }

                int minIndex = *min_element(indices.begin(), indices.end());
                int maxIndex = *max_element(indices.begin(), indices.end());

                DEBUG_INFO("🔢 Leyendo tabla " << tableName << " índices [" << minIndex << "-" << maxIndex << "]");

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
                        DEBUG_INFO("❌ Error leyendo tabla INT32: " << tableName);
                        continue;
                    }

                    DEBUG_INFO("✅ Leída tabla INT32: " << tableName << " (" << values.size() << " valores)");

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
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            int32_t newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            
                            if (result == UA_STATUSCODE_GOOD) {
                                vars_updated++;
                                DEBUG_INFO("📝 " << var->opcua_name << " = " << newValue);
                            } else {
                                DEBUG_INFO("❌ Error actualizando " << var->opcua_name);
                            }
                        }
                    }
                    
                    DEBUG_INFO("✅ Tabla INT32 " << tableName << ": " << vars_updated << " variables actualizadas");
                }
                else
                {
                    // 📊 LEER TABLA DE VALORES (FLOAT)
                    
                    // 🆕 DETECTAR SI ES TABLA API
                    bool isAPITable = tableName.find("TBL_API_") == 0;
                    
                    vector<float> values = pacClient->readFloatTable(tableName, minIndex, maxIndex);

                    if (values.empty()) {
                        DEBUG_INFO("❌ Error leyendo tabla FLOAT: " << tableName);
                        continue;
                    }

                    if (isAPITable) {
                        DEBUG_INFO("✅ Leída tabla API: " << tableName << " (" << values.size() << " valores)");
                    } else {
                        DEBUG_INFO("✅ Leída tabla FLOAT: " << tableName << " (" << values.size() << " valores)");
                    }

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
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            float newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            
                            if (result == UA_STATUSCODE_GOOD) {
                                vars_updated++;
                                DEBUG_INFO("📝 " << var->opcua_name << " = " << newValue);
                            } else {
                                DEBUG_INFO("❌ Error actualizando " << var->opcua_name);
                            }
                        }
                    }
                    
                    DEBUG_INFO("✅ Tabla FLOAT " << tableName << ": " << vars_updated << " variables actualizadas");
                }

                tables_updated++;
                
                // Pequeña pausa entre tablas
                this_thread::sleep_for(chrono::milliseconds(50));
            }

            // 🔓 DESACTIVAR BANDERAS - Usar store()
            server_writing_internally.store(false);
            updating_internally.store(false);
            
            DEBUG_INFO("✅ Actualización completada");
        }
        else
        {
            // 🔌 SIN CONEXIÓN PAC - INTENTAR RECONECTAR
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<chrono::seconds>(now - lastReconnect).count() >= 10)
            {
                DEBUG_INFO("🔄 Intentando reconectar al PAC: " << config.pac_ip << ":" << config.pac_port);
                
                pacClient.reset();
                pacClient = make_unique<PACControlClient>(config.pac_ip, config.pac_port);
                
                if (pacClient->connect())
                {
                    DEBUG_INFO("✅ Reconectado al PAC exitosamente");
                }
                else
                {
                    DEBUG_INFO("❌ Fallo en reconexión al PAC");
                }
                lastReconnect = now;
            }
        }

        // ⏱️ ESPERAR INTERVALO DE ACTUALIZACIÓN
        this_thread::sleep_for(chrono::milliseconds(config.update_interval_ms));
    }
    
    DEBUG_INFO("🛑 Hilo de actualización terminado");
}

// ============== FUNCIONES PRINCIPALES ==============

bool ServerInit()
{
    cout << "🚀 Inicializando servidor OPC UA..." << endl;

    // Cargar configuración
    try
    {
        if (!loadConfig("config.json"))
        {
            cout << "❌ Error cargando configuración - abortando" << endl;
            return false;  // ✅ CORREGIDO
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    

    cout << "📋 Servidor: " << config.server_name << endl;

    // Crear servidor OPC UA
    server = UA_Server_new();
    if (!server)
    {
        cout << "❌ Error creando servidor OPC UA" << endl;
        return false;  // ✅ CORREGIDO
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
    return true;  // ✅ AGREGAR retorno exitoso
}

UA_StatusCode runServer() {
    if (!server) {
        LOG_ERROR("Servidor no inicializado");
        return UA_STATUSCODE_BADINTERNALERROR;
    }
    
    LOG_INFO("▶️ Servidor OPC-UA iniciado en puerto " << config.opcua_port);

    // Iniciar hilo de actualización de datos
    std::thread updateThread(updateData);

    try {
        // Sincronizar variable bool normal con atomic
        server_running_flag = server_running.load();
        
        // Ejecutar servidor principal (requiere bool*)
        UA_StatusCode retval = UA_Server_run(server, &server_running_flag);
        
        // Actualizar variable atomic cuando termine
        server_running = false;
        running = false;
        
        // Esperar que termine el hilo de actualización
        if (updateThread.joinable()) {
            LOG_INFO("⏳ Esperando hilo de actualización...");
            
            // Dar tiempo para que termine elegantemente
            auto start = std::chrono::steady_clock::now();
            while (updateThread.joinable() && 
                   std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (updateThread.joinable()) {
                LOG_INFO("🔄 Forzando terminación de hilo de actualización");
                updateThread.detach();
            } else {
                updateThread.join();
            }
        }
        
        return retval;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Excepción en runServer: " << e.what());
        
        if (updateThread.joinable()) {
            updateThread.detach();
        }
        
        return UA_STATUSCODE_BADINTERNALERROR;
    }
}

void shutdownServer()
{
    if (server_running) {
        LOG_INFO("🛑 Deteniendo servidor OPC-UA...");
        server_running = false;
        server_running_flag = false;
    } else {
        LOG_INFO("⚠️ Servidor ya está detenido");
    }   
}

bool getPACConnectionStatus()
{
    return pacClient && pacClient->isConnected();
}

void cleanupServer() {
    LOG_INFO("🧹 Limpiando recursos del servidor OPC-UA...");
    
    try {
        // Marcar que estamos cerrando
        running = false;
        server_running = false;
        
        // Esperar que terminen las operaciones en curso
        if (updating_internally.load()) {
            LOG_INFO("⏳ Esperando que termine actualización en curso...");
            for (int i = 0; i < 30 && updating_internally.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        // Limpiar cliente PAC
        if (pacClient) {
            LOG_INFO("🔌 Desconectando cliente PAC...");
            pacClient->disconnect();
            pacClient.reset();
        }
        
        // Limpiar servidor OPC-UA
        if (server) {
            LOG_INFO("🌐 Eliminando servidor OPC-UA...");
            UA_Server_delete(server);
            server = nullptr;
        }
        
        // Limpiar configuración
        config.variables.clear();
        config.tags.clear();
        config.api_tags.clear();
        config.simple_variables.clear();
        config.batch_tags.clear();

        LOG_INFO("✅ Recursos liberados correctamente");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error durante limpieza: " << e.what());
    } catch (...) {
        LOG_ERROR("Error desconocido durante limpieza");
    }
}

// Función auxiliar para getVariableIndex específico de API
int getAPIVariableIndex(const std::string &varName) {
    // Para API tags: IV=0, NSV=1, CPL=2, CTL=3
    if (varName == "IV") return 0;    // Input Value
    if (varName == "NSV") return 1;   // Net Standard Volume  
    if (varName == "CPL") return 2;   // Corrected Pipeline
    if (varName == "CTL") return 3;   // Control
    
    // Fallback a función general
    return getVariableIndex(varName);
}

int getBatchVariableIndex(const std::string &varName)
{
    if (varName == "Tiquete") return 0;
    if (varName == "Cliente") return 1;
    if (varName == "Producto") return 2;
    if (varName == "Presion") return 3;
    if (varName == "Temperatura") return 4;
    if (varName == "Preision_EQ") return 5;
    if (varName == "Densidad_(@60ºF)") return 6;
    if (varName == "Densidad_OBSV") return 7;
    if (varName == "Flujo_Indicado") return 8;
    if (varName == "Flujo_Bruto") return 9;
    if (varName == "Flujo_Neto_STD") return 10;
    if (varName == "Volumen_Indicado") return 11;
    if (varName == "Volumen_Bruto") return 12;
    if (varName == "Volumen_Neto_STD") return 13;

    return 0;
}

// Alias para compatibilidad con main.cpp
void cleanupAndExit() {
    cleanupServer();
}
