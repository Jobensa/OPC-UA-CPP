# Servidor OPC UA con Protocolo PAC Control para PetroSantander SCADA

Este proyecto implementa un servidor OPC UA que se comunica directamente con dispositivos PAC (Process Automation Controller) de Opto 22 usando el protocolo nativo PAC Control, proporcionando acceso completo a las tablas internas del controlador.

## Características

- **Protocolo PAC Control nativo**: Comunicación directa TCP puerto 22001
- **Acceso completo a tablas PAC**: Lectura/escritura de variables tipo TBL_PT_*
- **Servidor OPC UA integrado**: Compatible con clientes OPC UA estándar
- **Alta confiabilidad**: Protocolo binario nativo sin dependencias externas
- **Mapeo configurable**: Tags mapeados a variables PAC específicas
- **Soporte para múltiples tipos de datos**: Float (IEEE 754), Int32, Boolean
- **Caché optimizado**: Sistema de caché para mejorar rendimiento
- **Thread-safe**: Comunicación segura concurrente

## Protocolo PAC Control Descifrado

El protocolo usa comandos formato texto "RANGO TABLA OPERACIÓN":
- Formato: `"9 0 TBL_PT_11001 TRange.\r"`
- Puerto: 22001 TCP
- Respuesta: Binario IEEE 754 little endian
- Documentación completa en `PROTOCOLO_PAC_CONTROL_DESCIFRADO.md`

## Estructura del Proyecto

```
├── src/
│   ├── main_integrated.cpp         # Aplicación principal integrada
│   ├── opcua_server_integrated.cpp # Servidor OPC UA con integración PAC
│   └── pac_control_client.cpp      # Cliente para comunicación PAC Control
├── include/
│   ├── common.h                    # Definiciones comunes
│   └── pac_control_client.h        # Interface PAC Control
├── build/                          # Archivos compilados
├── pac_config.json                 # Configuración de TAGs PAC (21 TAGs reales)
├── Makefile                        # Sistema de compilación
└── README.md                       # Esta documentación
```

## Configuración

### Archivo de Configuración Principal

El archivo `pac_config.json` contiene la configuración de todos los TAGs del PAC S1. Está preconfigurado con 21 TAGs reales del sistema:

```json
{
    "pac_ip": "192.168.1.10",
    "pac_port": 22001,
    "tags": [
        {
            "name": "DT_0001",
            "value_table": "TBL_DT_0001",
            "alarm_table": "TBL_DA_0001",
            "variables": [
                "Engineering Unit", "Tag-Desc", "Present-Value",
                "Effective-Hi-Limit", "Effective-Lo-Limit",
                "Hi-Hi-Limit", "Hi-Limit", "Lo-Limit", 
                "Lo-Lo-Limit", "Initial-Value"
            ],
            "alarms": [
                "Hi-Hi-Alarm", "Hi-Alarm", "Lo-Alarm",
                "Lo-Lo-Alarm", "Comm-Alarm"
            ]
        }
        // ... 20 TAGs adicionales (DT_, LT_, PT_, TT_)
    ]
}
```

> **Nota**: El archivo está configurado con todos los TAGs reales del PAC S1. 
> No es necesario editarlo a menos que se agregue    Comando float: <valor> <index> }<tabla> TABLE!\r
    Ejemplo: 123.45 2 }TBL_PT_11001 TABLE!\r
    Comando int32: <valor> <index> }<tabla> TABLE!\rn nuevos TAGs al sistema.

## Dependencias

### Librerías del sistema (Ubuntu/Debian):
```bash
sudo apt-get update
sudo apt-get install -y build-essential libopen62541-dev libopen62541-1
```

## Compilación

```bash
# Compilar proyecto 
make

# Compilar con información de debug
make debug

# Compilar versión optimizada
make release

# Limpiar archivos objeto
make clean

# Recompilar desde cero
make rebuild

# Compilar y ejecutar
make run
```

## Configuración

### 1. Configuración del PAC

Editar `pac_config.json`:

```json
{
    "pac_config": {
        "ip": "192.168.1.100",     // IP del dispositivo PAC Opto 22
        "port": 22001,             // Puerto PAC Control (SIEMPRE 22001)
        "timeout_ms": 5000         // Timeout de conexión
    }
}
```

### 2. Configuración de Tags

El archivo `pac_config.json` contiene la configuración completa de 21 tags del PAC S1:

- **3 DT_** (Transmisores diferenciales)
- **6 LT_** (Transmisores de nivel) 
- **6 PT_** (Transmisores de presión)
- **6 TT_** (Transmisores de temperatura)

Cada tag incluye:
- **Tabla de valores** (10 variables flotantes)
- **Tabla de alarmas** (5 variables int32)
- **Mapeo automático** a nodos OPC UA
        }
    ]
## Protocolo PAC Control

### Formato de Variables PAC:
- **TBL_DT_xxxxx**: Variables de datos de temperatura (Data Temperature)
- **TBL_LT_xxxxx**: Variables de nivel de líquido (Liquid Tank)  
- **TBL_PT_xxxxx**: Variables de presión (Pressure Tags)
- **TBL_TT_xxxxx**: Variables de transmisores de temperatura (Temperature Transmitters)

### Tablas de Alarmas:
- **TBL_DA_xxxxx**: Alarmas de datos (Data Alarms)
- **TBL_LA_xxxxx**: Alarmas de nivel (Level Alarms)
- **TBL_PA_xxxxx**: Alarmas de presión (Pressure Alarms)  
- **TBL_TA_xxxxx**: Alarmas de temperatura (Temperature Alarms)

### Comandos Protocolo PAC Control:
- **Lectura valores**: `"9 0 }TBL_DT_0001 TRange.\r"`
- **Lectura alarmas**: `"4 0 }TBL_DA_0001 TRange.\r"`
- **Puerto**: 22001 TCP
- **Respuesta**: Binario little endian (40 bytes valores, 20 bytes alarmas)

