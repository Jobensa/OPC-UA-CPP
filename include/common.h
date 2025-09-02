#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>

// ============== ESTRUCTURAS UNIFICADAS ==============

// Variable unificada (combinando ambas definiciones)
struct Variable {
    // Nombres e identificadores
    std::string opcua_name;      // Nombre completo en OPC-UA (ej: "API_11001.IV")
    std::string tag_name;        // Nombre del TAG (ej: "API_11001")
    std::string var_name;        // Nombre de variable (ej: "IV")
    std::string pac_source;      // Fuente en PAC: tabla+índice (ej: "TBL_API_11001:0")
    
    // Propiedades
    enum Type { FLOAT, INT32, SINGLE_FLOAT, SINGLE_INT32 } type;
    bool writable = false;       // Si se puede escribir
    bool has_node = false;       // Si ya se creó el nodo OPC-UA
    
    // 🆕 Campos específicos para API tags
    std::string api_group;       // "API_11001" (para agrupar)
    std::string variable_name;   // "IV" (alias de var_name)
    int table_index = -1;        // Índice en la tabla (0, 1, 2, 3)
};

// Configuración de TAG tradicional (TBL_tags)
struct Tag {
    std::string name;            // "TT_11001"
    std::string value_table;     // "TBL_TT_11001"
    std::string alarm_table;     // "TBL_TA_11001"
    std::vector<std::string> variables;  // ["PV", "SV", "HH", "LL"]
    std::vector<std::string> alarms;     // ["HI", "LO", "BAD"]
};

// 🆕 Configuración de API TAG (TBL_tags_api)  
struct APITag {
    std::string name;            // "API_11001"
    std::string value_table;     // "TBL_API_11001"     
    std::vector<std::string> variables;  // ["IV", "NSV", "CPL", "CTL"]
};

// 🆕 Configuración de BATCH TAG  
struct BatchTag {
    std::string name;            // "BATCH_B1"
    std::string value_table;     // "TBL_BATCH_B1"     
    std::vector<std::string> variables;  // ["Tiquete", "Cliente", etc.]
};

// Variable simple individual
struct SimpleVariable {
    std::string name;            // "TEMP_AMBIENT"
    std::string pac_source;      // "F_TEMP_AMBIENT" o "TBL_MISC:5"
    std::string type;            // "FLOAT" o "INT32"
    bool writable = false;
};

// Configuración completa unificada
struct Config {
    // Configuración de conexión
    std::string pac_ip = "192.168.1.30";
    int pac_port = 22001;
    int opcua_port = 4840;
    int update_interval_ms = 2000;
    std::string server_name = "PAC Control SCADA Server";
    
    // Estructuras de datos
    std::vector<Tag> tags;                    // TBL_tags tradicionales
    std::vector<APITag> api_tags;            // 🆕 TBL_tags_api  
    std::vector<BatchTag> batch_tags;        // 🆕 BATCH_tags
    std::vector<SimpleVariable> simple_variables;  // Variables individuales
    std::vector<Variable> variables;         // Variables procesadas finales
};

// ============== VARIABLES GLOBALES ==============
extern Config config;
extern std::atomic<bool> running;
extern std::atomic<bool> server_running;

// ============== LOGGING SIMPLIFICADO ==============
#ifdef SILENT_MODE
    #define LOG_ENABLED 0
#elif defined(VERBOSE_DEBUG)
    #define LOG_ENABLED 2
#else
    #define LOG_ENABLED 1
#endif

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

#define LOG_ERROR(msg) \
    std::cout << COLOR_RED << "❌ [ERROR] " << COLOR_RESET << msg << std::endl;

#define LOG_INFO(msg) \
    if (LOG_ENABLED >= 1) { \
        std::cout << COLOR_CYAN << "ℹ️  [INFO]  " << COLOR_RESET << msg << std::endl; \
    }

#define LOG_DEBUG(msg) \
    if (LOG_ENABLED >= 2) { \
        std::cout << COLOR_BLUE << "🔧 [DEBUG] " << COLOR_RESET << msg << std::endl; \
    }

#define LOG_WRITE(msg) \
    if (LOG_ENABLED >= 1) { \
        std::cout << COLOR_GREEN << "📝 [WRITE] " << COLOR_RESET << msg << std::endl; \
    }

#define LOG_PAC(msg) \
    if (LOG_ENABLED >= 1) { \
        std::cout << COLOR_YELLOW << "🔌 [PAC]   " << COLOR_RESET << msg << std::endl; \
    }

// Compatibilidad con código existente
#define DEBUG_INFO(msg) LOG_INFO(msg)

#endif // COMMON_H