#include "opcua_server.h"
#include "pac_control_client.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <stdexcept>
#include <cmath>     // 🔧 AGREGAR PARA std::isnan, std::isinf

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
    // ========== VARIABLES DE TABLAS TRADICIONALES (TT, LT, DT, PT) ==========
    // Estructura: [Input, SetHH, SetH, SetL, SetLL, SIM_Value, PV, min, max, percent]
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
        return 7;
    if (varName == "max")
        return 8;
    if (varName == "percent")
        return 9;

    // ========== VARIABLES DE ALARMA (TBL_TA_, TBL_LA_, TBL_DA_, TBL_PA_) ==========
    // Estructura: [HH, H, L, LL, Color]
    if (varName == "HH" || varName == "ALARM_HH")
        return 0;
    if (varName == "H" || varName == "ALARM_H")
        return 1;
    if (varName == "L" || varName == "ALARM_L")
        return 2;
    if (varName == "LL" || varName == "ALARM_LL")
        return 3;
    if (varName == "Color" || varName == "ALARM_Color")
        return 4;

    // ========== VARIABLES DE PID (TBL_FIT_) ==========
    // Estructura: [PV, SP, CV, auto_manual, Kp, Ki, Kd]
    if (varName == "SP")
        return 1; // SetPoint
    if (varName == "CV")
        return 2; // Control Value
    if (varName == "auto_manual")
        return 3;
    if (varName == "Kp")
        return 4;
    if (varName == "Ki")
        return 5;
    if (varName == "Kd")
        return 6;

    // Compatibilidad con nombres alternativos
    if (varName == "Min")
        return 7;
    if (varName == "Max")
        return 8;
    if (varName == "Percent")
        return 9;

    LOG_DEBUG("⚠️ Índice no encontrado para variable: " << varName << " (usando -1)");
    return -1;
}

int getAPIVariableIndex(const std::string &varName)
{
    // ========== VARIABLES DE API SEGÚN TU JSON ==========
    // Estructura de tbl_api: ["IV", "NSV", "CPL", "CTL"]

    if (varName == "IV")
        return 0; // Indicated Value
    if (varName == "NSV")
        return 1; // Net Standard Volume
    if (varName == "CPL")
        return 2; // Compensación de Presión y Línea
    if (varName == "CTL")
        return 3; // Control

    LOG_DEBUG("⚠️ Índice API no encontrado para: " << varName);
    return -1;
}

int getBatchVariableIndex(const std::string &varName)
{
    // ========== VARIABLES DE BATCH SEGÚN TU JSON ==========
    // Estructura: ["No_Tiquete", "Cliente", "Producto", "Presion", ...]

    if (varName == "No_Tiquete")
        return 0;
    if (varName == "Cliente")
        return 1;
    if (varName == "Producto")
        return 2;
    if (varName == "Presion")
        return 3;
    if (varName == "Temperatura")
        return 4;
    if (varName == "Precision_EQ")
        return 5;
    if (varName == "Densidad_(@60ºF)")
        return 6;
    if (varName == "Densidad_OBSV")
        return 7;
    if (varName == "Flujo_Indicado")
        return 8;
    if (varName == "Flujo_Bruto")
        return 9;
    if (varName == "Flujo_Neto_STD")
        return 10;
    if (varName == "Volumen_Indicado")
        return 11;
    if (varName == "Volumen_Bruto")
        return 12;
    if (varName == "Volumen_Neto_STD")
        return 13;

    LOG_DEBUG("⚠️ Índice BATCH no encontrado para: " << varName);
    return -1;
}

bool isWritableVariable(const std::string &varName)
{
    // Variables escribibles tradicionales
    bool writable = varName.find("Set") == 0 ||           // SetHH, SetH, SetL, SetLL
                    varName.find("SIM_") == 0 ||          // SIM_Value
                    varName.find("SP") != string::npos || // SP (SetPoint PID)
                    varName.find("CV") != string::npos || // CV (Control Value PID)
                    varName.find("Kp") != string::npos || // Parámetros PID
                    varName.find("Ki") != string::npos ||
                    varName.find("Kd") != string::npos ||
                    varName.find("auto_manual") != string::npos ||
                    // 🔧 AGREGAR VARIABLES API ESCRIBIBLES
                    varName == "CPL" || // Compensación API
                    varName == "CTL";   // Control API

    if (writable)
    {
        LOG_DEBUG("📝 Variable escribible detectada: " << varName);
    }

    return writable;
}

// ============== CONFIGURACIÓN ==============

