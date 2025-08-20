CXX = g++
CXXFLAGS = -Wall -std=c++17 -I./include
LDFLAGS = -lcurl -lcrypto -lpthread -L/usr/local/lib -lopen62541

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Target principal
TARGET = $(BUILD_DIR)/opcua_server

# Fuentes para la versión de PRODUCCIÓN (simplificada y optimizada)
PRODUCTION_SRCS = src/main.cpp src/opcua_server.cpp src/pac_control_client.cpp
PRODUCTION_OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/opcua_server.o $(BUILD_DIR)/pac_control_client.o

# Fuentes para la versión DEMO (con datos simulados)
DEMO_SRCS = src/main.cpp src/opcua_server.cpp src/pac_control_client_stub.cpp  
DEMO_OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/opcua_server.o $(BUILD_DIR)/pac_control_client_stub.o

# Regla por defecto: versión de producción
all: production

# Versión PRODUCCIÓN (recomendada) - arquitectura simplificada con PAC real
production: $(BUILD_DIR)/opcua_server_production

$(BUILD_DIR)/opcua_server_production: $(PRODUCTION_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✅ Versión PRODUCCIÓN compilada: $@"
	@echo "🚀 Arquitectura simplificada y optimizada para producción"

# Versión DEMO (para pruebas) - datos simulados
demo: $(BUILD_DIR)/opcua_server_demo

$(BUILD_DIR)/opcua_server_demo: $(DEMO_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✅ Versión DEMO compilada: $@"
	@echo "📝 Usa datos simulados para demostración"

# Reglas de compilación
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)
	@echo "🧹 Build directory cleaned"

rebuild: clean production

# Ejecutar versión de PRODUCCIÓN
run: production
	@echo "🚀 Ejecutando versión PRODUCCIÓN..."
	@echo "📝 Arquitectura simplificada con cliente PAC real"
	./$(BUILD_DIR)/opcua_server_production

# Ejecutar versión DEMO
run-demo: demo
	@echo "🚀 Ejecutando versión DEMO..."
	@echo "📝 Esta versión genera datos simulados para demostración"
	./$(BUILD_DIR)/opcua_server_demo

# Ejecutar versión original
run-original: original
	@echo "🚀 Ejecutando versión ORIGINAL..."
	@echo "📝 Arquitectura compleja - solo para referencia"
	./$(BUILD_DIR)/opcua_server_original_exe

# Aliases para compatibilidad hacia atrás
simple: production
	@echo "⚠️  NOTA: 'simple' ahora apunta a 'production'"
	@echo "� Usa 'make production' o 'make demo' para ser más específico"

# Ayuda
help:
	@echo "�️  OPCIONES DE COMPILACIÓN:"
	@echo ""
	@echo "  production     - Versión RECOMENDADA para producción (arquitectura simplificada)"
	@echo "  demo          - Versión de demostración con datos simulados" 
	@echo ""
	@echo "🚀 OPCIONES DE EJECUCIÓN:"
	@echo ""
	@echo "  run           - Ejecutar versión de producción"
	@echo "  run-demo      - Ejecutar versión demo"
	@echo ""
	@echo "🧹 UTILIDADES:"
	@echo ""
	@echo "  clean         - Limpiar archivos compilados"
	@echo "  rebuild       - Limpiar y recompilar producción"
	@echo "  help          - Mostrar esta ayuda"
	@echo ""
	@echo "💡 SCRIPT DE CONVENIENCIA:"
	@echo "  ./production.sh help - Script avanzado para gestión de producción"

.PHONY: all production demo clean rebuild run run-demo help