## Uso

### Inicio del Servidor

```bash
./build/opcua_telemetria_server
```

El servidor iniciará:
1. Carga automática de configuración desde `pac_config.json` (21 TAGs)
2. Conexión al dispositivo PAC S1 (puerto 22001)
3. Servidor OPC UA en puerto 4840
4. Mapeo dinámico de 315 variables OPC UA (21 TAGs × 15 variables c/u)
5. Lectura cíclica de variables y alarmas PAC
6. Actualización automática de nodos OPC UA

### Conexión de Clientes OPC UA

- **Endpoint**: `opc.tcp://localhost:4840`
- **Namespace**: 1
- **Nodos**: 315 variables PAC mapeadas automáticamente
- **Estructura**: Cada TAG expone 10 variables + 5 alarmas

### Variables Mapeadas

Cada uno de los 21 TAGs expone:
- **10 Variables de datos**: Engineering Unit, Tag-Desc, Present-Value, etc.
- **5 Estados de alarma**: Hi-Hi-Alarm, Hi-Alarm, Lo-Alarm, Lo-Lo-Alarm, Comm-Alarm
- **Actualización automática**: Valores leídos directamente del PAC S1
- **Mapeo dinámico**: Sin necesidad de codificación manual

## Troubleshooting

### Error de Conexión PAC Control
```
Error al conectar con PAC: Connection refused
```
- Verificar IP y puerto 22001 del PAC
- Verificar conectividad de red
- Verificar que el PAC esté ejecutándose

### Error de Variable PAC
```
Error leyendo TBL_PT_11001: Variable no encontrada
```
- Verificar que la variable existe en el PAC
- Revisar sintaxis del nombre de variable
- Verificar permisos de acceso en el PAC

### Error de Compilación
```
fatal error: open62541/server.h: No such file
```
- Instalar dependencias: `sudo apt-get install libopen62541-dev`
- Verificar instalación de open62541

### Variables OPC UA no se actualizan
- Verificar conexión con PAC en puerto 22001
- Revisar logs de consola para errores de protocolo
- Verificar nombres de variables PAC en configuración

## Desarrollo

### Estado Actual del Proyecto
- ✅ **Protocolo PAC Control**: Completamente implementado y funcional
- ✅ **21 TAGs configurados**: Todos los TAGs reales del PAC S1 mapeados
- ✅ **315 Variables OPC UA**: Mapeo dinámico automático
- ✅ **Lectura de alarmas**: Todas las tablas de alarmas funcionales
- ✅ **Debugging completo**: Sistema de logs comprehensive implementado

### Agregar Nuevos TAGs
1. Editar `pac_config.json` agregando nuevo TAG
2. Seguir formato existente: value_table, alarm_table, variables, alarms
3. Reiniciar servidor para aplicar cambios
4. Verificar en logs que el TAG se carga correctamente

### Modificar Configuración PAC
- **IP del PAC**: Editar `pac_ip` en `pac_config.json`
- **Puerto**: Editar `pac_port` (por defecto 22001)
- **Variables por TAG**: Modificar array `variables` (máx. 10)
- **Alarmas por TAG**: Modificar array `alarms` (máx. 5)

### Debugging y Logs
El sistema incluye logging comprehensive:
- Información de conexión PAC
- Status de cada lectura de tabla
- Errores de protocolo detallados
- Estado de mapeo OPC UA
- Performance de comunicaciones

### Arquitectura del Sistema
- **Carga dinámica**: Configuración leída al inicio desde JSON
- **Mapeo automático**: Variables OPC UA creadas dinámicamente
- **Protocolo robusto**: Manejo de errores y reconexión automática
- **Performance optimizada**: Lecturas paralelas de tablas PAC

## Notas Técnicas

### Protocolo PAC Control Reverse-Engineered
- Formato de comando descifrado completamente
- Protocolo binario little endian confirmado  
- Manejo de errores de red implementado
- Compatible con PAC Control versión actual

### Lectura de Tablas (Binario)
    Comando: "9 0 }TBL_PT_11001 TRange.\r"
    Respuesta: Binario IEEE 754 little endian (ej. 40 bytes para 10 floats)
### Lectura de Variables Individuales (ASCII)
    Comando float: ^F_CPL_11001 @@ F.\r
    Comando int32: ^STATUS_BATCH @@ .\r
    Respuesta: ASCII terminado en espacio (0x20), ej: "1234.5 " o "1.234560e+05 "
### Escritura de Variables Individuales
    Comando float: <valor> ^<variable> @!\r
    Ejemplo: 123.45 ^F_CPL_11001 @!\r
    Comando int32: <valor> ^<variable> @!\r
    Ejemplo: 42 ^STATUS_BATCH @!\r
    Respuesta: ASCII (puede ser eco o confirmación, depende del PAC)
### Escritura en Tablas
    Comando float: <valor> <index> }<tabla> TABLE!\r
    Ejemplo: 123.45 2 }TBL_PT_11001 TABLE!\r
    Comando int32: <valor> <index> }<tabla> TABLE!\r
    Ejemplo: 42 3 }TBL_DA_0001 TABLE!\r
    Respuesta: ASCII (puede ser eco o confirmación)

### Limitaciones Conocidas
- Máximo 10 variables por TAG (limitación de protocolo PAC)
- Máximo 5 alarmas por TAG (limitación de protocolo PAC)
- Puerto PAC Control fijo en 22001 (estándar del sistema)

## Autor

Jose Salamanca - PetroSantander SCADA Project  
Protocolo PAC Control completamente reverse-engineered

## Licencia

Proyecto privado - PetroSantander