bool loadConfig(const string &configFile)
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
            "config/config.json"};

        json configJson;
        bool configLoaded = false;

        for (const auto &fileName : configFiles)
        {
            ifstream file(fileName);
            if (file.is_open())
            {
                cout << "📄 Usando archivo: " << fileName << endl;
                file >> configJson;
                file.close();
                configLoaded = true;
                break;
            }
        }

        if (!configLoaded)
        {
            cout << "❌ No se encontró ningún archivo de configuración" << endl;
            cout << "📝 Archivos intentados:" << endl;
            for (const auto &fileName : configFiles)
            {
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

bool processConfigFromJson(const json &configJson)
{
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

    // 🔧 LIMPIAR CONFIGURACIÓN ANTERIOR
    config.clear();

    // 📋 PROCESAR SIMPLE_VARIABLES
    if (configJson.contains("simple_variables")) {
        for (const auto &simpleVar : configJson["simple_variables"]) {
            Variable var;
            var.opcua_name = simpleVar.value("name", "");
            var.tag_name = "SimpleVars";
            var.var_name = simpleVar.value("name", "");
            var.pac_source = simpleVar.value("pac_source", var.var_name);
            var.description = simpleVar.value("description", "");
            
            // 🔧 USAR TIPO DEL JSON O INFERIR DEL PREFIJO
            std::string jsonType = simpleVar.value("type", "");
            if (jsonType == "FLOAT" || var.var_name.find("F_") == 0) {
                var.type = Variable::FLOAT;
            } else if (jsonType == "INT32" || var.var_name.find("I_") == 0) {
                var.type = Variable::INT32;
            } else {
                var.type = Variable::FLOAT; // Por defecto
            }
            
            var.writable = simpleVar.value("writable", false);
            var.table_index = -1;

            config.variables.push_back(var);
            LOG_DEBUG("🔧 Variable simple " << (var.type == Variable::FLOAT ? "FLOAT" : "INT32") << ": " << var.opcua_name);
        }
        
        LOG_INFO("✓ Cargadas " << configJson["simple_variables"].size() << " variables simples");
    }

    // 📊 PROCESAR TBL_TAGS (tradicionales - TT, LT, DT, PT)
    if (configJson.contains("tbL_tags"))
    {
        LOG_INFO("🔍 Procesando tbL_tags...");

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
            LOG_DEBUG("✅ TBL_tag: " << tag.name << " (" << tag.variables.size() << " vars, " << tag.alarms.size() << " alarms)");
        }
        LOG_INFO("✓ Cargados " << config.tags.size() << " TBL_tags");
    }

    // 🔧 PROCESAR TBL_API (tablas API) - CORREGIR NOMBRE DE SECCIÓN
    if (configJson.contains("tbl_api"))
    { // 🔧 ERA "tbl_api" NO "tbl_api"
        LOG_INFO("🔍 Procesando tbl_api...");

        for (const auto &apiJson : configJson["tbl_api"])
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
            LOG_DEBUG("✅ API_tag: " << apiTag.name << " (" << apiTag.variables.size() << " variables)");
        }
        LOG_INFO("✓ Cargados " << config.api_tags.size() << " API_tags");
    }

    // 📦 PROCESAR TBL_BATCH (tablas de lote) - CORREGIR NOMBRE DE SECCIÓN
    if (configJson.contains("tbl_batch"))
    { // 🔧 YA ESTÁ CORRECTO
        LOG_INFO("🔍 Procesando tbl_batch...");

        for (const auto &batchJson : configJson["tbl_batch"])
        {
            BatchTag batchTag;
            batchTag.name = batchJson.value("name", "");
            batchTag.value_table = batchJson.value("value_table", "");

            if (batchJson.contains("variables"))
            {
                for (const auto &var : batchJson["variables"])
                {
                    batchTag.variables.push_back(var);
                }
            }

            config.batch_tags.push_back(batchTag);
            LOG_DEBUG("✅ Batch_tag: " << batchTag.name << " (" << batchTag.variables.size() << " variables)");
        }
        LOG_INFO("✓ Cargados " << config.batch_tags.size() << " Batch_tags");
    }

    // 🎛️ PROCESAR TBL_PID (controladores PID)
    if (configJson.contains("tbl_pid"))
    {
        LOG_INFO("🔍 Procesando tbl_pid...");

        for (const auto &pidJson : configJson["tbl_pid"])
        {
            Tag pidTag; // Usar estructura Tag normal
            pidTag.name = pidJson.value("name", "");
            pidTag.value_table = pidJson.value("value_table", "");
            pidTag.alarm_table = ""; // Los PID normalmente no tienen alarmas

            if (pidJson.contains("variables"))
            {
                for (const auto &var : pidJson["variables"])
                {
                    pidTag.variables.push_back(var);
                }
            }

            config.tags.push_back(pidTag); // Agregar a tags normales
            LOG_DEBUG("✅ PID_tag: " << pidTag.name << " (" << pidTag.variables.size() << " variables)");
        }
        LOG_INFO("✓ Cargados PID_tags como tags tradicionales");
    }

    // Procesar configuración en variables
    processConfigIntoVariables();

    return true;
}

