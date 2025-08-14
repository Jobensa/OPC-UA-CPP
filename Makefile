CXX = g++
CFLAGS=-std=c++17 -I/usr/local/include
LDFLAGS = -lcurl  -lcrypto -lpthread -L/usr/local/lib -lopen62541

SRC_DIR = src
INC_DIR = include

CXXFLAGS = -Wall -std=c++17 -I./include

# Opciones de debug - descomenta las que necesites
# CXXFLAGS += -DENABLE_DEBUG      # Debug completo
# CXXFLAGS += -DENABLE_VERBOSE    # Debug detallado de variables
# CXXFLAGS += -DENABLE_INFO       # Solo mensajes informativos (por defecto)

BUILD_DIR = build
TARGET = $(BUILD_DIR)/opcua_telemetria_server

# Buscar fuentes
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Reglas especiales para diferentes modos
debug: CXXFLAGS += -DENABLE_DEBUG -g
debug: $(TARGET)

verbose: CXXFLAGS += -DENABLE_VERBOSE -DENABLE_INFO
verbose: $(TARGET)

release: CXXFLAGS += -O2 -DNDEBUG
release: $(TARGET)

# Regla principal
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

run: all
	./$(TARGET)