#!/bin/bash

# Script de Producción - PAC to OPC-UA Server
# Versión simplificada y optimizada

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}🚀 PAC to OPC-UA Server - Producción${NC}"
echo "=================================================="

case "$1" in
    "start")
        echo -e "${GREEN}✓ Iniciando servidor de producción...${NC}"
        make production
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✓ Compilación exitosa${NC}"
            echo -e "${BLUE}🔄 Iniciando servidor...${NC}"
            ./build/opcua_server_production
        else
            echo -e "${RED}❌ Error en compilación${NC}"
            exit 1
        fi
        ;;
    "test")
        echo -e "${YELLOW}🧪 Ejecutando test de 10 segundos...${NC}"
        make production
        timeout 10 ./build/opcua_server_production
        echo -e "${GREEN}✓ Test completado${NC}"
        ;;
    "background")
        echo -e "${GREEN}🌐 Iniciando en modo background...${NC}"
        make production
        mkdir -p logs
        nohup ./build/opcua_server_production > logs/opcua_$(date +%Y%m%d_%H%M%S).log 2>&1 &
        PID=$!
        echo -e "${GREEN}✓ Servidor iniciado con PID: $PID${NC}"
        echo "📁 Log: logs/opcua_$(date +%Y%m%d_%H%M%S).log"
        ;;
    "stop")
        echo -e "${YELLOW}🛑 Deteniendo servidor...${NC}"
        pkill -f opcua_server_production
        echo -e "${GREEN}✓ Servidor detenido${NC}"
        ;;
    "status")
        echo -e "${BLUE}📊 Estado del servidor:${NC}"
        ps aux | grep opcua_server_production | grep -v grep
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✓ Servidor ejecutándose${NC}"
        else
            echo -e "${RED}❌ Servidor no encontrado${NC}"
        fi
        ;;
    "logs")
        echo -e "${BLUE}📋 Últimos logs:${NC}"
        if [ -d "logs" ]; then
            tail -n 50 logs/opcua_*.log | tail -n 50
        else
            echo -e "${RED}❌ No hay logs disponibles${NC}"
        fi
        ;;
    "demo")
        echo -e "${YELLOW}🎭 Iniciando versión demo...${NC}"
        make demo
        if [ $? -eq 0 ]; then
            ./build/opcua_server_demo
        fi
        ;;
    "clean")
        echo -e "${YELLOW}🧹 Limpiando builds...${NC}"
        make clean
        echo -e "${GREEN}✓ Limpieza completada${NC}"
        ;;
    "help"|"")
        echo -e "${BLUE}📖 Comandos disponibles:${NC}"
        echo ""
        echo -e "  ${GREEN}start${NC}      - Compilar e iniciar servidor de producción"
        echo -e "  ${GREEN}test${NC}       - Ejecutar test de 10 segundos"
        echo -e "  ${GREEN}background${NC} - Iniciar en modo background"
        echo -e "  ${GREEN}stop${NC}       - Detener servidor"
        echo -e "  ${GREEN}status${NC}     - Ver estado del servidor"
        echo -e "  ${GREEN}logs${NC}       - Ver últimos logs"
        echo -e "  ${GREEN}demo${NC}       - Iniciar versión demo"
        echo -e "  ${GREEN}clean${NC}      - Limpiar builds"
        echo -e "  ${GREEN}help${NC}       - Mostrar esta ayuda"
        echo ""
        echo -e "${YELLOW}Ejemplos:${NC}"
        echo "  ./production.sh start"
        echo "  ./production.sh background"
        echo "  ./production.sh status"
        ;;
    *)
        echo -e "${RED}❌ Comando no reconocido: $1${NC}"
        echo -e "Usa: ${GREEN}./production.sh help${NC} para ver comandos disponibles"
        exit 1
        ;;
esac