void processConfigIntoVariables()
{
    LOG_INFO("🔧 Procesando configuración en variables...");

    // 📋 CREAR VARIABLES DESDE SIMPLE_VARIABLES
    for (const auto &simpleVar : config.simple_variables)
    {
        if (simpleVar.name.empty())
        {
            LOG_DEBUG("⚠️ Variable simple sin nombre, omitiendo");
            continue;
        }

        Variable var;
        var.opcua_name = simpleVar.name;
        var.tag_name = "SimpleVars";
        var.var_name = simpleVar.name;
        var.pac_source = simpleVar.pac_source; // F_xxx o I_xxx directo
        var.type = (simpleVar.type == "INT32") ? Variable::INT32 : Variable::FLOAT;
        var.writable = simpleVar.writable;
        var.description = simpleVar.description;

        config.variables.push_back(var);
    }

    // 📊 CREAR VARIABLES DESDE TBL_TAGS 
    for (const auto &tag : config.tags)
    {
        // Variables de valores
        for (const auto &varName : tag.variables)
        {
            Variable var;
            var.opcua_name = tag.name + "." + varName;
            var.tag_name = tag.name;
            var.var_name = varName;
            var.pac_source = tag.value_table + ":" + to_string(getVariableIndex(varName));
            
            // 🔧 REGLAS CORRECTAS SEGÚN ESPECIFICACIÓN:
            // TT, PT, LT (Temperature, Pressure, Level) → FLOAT
            // TA, PA, LA (Alarm tags) → INT32
            // Color → INT32
            // ALARM_xxx → INT32
            
            if (varName.find("ALARM_") == 0 || 
                varName == "Color" ||
                tag.name.find("TA_") == 0 ||
                tag.name.find("PA_") == 0 ||
                tag.name.find("LA_") == 0) {
                var.type = Variable::INT32;
                LOG_DEBUG("🔧 Variable INT32: " << var.opcua_name);
            } else {
                // 🔧 TT_, PT_, LT_ y todas las demás variables son FLOAT
                var.type = Variable::FLOAT;
                LOG_DEBUG("🔧 Variable FLOAT: " << var.opcua_name);
            }
            
            var.writable = isWritableVariable(varName);
            var.table_index = getVariableIndex(varName);  // 🔧 LÍNEA CORREGIDA
            
            config.variables.push_back(var);
        }

        // Variables de alarmas - SIEMPRE INT32
        if (!tag.alarm_table.empty())
        {
            for (const auto &alarmName : tag.alarms)
            {
                Variable var;
                var.opcua_name = tag.name + ".ALARM_" + alarmName;
                var.tag_name = tag.name;
                var.var_name = "ALARM_" + alarmName;
                var.pac_source = tag.alarm_table + ":" + to_string(getVariableIndex(alarmName));
                var.type = Variable::INT32; // 🔧 ALARMAS SIEMPRE INT32
                var.writable = false;       
                var.table_index = getVariableIndex(alarmName);

                config.variables.push_back(var);
                LOG_DEBUG("🔧 Variable ALARM INT32: " << var.opcua_name);
            }
        }
    }

    // 🔧 CREAR VARIABLES DESDE API_TAGS (TODAS FLOAT)
    for (const auto &apiTag : config.api_tags)
    {
        for (const auto &varName : apiTag.variables)
        {
            Variable var;
            var.opcua_name = apiTag.name + "." + varName;
            var.tag_name = apiTag.name;
            var.var_name = varName;
            var.pac_source = apiTag.value_table + ":" + to_string(getAPIVariableIndex(varName)); // 🔧 CORREGIR: value_table en lugar de table
            var.type = Variable::FLOAT;  // 🔧 API TAGS SON FLOAT
            var.writable = isWritableVariable(varName);
            var.table_index = getAPIVariableIndex(varName);

            config.variables.push_back(var);
            LOG_DEBUG("🔧 Variable API FLOAT: " << var.opcua_name);
        }
    }

    // 📦 CREAR VARIABLES DESDE BATCH_TAGS CON ÍNDICES CORRECTOS
    for (const auto &batchTag : config.batch_tags)
    {
        for (size_t i = 0; i < batchTag.variables.size(); i++)
        {
            const auto &varName = batchTag.variables[i];

            Variable var;
            var.opcua_name = batchTag.name + "." + varName;
            var.tag_name = batchTag.name;
            var.var_name = varName;
            var.pac_source = batchTag.value_table + ":" + to_string(getBatchVariableIndex(varName)); // 🔧 VERIFICAR: value_table

            var.type = Variable::FLOAT; // Valores de proceso
            

            var.writable = false; // Batch tags normalmente son solo lectura (datos históricos)
            var.table_index = getBatchVariableIndex(varName);

            config.variables.push_back(var);

            LOG_DEBUG("✅ Batch variable: " << var.opcua_name << " (índice: " << var.table_index << ", tipo: " << (var.type == Variable::FLOAT ? "FLOAT" : "INT32") << ")");
        }
    }

    // 🔧 VALIDAR TIPOS DE TODAS LAS VARIABLES ANTES DE CONTINUAR
    LOG_INFO("🔧 Validando tipos de variables...");
    int invalidTypes = 0;
    for (auto &var : config.variables) {
        if (var.type != Variable::FLOAT && var.type != Variable::INT32) {
            LOG_ERROR("❌ Variable con tipo inválido: " << var.opcua_name << " (tipo: " << var.type << ")");
            var.type = Variable::FLOAT; // Corregir a FLOAT
            invalidTypes++;
        }
    }
    
    if (invalidTypes > 0) {
        LOG_INFO("🔧 Corregidos " << invalidTypes << " tipos inválidos a FLOAT");
    }

    LOG_INFO("✅ Procesamiento completado:");
    LOG_INFO("   📋 Variables simples: " << config.simple_variables.size());
    LOG_INFO("   📊 TBL_tags: " << config.tags.size());
    LOG_INFO("   🔧 API_tags: " << config.api_tags.size());
    LOG_INFO("   📦 Batch_tags: " << config.batch_tags.size());
    LOG_INFO("   🎯 Total variables OPC-UA: " << config.getTotalVariableCount());
    LOG_INFO("   📝 Variables escribibles: " << config.getWritableVariableCount());

    // 🔧 RESUMEN DE TIPOS APLICADOS CON VARIABLES SIMPLES
    int floatCount = 0, int32Count = 0;
    int simpleFloatCount = 0, simpleInt32Count = 0;
    
    for (const auto &var : config.variables) {
        if (var.type == Variable::FLOAT) {
            floatCount++;
            if (var.tag_name == "SimpleVars" && var.var_name.find("F_") == 0) {
                simpleFloatCount++;
            }
        } else if (var.type == Variable::INT32) {
            int32Count++;
            if (var.tag_name == "SimpleVars" && var.var_name.find("I_") == 0) {
                simpleInt32Count++;
            }
        }
    }
    
    LOG_INFO("📊 Tipos de variables asignados:");
    LOG_INFO("   🔢 FLOAT: " << floatCount << " total");
    LOG_INFO("     ├─ TT_/PT_/LT_/API_/BATCH_: " << (floatCount - simpleFloatCount));
    LOG_INFO("     └─ Variables simples F_xxx: " << simpleFloatCount);
    LOG_INFO("   🔢 INT32: " << int32Count << " total");
    LOG_INFO("     ├─ TA_/PA_/LA_/ALARM_/Color: " << (int32Count - simpleInt32Count));
    LOG_INFO("     └─ Variables simples I_xxx: " << simpleInt32Count);
}

