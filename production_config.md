# Configuración de Producción - PAC to OPC-UA Server

## 🚀 Arquitectura Simplificada para Producción

### Estado Actual
- ✅ **Versión de Producción**: `build/opcua_server_production`
- ✅ **Conexión PAC**: 192.168.1.30:22001 (funcional)
- ✅ **Datos Reales**: Transmisores TT industriales
- ✅ **OPC-UA Server**: Puerto 4840

### Comandos de Producción

#### Compilar Versión de Producción
```bash
make production
```

#### Ejecutar en Producción
```bash
# Ejecución normal
./build/opcua_server_production

# Ejecución en background
nohup ./build/opcua_server_production > logs/opcua_server.log 2>&1 &
```

#### Comandos de Control
```bash
# Ver ayuda del sistema
make help

# Limpiar builds
make clean

# Verificar status
ps aux | grep opcua_server_production
```

### Configuración del Sistema

#### Archivo de Configuración Principal
- **Ubicación**: `tags.json`
- **Contenido**: 30 tags industriales (TT, PT, LT, DT)
- **Estructura**: Variables individuales + configuración PAC

#### Variables Monitoreadas
- **TT_11002** a **TT_11006**: Transmisores de Temperatura
- **PT_11007** a **PT_11011**: Transmisores de Presión  
- **LT_11012** a **LT_11021**: Transmisores de Nivel
- **DT_11022** a **DT_11031**: Transmisores de Densidad

### Arquitectura de Red

#### Servidor OPC-UA
- **Puerto**: 4840
- **Protocolo**: opc.tcp://localhost:4840
- **Namespace**: http://petrosantander.com/opcua

#### Cliente PAC
- **Dirección**: 192.168.1.30
- **Puerto**: 22001
- **Protocolo**: TCP/Socket personalizado

### Ventajas de la Versión Simplificada

1. **Menos Complejidad**: Código limpio y mantenible
2. **Mejor Rendimiento**: Sin capas innecesarias
3. **Más Estable**: Menos puntos de fallo
4. **Fácil Depuración**: Logs claros y concisos
5. **Escalable**: Estructura modular

### Monitoreo y Logs

#### Ubicación de Logs
```bash
# Crear directorio de logs
mkdir -p logs

# Logs con timestamp
./build/opcua_server_production 2>&1 | tee logs/opcua_$(date +%Y%m%d_%H%M%S).log
```

#### Verificación de Conexión PAC
```bash
# Test de conectividad
telnet 192.168.1.30 22001

# Verificar datos en tiempo real
timeout 10 ./build/opcua_server_production | grep "TT_11006"
```

### Implementación en Producción

#### Script de Inicio Automático
```bash
#!/bin/bash
# /etc/systemd/system/opcua-server.service

[Unit]
Description=OPC-UA Server for PAC Integration
After=network.target

[Service]
Type=simple
User=scada
WorkingDirectory=/path/to/pac_to_opcua
ExecStart=/path/to/pac_to_opcua/build/opcua_server_production
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

#### Activación del Servicio
```bash
sudo systemctl enable opcua-server
sudo systemctl start opcua-server
sudo systemctl status opcua-server
```

### Respaldo y Mantenimiento

#### Archivos Críticos para Respaldar
- `tags.json` - Configuración de variables
- `src/` - Código fuente (main.cpp, opcua_server.cpp, pac_control_client.cpp)
- `include/` - Headers (opcua_server.h, pac_control_client.h, common.h)
- `Makefile` - Sistema de build simplificado
- `production.sh` - Script de gestión de producción

#### Comandos de Mantenimiento
```bash
# Backup completo
tar -czf backup_opcua_$(date +%Y%m%d).tar.gz src/ include/ tags.json Makefile

# Rebuild completo
make clean && make production

# Test de funcionamiento
timeout 5 ./build/opcua_server_production | grep "✓ Tabla"
```

### Recomendaciones de Producción

1. **Monitoreo Continuo**: Implementar watchdog
2. **Logs Rotativos**: Configurar logrotate
3. **Alertas**: Notificaciones por desconexión PAC
4. **Backup Automático**: Cron job diario
5. **Documentación**: Mantener este archivo actualizado

### Contacto y Soporte
- **Desarrollador**: Sistema integrado y optimizado
- **Fecha**: Agosto 2025
- **Versión**: Producción simplificada v1.0
