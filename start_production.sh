#!/bin/bash

# Script de inicio para producción
# =================================

echo "🚀 INICIANDO OPC-UA SERVER - MODO PRODUCCIÓN"
echo "============================================="
echo "📅 Fecha: $(date)"
echo "💼 Sistema: PetroSantander SCADA"
echo "🔧 Modo: Producción (sin logs de debug)"
echo ""

# Verificar que el PAC esté disponible
echo "🔍 Verificando conectividad PAC..."
# IP del PAC (ajustar según configuración)
PAC_IP="192.168.1.100"
PAC_PORT="22001"

# Test de conectividad básico
timeout 5 bash -c "</dev/tcp/$PAC_IP/$PAC_PORT" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "✅ PAC disponible en $PAC_IP:$PAC_PORT"
else
    echo "⚠️  PAC no responde en $PAC_IP:$PAC_PORT"
    echo "   El servidor iniciará de todas formas (reconexión automática)"
fi

echo ""
echo "🚀 Iniciando servidor OPC-UA..."
echo "   Puerto OPC-UA: 4840"
echo "   Intervalo actualización: 1000ms"
echo "   Variables: 315 (21 TAGs)"
echo ""

# Ejecutar servidor
./build/opcua_telemetria_server

