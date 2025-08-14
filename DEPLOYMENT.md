# DESPLIEGUE EN PRODUCCIÓN
========================

## Archivos Necesarios

### Ejecutables
- `build/opcua_telemetria_server` - Servidor principal optimizado
- `start_production.sh` - Script de inicio

### Configuración
- `pac_config.json` - Configuración del servidor y PAC
- `tags.json` - Definición de tags y variables

## Configuración de Red

### PAC Control
- **Puerto:** 22001
- **Protocolo:** TCP/IP
- **IP:** Configurar en pac_config.json

### OPC-UA Server
- **Puerto:** 4840
- **Protocolo:** OPC-UA
- **Acceso:** Cliente OPC-UA

## Mejoras Implementadas

### ✅ Robustez de Comunicación
1. **Detección automática de desconexión**
2. **Reconexión automática** cada 5 segundos
3. **Validación de integridad** de datos
4. **Retry automático** si se detecta contaminación

### ✅ Prevención de Interferencias
1. **Limpieza de buffers** antes de cada lectura
2. **Delays entre tablas** (50ms) para evitar timing issues
3. **Detección de contaminación** automática
4. **Aislamiento perfecto** entre tablas float e int32

### ✅ Monitoreo y Diagnóstico
1. **Logs de estado** de conexión
2. **Detección de cambios** en variables
3. **Validación continua** de datos
4. **Métricas de estabilidad**

## Inicio del Sistema

```bash
# Modo recomendado para producción
./start_production.sh

# Modo manual
./build/opcua_telemetria_server
```

## Verificación

### Conexión PAC
- El sistema detecta automáticamente desconexiones
- Reconexión automática sin intervención manual
- Logs claros del estado de conectividad

### Variables OPC-UA
- 315 variables disponibles
- 21 TAGs principales (DT, PT, TT, LT)
- Actualización cada 1000ms
- Detección automática de cambios

### Estabilidad
- Sistema probado con lecturas consecutivas
- 100% de éxito en tests de interferencia
- Validación automática de integridad

## Monitoreo

El sistema proporciona logs de:
- Estado de conexión PAC
- Cambios en variables
- Errores de comunicación
- Métricas de performance

## Solución de Problemas

### PAC No Responde
- ✅ Reconexión automática activa
- ✅ Logs claros del estado
- ✅ No requiere reinicio manual

### Datos Inconsistentes
- ✅ Validación automática implementada
- ✅ Retry automático en caso de contaminación
- ✅ Aislamiento entre tablas garantizado

### Performance
- ✅ Optimizado para producción
- ✅ Sin logs de debug verbose
- ✅ Delays mínimos necesarios (50ms entre tablas)
