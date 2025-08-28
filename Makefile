CXX = g++
CXXFLAGS = -Wall -std=c++17 -I./include
LDFLAGS = -lcurl -lcrypto -lpthread -L/usr/local/lib -lopen62541

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Fuentes principales
PRODUCTION_SRCS = src/main.cpp src/opcua_server.cpp src/pac_control_client.cpp
PRODUCTION_OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/opcua_server.o $(BUILD_DIR)/pac_control_client.o

# Regla por defecto
all: production

# 🚀 TARGET PRODUCCIÓN - Version estable con logs importantes
production: CXXFLAGS += -O2 -DDEBUG=1
production: $(BUILD_DIR)/opcua_server_production

$(BUILD_DIR)/opcua_server_production: $(PRODUCTION_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✅ Versión PRODUCCIÓN compilada: $@"

# 🐛 TARGET DEBUG - Con información detallada
production-debug: CXXFLAGS += -g -O0 -DDEBUG=1 -DVERBOSE_DEBUG -ggdb3
production-debug: $(BUILD_DIR)/opcua_server_production_debug

$(BUILD_DIR)/opcua_server_production_debug: $(PRODUCTION_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "🐛 Versión DEBUG compilada: $@"

# 🔇 TARGET SILENCIOSO - Solo errores críticos
silent: CXXFLAGS += -O2 -DSILENT_MODE
silent: $(BUILD_DIR)/opcua_server_silent

$(BUILD_DIR)/opcua_server_silent: $(PRODUCTION_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "🔇 Versión SILENCIOSA compilada: $@"

# Reglas de compilación
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 🚀 TARGETS DE EJECUCIÓN
run: production
	@echo "🚀 Ejecutando versión PRODUCCIÓN (Ctrl+C para detener)..."
	./$(BUILD_DIR)/opcua_server_production

run-debug: production-debug
	@echo "🐛 Ejecutando versión DEBUG (Ctrl+C para detener)..."
	./$(BUILD_DIR)/opcua_server_production_debug

run-silent: silent
	@echo "🔇 Ejecutando versión SILENCIOSA (Ctrl+C para detener)..."
	./$(BUILD_DIR)/opcua_server_silent

# 🧹 LIMPIEZA
clean:
	rm -rf $(BUILD_DIR)
	@echo "🧹 Build directory cleaned"

rebuild: clean production
rebuild-debug: clean production-debug

# 📊 ESTADO Y AYUDA
status:
	@echo "📊 ESTADO DEL PROYECTO:"
	@ls -la $(BUILD_DIR)/ 2>/dev/null || echo "❌ No hay ejecutables compilados"
	@echo ""
	@echo "💡 Para compilar: make production"
	@echo "💡 Para ejecutar: make run"

help:
	@echo "🛠️  SERVIDOR OPC-UA PAC CONTROL"
	@echo ""
	@echo "📦 COMPILACIÓN:"
	@echo "  make production       - Versión normal con logs importantes"
	@echo "  make production-debug - Versión con debug detallado"
	@echo "  make silent          - Versión solo errores críticos"
	@echo ""
	@echo "🚀 EJECUCIÓN:"
	@echo "  make run             - Ejecutar versión normal"
	@echo "  make run-debug       - Ejecutar versión debug"
	@echo "  make run-silent      - Ejecutar versión silenciosa"
	@echo ""
	@echo "🧹 UTILIDADES:"
	@echo "  make clean           - Limpiar archivos compilados"
	@echo "  make rebuild         - Limpiar y recompilar"
	@echo "  make status          - Ver estado del proyecto"

.PHONY: all production production-debug silent run run-debug run-silent clean rebuild rebuild-debug status help