// ============== CALLBACKS CORREGIDOS ==============

// Para UA_DataSource.read
static UA_StatusCode readCallback(UA_Server *server,
                                  const UA_NodeId *sessionId, void *sessionContext,
                                  const UA_NodeId *nodeId, void *nodeContext,
                                  UA_Boolean sourceTimeStamp,
                                  const UA_NumericRange *range,
                                  UA_DataValue *dataValue)
{
    // 🔍 IGNORAR LECTURAS DURANTE ACTUALIZACIONES INTERNAS
    if (updating_internally.load())
    {
        return UA_STATUSCODE_GOOD;
    }

    // 🔧 CORREGIR PARA NODEID STRING
    if (nodeId && nodeId->identifierType == UA_NODEIDTYPE_STRING)
    {
        std::string stringNodeId = std::string((char*)nodeId->identifier.string.data, nodeId->identifier.string.length);
        LOG_DEBUG("📖 Lectura de NodeId: " << stringNodeId);
    }

    return UA_STATUSCODE_GOOD;
}

// Para UA_DataSource.write
static UA_StatusCode writeCallback(UA_Server *server,
                                   const UA_NodeId *sessionId, void *sessionContext,
                                   const UA_NodeId *nodeId, void *nodeContext,
                                   const UA_NumericRange *range,
                                   const UA_DataValue *data)
{

    // 🔍 IGNORAR ESCRITURAS INTERNAS DEL SERVIDOR
    if (server_writing_internally.load())
    {
        return UA_STATUSCODE_GOOD;
    }

    // 🔒 EVITAR PROCESAMIENTO DURANTE ACTUALIZACIONES
    if (updating_internally.load())
    {
        LOG_ERROR("Escritura rechazada: servidor actualizando");
        return UA_STATUSCODE_BADNOTWRITABLE;
    }

    // ✅ VALIDACIONES BÁSICAS
    if (!server || !nodeId || !data || !data->value.data)
    {
        LOG_ERROR("Parámetros inválidos en writeCallback");
        return UA_STATUSCODE_BADINVALIDARGUMENT;
    }

    // 🔧 MANEJAR NODEID STRING (COMO EN LA VERSIÓN ORIGINAL)
    if (nodeId->identifierType != UA_NODEIDTYPE_STRING)
    {
        LOG_ERROR("Tipo de NodeId no soportado (esperado string)");
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }

    std::string stringNodeId = std::string((char *)nodeId->identifier.string.data, nodeId->identifier.string.length);
    LOG_WRITE("📝 ESCRITURA RECIBIDA en NodeId: " << stringNodeId);

    // 🔍 BUSCAR VARIABLE POR NODEID STRING
    Variable *var = nullptr;
    for (auto &v : config.variables)
    {
        if (v.has_node && v.opcua_name == stringNodeId)
        {
            var = &v;
            break;
        }
    }

    if (!var)
    {
        LOG_ERROR("Variable no encontrada para NodeId: " << stringNodeId);
        return UA_STATUSCODE_BADNODEIDUNKNOWN;
    }

    if (!var->writable)
    {
        LOG_ERROR("Variable no escribible: " << var->opcua_name);
        return UA_STATUSCODE_BADNOTWRITABLE;
    }

    // 🔒 VERIFICAR CONEXIÓN PAC
    if (!pacClient || !pacClient->isConnected())
    {
        LOG_ERROR("PAC no conectado para escritura: " << var->opcua_name);
        return UA_STATUSCODE_BADCONNECTIONCLOSED;
    }

    LOG_DEBUG("✅ Procesando escritura para: " << var->opcua_name << " (PAC: " << var->pac_source << ")");

    // 🎯 PROCESAR ESCRITURA SEGÚN TIPO
    bool write_success = false;

    if (var->type == Variable::FLOAT)
    {
        if (data->value.type == &UA_TYPES[UA_TYPES_FLOAT])
        {
            float value = *(float *)data->value.data;
            // TODO: Implementar escritura real al PAC
            LOG_WRITE("Escribiendo FLOAT " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        }
        else
        {
            LOG_ERROR("Tipo de dato incorrecto para variable FLOAT: " << var->opcua_name);
            return UA_STATUSCODE_BADTYPEMISMATCH;
        }
    }
    else if (var->type == Variable::INT32)
    {
        if (data->value.type == &UA_TYPES[UA_TYPES_INT32])
        {
            int32_t value = *(int32_t *)data->value.data;
            // TODO: Implementar escritura real al PAC
            LOG_WRITE("Escribiendo INT32 " << value << " a " << var->pac_source);
            write_success = true; // Placeholder
        }
        else
        {
            LOG_ERROR("Tipo de dato incorrecto para variable INT32: " << var->opcua_name);
            return UA_STATUSCODE_BADTYPEMISMATCH;
        }
    }

    // ✅ RESULTADO FINAL
    if (write_success)
    {
        LOG_WRITE("✅ Escritura exitosa: " << var->opcua_name);
        return UA_STATUSCODE_GOOD;
    }
    else
    {
        LOG_ERROR("❌ Error en escritura: " << var->opcua_name);
        return UA_STATUSCODE_BADINTERNALERROR;
    }
}

// ============== CREACIÓN DE NODOS ==============

void createNodes()
{
    LOG_INFO("🏗️ Creando nodos OPC-UA con estructura jerárquica original...");

    if (!server)
    {
        LOG_ERROR("Servidor no inicializado");
        return;
    }

    // 🗂️ AGRUPAR VARIABLES POR TAG (COMO EN LA VERSIÓN ORIGINAL)
    map<string, vector<Variable *>> tagVars;
    for (auto &var : config.variables)
    {
        tagVars[var.tag_name].push_back(&var);
    }

    LOG_INFO("📊 Creando " << tagVars.size() << " TAGs con estructura jerárquica");

    // 🏗️ CREAR CADA TAG CON SUS VARIABLES (ESTRUCTURA ORIGINAL)
    for (const auto &[tagName, variables] : tagVars)
    {
        LOG_INFO("📁 Creando TAG: " << tagName << " (" << variables.size() << " variables)");

        // 🏗️ CREAR NODO TAG COMO CARPETA PADRE
        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char *>(tagName.c_str()));

        UA_NodeId tagNodeId;
        UA_StatusCode result = UA_Server_addObjectNode(
            server,
            UA_NODEID_STRING(1, const_cast<char *>(tagName.c_str())), // 🔧 STRING NodeId
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),             // Bajo ObjectsFolder
            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),                 // Relación
            UA_QUALIFIEDNAME(1, const_cast<char *>(tagName.c_str())), // Nombre calificado
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),            // Tipo base
            oAttr,
            nullptr,
            &tagNodeId // 🔧 IMPORTANTE: Capturar NodeId del TAG
        );

        if (result != UA_STATUSCODE_GOOD)
        {
            LOG_ERROR("❌ Error creando TAG: " << tagName << " - " << UA_StatusCode_name(result));
            continue;
        }

        LOG_DEBUG("✅ TAG creado: " << tagName);

        // 🔧 CREAR VARIABLES BAJO EL TAG (COMO HIJOS)
        int created_vars = 0;
        for (auto var : variables)
        {
            try
            {
                // 🔧 CONFIGURAR ATRIBUTOS DE VARIABLE
                UA_VariableAttributes vAttr = UA_VariableAttributes_default;

                // 🔧 DISPLAY NAME: Solo el nombre de la variable (ej: "PV", no "TT_11001.PV")
                vAttr.displayName = UA_LOCALIZEDTEXT("en", const_cast<char *>(var->var_name.c_str()));

                // 🔧 CONFIGURAR ACCESO
                if (var->writable)
                {
                    vAttr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
                    vAttr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
                }
                else
                {
                    vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
                    vAttr.userAccessLevel = UA_ACCESSLEVELMASK_READ;
                }

                // 🔧 CONFIGURAR TIPO DE DATO Y VALOR INICIAL ESCALAR
                UA_Variant value;
                UA_Variant_init(&value);

                // 🔧 DETERMINAR TIPO CORRECTO SEGÚN VARIABLE - LÓGICA MEJORADA
                bool shouldBeInt32 = false;
                
                // 🔧 Detectar variables INT32:
                // 1. Variables que empiezan con ALARM_
                if (var->var_name.find("ALARM_") == 0) {
                    shouldBeInt32 = true;
                    LOG_DEBUG("  🎯 ALARM detectado: " << var->var_name);
                }
                // 2. Variable Color
                else if (var->var_name == "Color") {
                    shouldBeInt32 = true;
                    LOG_DEBUG("  🎯 Color detectado: " << var->var_name);
                }
                // 3. Variables simples I_xxx
                else if (var->tag_name == "SimpleVars" && var->var_name.find("I_") == 0) {
                    shouldBeInt32 = true;
                    LOG_DEBUG("  🎯 Variable simple I_ detectada: " << var->var_name);
                }
                // 4. Tags de alarma (TA_, PA_, LA_, DA_)
                else if (var->tag_name.find("TA_") == 0 || var->tag_name.find("PA_") == 0 || 
                         var->tag_name.find("LA_") == 0 || var->tag_name.find("DA_") == 0) {
                    shouldBeInt32 = true;
                    LOG_DEBUG("  🎯 Tag de alarma detectado: " << var->tag_name);
                }
                // 5. Forzar según tipo configurado
                else if (var->type == Variable::INT32) {
                    shouldBeInt32 = true;
                    LOG_DEBUG("  🎯 Tipo INT32 configurado: " << var->var_name);
                }
                
                if (shouldBeInt32)
                {
                    int32_t initial = 0;
                    UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_INT32]);
                    vAttr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
                    vAttr.valueRank = UA_VALUERANK_SCALAR;
                    vAttr.arrayDimensionsSize = 0;
                    vAttr.arrayDimensions = nullptr;
                    
                    // 🔧 FORZAR TIPO EN ESTRUCTURA TAMBIÉN
                    var->type = Variable::INT32;
                    
                    LOG_DEBUG("  🔢 Variable INT32: " << var->var_name << " (DataType: " << vAttr.dataType.identifier.numeric << ")");
                }
                else
                {
                    float initial = 0.0f;
                    UA_Variant_setScalar(&value, &initial, &UA_TYPES[UA_TYPES_FLOAT]);
                    vAttr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
                    vAttr.valueRank = UA_VALUERANK_SCALAR;
                    vAttr.arrayDimensionsSize = 0;
                    vAttr.arrayDimensions = nullptr;
                    
                    // 🔧 FORZAR TIPO EN ESTRUCTURA TAMBIÉN
                    var->type = Variable::FLOAT;
                    
                    LOG_DEBUG("  🔢 Variable FLOAT: " << var->var_name << " (DataType: " << vAttr.dataType.identifier.numeric << ")");
                }

                vAttr.value = value;

                // 🔧 CREAR VARIABLE BAJO EL TAG (COMO HIJO)
                UA_NodeId varNodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                result = UA_Server_addVariableNode(
                    server,
                    varNodeId,                                                      // NodeId de la variable
                    tagNodeId,                                                      // 🏗️ PADRE: TAG (no ObjectsFolder)
                    UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),                    // 🔧 Relación HasComponent
                    UA_QUALIFIEDNAME(1, const_cast<char *>(var->var_name.c_str())), // Nombre calificado (solo variable)
                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),            // Tipo base
                    vAttr,
                    nullptr,
                    nullptr);

                if (result == UA_STATUSCODE_GOOD)
                {
                    var->has_node = true;
                    created_vars++;

                    // 🔧 FORZAR VALOR POR DEFECTO INMEDIATAMENTE (SIN CALLBACKS)
                    UA_Variant initialValue;
                    UA_Variant_init(&initialValue);
                    
                    if (var->type == Variable::FLOAT) {
                        float defaultFloat = 0.0f;
                        UA_Variant_setScalar(&initialValue, &defaultFloat, &UA_TYPES[UA_TYPES_FLOAT]);
                    } else {
                        int32_t defaultInt = 0;
                        UA_Variant_setScalar(&initialValue, &defaultInt, &UA_TYPES[UA_TYPES_INT32]);
                    }
                    
                    // 🔧 ESCRIBIR VALOR POR DEFECTO (SIN DATASOURCE = SIN CALLBACKS)
                    UA_StatusCode writeResult = UA_Server_writeValue(server, varNodeId, initialValue);
                    if (writeResult == UA_STATUSCODE_GOOD) {
                        LOG_DEBUG("  🔧 Valor por defecto asignado a: " << var->var_name);
                    }

                    // 🔧 NO CONFIGURAR DATASOURCE AQUÍ - EVITAR CALLBACKS DURANTE INIT
                    LOG_DEBUG("  ✅ " << var->var_name << " (" << 
                             (var->writable ? "R/W" : "R") << ", " << 
                             (var->type == Variable::FLOAT ? "FLOAT" : "INT32") << 
                             ") - SIN callbacks");
                }
                else
                {
                    LOG_ERROR("  ❌ Error creando variable: " << var->opcua_name << " - " << UA_StatusCode_name(result));
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("❌ Excepción creando variable " << var->opcua_name << ": " << e.what());
            }
        }

        // 🔧 RESUMEN FINAL DE TIPOS ASIGNADOS
        int floatNodes = 0, int32Nodes = 0;
        for (const auto &var : config.variables) {
            if (var.has_node) {
                if (var.type == Variable::FLOAT) floatNodes++;
                else if (var.type == Variable::INT32) int32Nodes++;
            }
        }
        
        LOG_INFO("✅ Estructura jerárquica completada:");
        LOG_INFO("   📁 TAGs creados: " << tagVars.size());
        LOG_INFO("   📊 Variables totales: " << config.getTotalVariableCount());
        LOG_INFO("   📝 Variables escribibles: " << config.getWritableVariableCount());
        LOG_INFO("   🔢 Tipos asignados: " << floatNodes << " FLOAT, " << int32Nodes << " INT32");
    }

    LOG_INFO("✅ Estructura jerárquica completada:");
    LOG_INFO("   📁 TAGs creados: " << tagVars.size());
    LOG_INFO("   📊 Variables totales: " << config.getTotalVariableCount());
    LOG_INFO("   📝 Variables escribibles: " << config.getWritableVariableCount());
}

