#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <iomanip>
#include <sstream>

// 🔧 CONFIGURACIÓN SIMPLE DE LOGGING
#ifdef SILENT_MODE
    #define LOG_ENABLED 0
#elif defined(VERBOSE_DEBUG)
    #define LOG_ENABLED 2
#else
    #define LOG_ENABLED 1
#endif

// 🎨 COLORES PARA TERMINAL
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

// 🔧 MACROS DE LOGGING SIMPLIFICADAS
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

// 🔄 COMPATIBILIDAD CON DEBUG_INFO EXISTENTE
#define DEBUG_INFO(msg) LOG_INFO(msg)

#endif // COMMON_H