// ============== ACTUALIZACIÓN DE DATOS ==============

void updateData()
{
    static auto lastReconnect = chrono::steady_clock::now();
    static bool firstUpdate = true;  // 🔧 BANDERA PARA PRIMERA ACTUALIZACIÓN

    while (running && server_running)
    {
        // Solo log cuando inicia ciclo completo
        LOG_DEBUG("Iniciando ciclo de actualización PAC");

        // 🔒 VERIFICAR SI ES SEGURO HACER ACTUALIZACIONES
        if (updating_internally.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (pacClient && pacClient->isConnected())
        {
            LOG_DEBUG("🔄 Iniciando actualización de datos del PAC");

            // ⚠️ MARCAR ACTUALIZACIÓN EN PROGRESO
            updating_internally.store(true);

            // 🔒 ACTIVAR BANDERA DE ESCRITURA INTERNA DEL SERVIDOR (EVITAR CALLBACKS)
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
                    // Variable simple (F_xxx, I_xxx)
                    simpleVars.push_back(&var);
                }
            }

            // 📋 ACTUALIZAR VARIABLES SIMPLES PRIMERO
            if (!simpleVars.empty())
            {
                LOG_DEBUG("📋 Actualizando " << simpleVars.size() << " variables simples...");

                for (const auto &var : simpleVars)
                {
                    bool success = false;

                    // 🔧 USAR SIGNATURA CORRECTA: readFloatVariable(table_name, index)
                    if (var->type == Variable::FLOAT)
                    {
                        try
                        {
                            // Para variables simples F_xxx, usar índice 0
                            float newValue = pacClient->readFloatVariable(var->pac_source, 0);

                            // 🔧 USAR NODEID STRING (COHERENTE CON createNodes)
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));
                            UA_Variant value;
                            UA_Variant_init(&value);
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            success = (result == UA_STATUSCODE_GOOD);

                            if (success)
                            {
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            LOG_DEBUG("❌ Error leyendo FLOAT " << var->pac_source << ": " << e.what());
                        }
                    }
                    else if (var->type == Variable::INT32)
                    {
                        try
                        {
                            // Para variables simples I_xxx, usar índice 0
                            int32_t newValue = pacClient->readInt32Variable(var->pac_source, 0);

                            // 🔧 USAR NODEID STRING (COHERENTE CON createNodes)
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));
                            UA_Variant value;
                            UA_Variant_init(&value);
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);
                            success = (result == UA_STATUSCODE_GOOD);

                            if (success)
                            {
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            LOG_DEBUG("❌ Error leyendo INT32 " << var->pac_source << ": " << e.what());
                        }
                    }

                    if (!success)
                    {
                        LOG_DEBUG("❌ Error actualizando variable simple: " << var->opcua_name);
                    }
                }

                LOG_DEBUG("✅ Variables simples procesadas");
            }

            // 📊 ACTUALIZAR VARIABLES DE TABLA (código existente)
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

                if (indices.empty())
                {
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

                    if (values.empty())
                    {
                        LOG_DEBUG("❌ Error leyendo tabla INT32: " << tableName);
                        continue;
                    }

                        LOG_DEBUG("✅ Leída tabla INT32: " << tableName << " (" << values.size() << " valores)");

                    // Actualizar variables
                    int vars_updated = 0;
                    for (const auto &var : vars)
                    {
                        // 🔧 PROCESAR TODAS LAS VARIABLES, NO SOLO INT32
                        size_t pos = var->pac_source.find(':');
                        int index = stoi(var->pac_source.substr(pos + 1));
                        int arrayIndex = index - minIndex;

                        if (arrayIndex >= 0 && arrayIndex < (int)values.size())
                        {
                            // 🔧 USAR NODEID STRING
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            
                            // 🔧 RESPETAR TIPO CONFIGURADO DEL NODO
                            if (var->type == Variable::INT32) {
                                // Usar datos INT32 directamente
                                int32_t newValue = values[arrayIndex];
                                UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_INT32]);
                                
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue << " (INT32 desde alarma)");
                            } else {
                                // Convertir a FLOAT si la variable está configurada como FLOAT
                                float newValue = static_cast<float>(values[arrayIndex]);
                                UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);
                                
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue << " (FLOAT convertido desde alarma)");
                            }

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);

                            if (result == UA_STATUSCODE_GOOD)
                            {
                                vars_updated++;
                            }
                            else
                            {
                                LOG_ERROR("❌ Error actualizando " << var->opcua_name << ": " << UA_StatusCode_name(result));
                            }
                        }
                    }

                    LOG_DEBUG("✅ Tabla INT32 " << tableName << ": " << vars_updated << " variables actualizadas");
                }
                else
                {
                    // 📊 LEER TABLA DE VALORES (FLOAT)
                    vector<float> values = pacClient->readFloatTable(tableName, minIndex, maxIndex);

                    if (values.empty())
                    {
                        LOG_DEBUG("❌ Error leyendo tabla FLOAT: " << tableName);
                        
                        // 🔧 MANTENER VALORES POR DEFECTO PARA VARIABLES SIN DATOS
                        LOG_DEBUG("🔧 Manteniendo valores por defecto para tabla: " << tableName);
                        
                        // Asignar valores por defecto para mantener tipos
                        for (const auto &var : vars) {
                            if (var->type != Variable::FLOAT) continue;
                            
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));
                            UA_Variant defaultValue;
                            UA_Variant_init(&defaultValue);
                            float defaultFloat = 0.0f;
                            UA_Variant_setScalar(&defaultValue, &defaultFloat, &UA_TYPES[UA_TYPES_FLOAT]);
                            
                            UA_Server_writeValue(server, nodeId, defaultValue);
                            LOG_DEBUG("🔧 Valor por defecto asignado a: " << var->opcua_name);
                        }
                        
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
                            // 🔧 USAR NODEID STRING (COMO EN LA VERSIÓN ORIGINAL)
                            UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var->opcua_name.c_str()));

                            UA_Variant value;
                            UA_Variant_init(&value);
                            float newValue = values[arrayIndex];
                            UA_Variant_setScalar(&value, &newValue, &UA_TYPES[UA_TYPES_FLOAT]);

                            UA_StatusCode result = UA_Server_writeValue(server, nodeId, value);

                            if (result == UA_STATUSCODE_GOOD)
                            {
                                vars_updated++;
                                LOG_DEBUG("📝 " << var->opcua_name << " = " << newValue);
                            }
                            else
                            {
                                LOG_DEBUG("❌ Error actualizando " << var->opcua_name << ": " << UA_StatusCode_name(result));
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

            // 🔧 ACTIVAR CALLBACKS SOLO DESPUÉS DE LA PRIMERA ACTUALIZACIÓN COMPLETA
            if (firstUpdate) {
                LOG_INFO("🎯 Primera actualización completada - activando callbacks de escritura");
                enableWriteCallbacks();
                firstUpdate = false;
            }

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
        UA_LOCALIZEDTEXT(const_cast<char *>("en"), const_cast<char *>(config.server_name.c_str()));

    LOG_INFO("📡 Servidor configurado en puerto " << config.opcua_port);

    // Crear nodos
    createNodes();
    
    // 🔧 ELIMINAR ESTA LÍNEA - NO NECESITAMOS verifyAndFixNodeTypes
    // verifyAndFixNodeTypes();

    // Inicializar cliente PAC
    pacClient = std::make_unique<PACControlClient>(config.pac_ip, config.pac_port);

    LOG_INFO("✅ Servidor OPC-UA inicializado correctamente");
    return true;
}

UA_StatusCode runServer()
{
    if (!server)
    {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    // Iniciar hilo de actualización
    std::thread updateThread(updateData);

    // Ejecutar servidor
    UA_StatusCode retval = UA_Server_run(server, &server_running_flag);

    // Detener actualización
    running.store(false);
    if (updateThread.joinable())
    {
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

    if (pacClient)
    {
        pacClient.reset();
    }

    if (server)
    {
        UA_Server_delete(server);
        server = nullptr;
    }
}

bool getPACConnectionStatus()
{
    return pacClient && pacClient->isConnected();
}

void cleanupServer()
{
    shutdownServer();
}

void cleanupAndExit()
{
    cleanupServer();
}

// ============== FUNCIONES ADICIONALES ==============

void verifyAndFixNodeTypes()
{
    LOG_INFO("🔧 Verificando y corrigiendo tipos de nodos...");
    
    int fixedNodes = 0;
    int totalNodes = 0;
    
    for (auto &var : config.variables) {
        if (!var.has_node) continue;
        
        totalNodes++;
        UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var.opcua_name.c_str()));
        
        // 🔧 APLICAR TIPOS SEGÚN ESPECIFICACIÓN CORRECTA
        UA_Variant defaultValue;
        UA_Variant_init(&defaultValue);
        
        // 🔧 VERIFICAR Y CORREGIR TIPOS SEGÚN REGLAS:
        // TT_, PT_, LT_ → FLOAT
        // TA_, PA_, LA_ → INT32  
        // API_ → FLOAT
        // BATCH_ → FLOAT
        // F_xxx → FLOAT (variables simples)
        // I_xxx → INT32 (variables simples)
        // ALARM_ → INT32
        // Color → INT32
        
        bool shouldBeFloat = false;
        bool shouldBeInt32 = false;
        
        if (var.tag_name.find("TT_") == 0 || 
            var.tag_name.find("PT_") == 0 || 
            var.tag_name.find("LT_") == 0 ||
            var.tag_name.find("API_") == 0 ||
            var.tag_name.find("BATCH_") == 0 ||
            (var.tag_name == "SimpleVars" && var.var_name.find("F_") == 0)) {  // 🔧 F_xxx
            shouldBeFloat = true;
        } else if (var.tag_name.find("TA_") == 0 ||
                   var.tag_name.find("PA_") == 0 ||
                   var.tag_name.find("LA_") == 0 ||
                   var.var_name.find("ALARM_") == 0 ||
                   var.var_name == "Color" ||
                   (var.tag_name == "SimpleVars" && var.var_name.find("I_") == 0)) {  // 🔧 I_xxx
            shouldBeInt32 = true;
        } else {
            // Por defecto, variables de proceso son FLOAT
            shouldBeFloat = true;
        }
        
        // Corregir tipo si es necesario
        if (shouldBeFloat && var.type != Variable::FLOAT) {
            LOG_DEBUG("🔧 Corrigiendo " << var.opcua_name << " de INT32 a FLOAT");
            var.type = Variable::FLOAT;
        } else if (shouldBeInt32 && var.type != Variable::INT32) {
            LOG_DEBUG("🔧 Corrigiendo " << var.opcua_name << " de FLOAT a INT32");
            var.type = Variable::INT32;
        }
        
        // Asignar valor por defecto según tipo correcto
        if (var.type == Variable::FLOAT) {
            float defaultFloat = 0.0f;
            UA_Variant_setScalar(&defaultValue, &defaultFloat, &UA_TYPES[UA_TYPES_FLOAT]);
        } else {
            int32_t defaultInt = 0;
            UA_Variant_setScalar(&defaultValue, &defaultInt, &UA_TYPES[UA_TYPES_INT32]);
        }
        
        // 🔧 ESCRIBIR VALOR POR DEFECTO PARA FORZAR TIPO
        UA_StatusCode writeResult = UA_Server_writeValue(server, nodeId, defaultValue);
        if (writeResult == UA_STATUSCODE_GOOD) {
            fixedNodes++;
            
            LOG_DEBUG("  ✅ " << var.opcua_name << " → " << 
                     (var.type == Variable::FLOAT ? "FLOAT" : "INT32") << 
                     " (valor: " << (var.type == Variable::FLOAT ? "0.0" : "0") << ")");
        } else {
            LOG_ERROR("  ❌ Error asignando valor por defecto a: " << var.opcua_name << " - " << UA_StatusCode_name(writeResult));
        }
    }
    
    LOG_INFO("🔧 Verificación completada:");
    LOG_INFO("   📊 Nodos procesados: " << totalNodes);
    LOG_INFO("   ✅ Nodos corregidos: " << fixedNodes);
    
    if (fixedNodes < totalNodes) {
        //LOG_WARN("⚠️ " << (totalNodes - fixedNodes) << " nodos podrían seguir con problemas de tipo");
    }
}

void enableWriteCallbacks()
{
    LOG_INFO("📝 Activando callbacks de escritura para variables escribibles...");
    
    int callbacksEnabled = 0;
    
    for (auto &var : config.variables) {
        if (!var.has_node || !var.writable) continue;
        
        UA_NodeId nodeId = UA_NODEID_STRING(1, const_cast<char *>(var.opcua_name.c_str()));
        
        // 🔧 CONFIGURAR DATASOURCE SOLO PARA VARIABLES ESCRIBIBLES
        UA_DataSource dataSource;
        dataSource.read = readCallback;
        dataSource.write = writeCallback;
        
        UA_StatusCode dsResult = UA_Server_setVariableNode_dataSource(server, nodeId, dataSource);
        if (dsResult == UA_STATUSCODE_GOOD) {
            callbacksEnabled++;
            LOG_DEBUG("  📝 Callback habilitado para: " << var.opcua_name);
        } else {
            LOG_ERROR("  ❌ Error configurando callback para: " << var.opcua_name);
        }
    }
    
    LOG_INFO("📝 Callbacks activados: " << callbacksEnabled << " variables escribibles");
}
