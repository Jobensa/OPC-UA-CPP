# Servidor OPC UA con Protocolo PAC Control para PetroSantander SCADA

Este proyecto implementa un servidor OPC UA que se comunica directamente con dispositivos PAC (Process Automation Controller) de Opto 22 usando el protocolo nativo PAC Control, proporcionando acceso completo de **lectura y escritura** a las tablas internas y variables individuales del controlador.

## Características

- **Protocolo PAC Control nativo completo**: Comunicación directa TCP puerto 22001
- **Lectura y escritura completa**: Variables de tabla y variables individuales
- **Doble formato de respuesta**: ASCII para variables individuales, binario para tablas
- **Servidor OPC UA integrado**: Compatible con clientes OPC UA estándar
- **Alta confiabilidad**: Protocolo binario nativo sin dependencias externas
- **Mapeo configurable**: Tags mapeados a variables PAC específicas
- **Soporte para múltiples tipos**: Float (IEEE 754), Int32, variables individuales
- **Variables escribibles inteligentes**: Solo SET_xxx, E_xxx, SetHH/H/L/LL, SIM_Value
- **Sistema de debug avanzado**: Logs configurables con macros DEBUG_VERBOSE/DEBUG_INFO
- **Thread-safe**: Comunicación segura concurrente
- **Notación científica**: Soporte completo para valores grandes (ej: 1.234560e+05)

## Protocolo PAC Control Completamente Implementado

### Comandos de Lectura

#### Lectura de Tablas (Respuesta Binaria)
- **Formato**: `"<end_pos> 0 }<tabla> TRange.\r"`
- **Ejemplo**: `"9 0 }TBL_PT_11001 TRange.\r"`
- **Respuesta**: Binario IEEE 754 little endian (40 bytes para 10 floats)

#### Lectura de Variables Individuales (Respuesta ASCII)
- **Float**: `^<variable> @@ F.\r`
  - Ejemplo: `^F_CPL_11001 @@ F.\r`
- **Int32**: `^<variable> @@ .\r`
  - Ejemplo: `^STATUS_BATCH @@ .\r`
- **Respuesta**: ASCII terminado en espacio (0x20)
  - Ejemplos: `"1234.5 "`, `"1.234560e+05 "`, `"42 "`

### Comandos de Escritura

#### Escritura de Variables Individuales
- **Float**: `<valor> ^<variable> @!\r`
  - Ejemplo: `123.45 ^F_CPL_11001 @!\r`
- **Int32**: `<valor> ^<variable> @!\r`
  - Ejemplo: `42 ^STATUS_BATCH @!\r`

#### Escritura en Tablas
- **Float**: `<valor> <index> }<tabla> TABLE!\r`
  - Ejemplo: `123.45 2 }TBL_PT_11001 TABLE!\r`
- **Int32**: `<valor> <index> }<tabla> TABLE!\r`
  - Ejemplo: `42 3 }TBL_DA_0001 TABLE!\r`

### Protocolo de Comunicación
- **Puerto**: 22001 TCP (estándar PAC Control)
- **Terminadores**: 
  - Comando: `\r` (0x0D)
  - Respuesta ASCII: espacio (0x20)
- **Formato de datos**: 
  - Tablas: IEEE 754 little endian
  - Variables individuales: ASCII con soporte científico

## Estructura del Proyecto

```
├── src/
│   ├── main_integrated.cpp           # Aplicación principal integrada
│   ├── opcua_server_integrated.cpp   # Servidor OPC UA con lectura/escritura PAC
│   └── pac_control_client..cpp       # Cliente PAC Control completo (lectura/escritura)
├── include/
│   ├── common.h                      # Macros de debug configurables
│   ├── opcua_server_integrated.h     # Interface servidor OPC UA
│   └── pac_control_client.h          # Interface PAC Control completa
├── build/                            # Archivos compilados
├── pac_config.json                   # Configuración de TAGs PAC (21 TAGs reales)
├── Makefile                          # Sistema de compilación
└── README.md                         # Esta documentación
```

## Variables y Escritura OPC UA

### Variables Escribibles Automáticamente
El servidor OPC UA permite escritura **solo en variables apropiadas**:

#### Variables de Tabla Escribibles:
- `SetHH`, `SetH`, `SetL`, `SetLL` (setpoints de alarmas)
- `SIM_Value` (valor de simulación)
- `SET_xxx` (variables de configuración)
- `E_xxx` (variables de habilitación)

#### Variables de Tabla Solo Lectura:
- `Input`, `PV`, `Min`, `Max`, `Percent` (valores del proceso)
- `ALARM_HH`, `ALARM_H`, `ALARM_L`, `ALARM_LL`, `COLOR` (estados de alarma)

#### Variables Individuales:
- **Todas las variables float/int32 individuales son escribibles**

### Mapeo Inteligente de Escritura
```
TAG.SetHH     → TBL_TAG[1] = valor    ✅ ESCRIBIBLE
TAG.PV        → TBL_TAG[6] = valor    ❌ SOLO LECTURA
F_CPL_11001   → ^F_CPL_11001 @!\r     ✅ ESCRIBIBLE
STATUS_BATCH  → ^STATUS_BATCH @!\r    ✅ ESCRIBIBLE
```

## Dependencias

### Librerías del sistema (Ubuntu/Debian):
```bash
sudo apt-get update
sudo apt-get install -y build-essential libopen62541-dev libopen62541-1 nlohmann-json3-dev
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

## Configuración de Debug

### Control de Logs en `include/common.h`:

```cpp
// Configuración de debug
#define ENABLE_INFO      // ✅ Mensajes informativos básicos
// #define ENABLE_VERBOSE   // ❌ Debug detallado (comentar para producción)
```

#### Niveles de Debug:
- **Silencioso**: Comentar ambas macros (solo errores críticos)
- **Normal**: Solo `ENABLE_INFO` (info básica de operaciones)
- **Completo**: Ambas macros (debug detallado de protocolos)

## Configuración

### 1. Configuración del PAC

Editar `pac_config.json`:

```json
{
    "pac_config": {
        "ip": "192.168.1.30",         // IP del dispositivo PAC Opto 22
        "port": 22001,                // Puerto PAC Control (SIEMPRE 22001)
        "timeout_ms": 5000            // Timeout de conexión
    },
    "opcua_port": 4840,               // Puerto del servidor OPC UA
    "update_interval_ms": 2000        // Intervalo de actualización
}
```

### 2. Configuración de Tags

El archivo `pac_config.json` contiene:

- **Variables de tabla**: 21 tags completos del PAC S1
- **Variables individuales**: Float e Int32 independientes
- **Mapeo automático**: A nodos OPC UA con escritura inteligente

```json
{
    "tags": [
        {
            "name": "TT_11001",
            "value_table": "TBL_TT_11001",
            "alarm_table": "TBL_TA_11001",
            "variables": ["Input", "SetHH", "SetH", "SetL", "SetLL", 
                         "SIM_Value", "PV", "Min", "Max", "Percent"],
            "alarms": ["ALARM_HH", "ALARM_H", "ALARM_L", "ALARM_LL", "COLOR"]
        }
    ],
    "global_float_variables": [
        {"name": "F_Current_Flow", "pac_tag": "F_CPL_11001", "description": "Flujo actual"},
        {"name": "F_Total_Volume", "pac_tag": "F_TOTAL_VOL", "description": "Volumen total"}
    ],
    "global_int32_variables": [
        {"name": "Status_Batch", "pac_tag": "STATUS_BATCH", "description": "Estado del batch"},
        {"name": "Pump_Status", "pac_tag": "PUMP_CTRL", "description": "Estado de bomba"}
    ]
}
```

## Uso

### Inicio del Servidor

```bash
./build/opcua_telemetria_server
```

El servidor iniciará:
1. **Carga de configuración** desde `pac_config.json`
2. **Conexión PAC S1** (puerto 22001 con retry automático)
3. **Servidor OPC UA** en puerto 4840
4. **Mapeo dinámico** de variables con escritura inteligente
5. **Lectura cíclica** de tablas e individuales
6. **Escritura on-demand** desde clientes OPC UA

### Logs de Operación Normal

```
🚀 SERVIDOR OPC UA + CLIENTE PAC CONTROL INTEGRADO
✅ Conectado al PAC en 192.168.1.30:22001
🏗️ Creando nodos OPC UA con capacidad de escritura...
✓ Tabla TBL_TT_11001 leída: 10 valores
✅ Variable float individual leída: F_CPL_11001 = 1234.5
📝 Variable ESCRIBIBLE: TT_11001.SetHH
👁️ Variable SOLO LECTURA: TT_11001.PV
🌐 Servidor OPC UA iniciado en puerto 4840
```

### Conectar Cliente OPC UA

- **Endpoint**: `opc.tcp://localhost:4840`
- **Namespace**: 1
- **Estructura de nodos**:
  ```
  Objects/
  ├── TT_11001/           (TAG de temperatura)
  │   ├── Input           (solo lectura)
  │   ├── SetHH           (escribible)
  │   ├── SetH            (escribible)
  │   ├── PV              (solo lectura)
  │   └── ...
  ├── F_Current_Flow      (variable individual escribible)
  ├── Status_Batch        (variable individual escribible)
  └── ...
  ```

### Operaciones de Escritura

#### Desde Cliente OPC UA:
```
Escribir TT_11001.SetHH = 85.5
→ PAC: "85.5 1 }TBL_TT_11001 TABLE!\r"
→ Log: "✅ Escritura exitosa en PAC: TT_11001.SetHH"

Escribir F_Current_Flow = 123.45
→ PAC: "123.45 ^F_CPL_11001 @!\r"  
→ Log: "✅ Variable float individual leída: F_CPL_11001 = 123.45"
```

## Protocolo PAC Control - Detalles Técnicos

### Manejo de Formatos Numéricos

#### Notación Científica (Variables Individuales):
```
PAC Response: "1.234560e+05 "  → OPC UA: 123456.0
PAC Response: "1.23e-03 "      → OPC UA: 0.00123
PAC Response: "-2.34e-05 "     → OPC UA: -0.0000234
```

#### Valores Normales:
```
PAC Response: "1234.5 "        → OPC UA: 1234.5
PAC Response: "42 "            → OPC UA: 42
```

### Terminadores de Protocolo:
```
Comando → PAC:     "comando\r"           (0x0D)
PAC → Respuesta:   "valor "              (terminado en 0x20)
Tabla → PAC:       datos binarios        (IEEE 754 little endian)
```

## Troubleshooting

### Errores de Conexión PAC
```
❌ Error al conectar con PAC: Connection refused
```
**Solución**:
- Verificar IP del PAC en `pac_config.json`
- Verificar que puerto 22001 esté abierto
- Verificar conectividad de red: `ping 192.168.1.30`

### Errores de Protocolo
```
⚠️ TIMEOUT recibiendo respuesta ASCII después de 3000ms
```
**Solución**:
- Verificar sintaxis de comando PAC
- Revisar que la variable existe en el PAC
- Aumentar timeout en configuración

### Variables No Escribibles
```
🔒 Variable de tabla SOLO LECTURA: TT_11001.PV
```
**Comportamiento normal**: Solo variables SET_xxx, E_xxx, etc. son escribibles.

### Debug Detallado
Para debug completo, descomentar en `include/common.h`:
```cpp
#define ENABLE_VERBOSE   // ✅ Debug detallado activado
```

Logs resultantes:
```
📡 Byte recibido: 0x31 ('1')
📡 Byte recibido: 0x32 ('2')
🔍 ASCII RESPONSE: '1234.5'
✅ CONVERSIÓN EXITOSA: '1234.5' -> 1234.5
```

## Desarrollo y Extensiones

### Estado Actual
- ✅ **Protocolo PAC Control**: Completamente implementado (lectura/escritura)
- ✅ **Variables individuales**: Float/Int32 con notación científica
- ✅ **Escritura inteligente**: Solo variables apropiadas
- ✅ **21 TAGs configurados**: Mapeo completo del PAC S1
- ✅ **Debug configurable**: Sistema de logs avanzado
- ✅ **Manejo de errores**: Reconnección automática y validación

### Agregar Nuevas Variables

#### Variables Individuales:
```json
"global_float_variables": [
    {
        "name": "Nueva_Variable",
        "pac_tag": "NUEVA_VAR_PAC", 
        "description": "Descripción"
    }
]
```

#### TAGs de Tabla:
```json
"tags": [
    {
        "name": "NUEVO_TAG",
        "value_table": "TBL_NUEVO_TAG",
        "alarm_table": "TBL_ALARMA_TAG",
        "variables": ["Input", "SetHH", "SetH", "PV"],
        "alarms": ["ALARM_HH", "ALARM_H"]
    }
]
```

### Performance y Optimización

#### Configuración de Intervalos:
```json
{
    "update_interval_ms": 1000,    // Más frecuente = más carga
    "pac_config": {
        "timeout_ms": 3000         // Timeout más bajo = respuesta más rápida
    }
}
```

#### Cache de Variables:
```cpp
// En pac_control_client..cpp
cache_enabled = true;  // Habilitar cache para mejor performance
```

## Arquitectura del Sistema

### Flujo de Datos Completo:
```
Cliente OPC UA → Servidor OPC UA → PACControlClient → PAC S1
     ↑                                                    ↓
  Escritura                                         Lectura/Escritura
     ↓                                                    ↑
 Respuesta ← Mapeo Dinámico ← ASCII/Binario ← Protocolo PAC
```

### Tipos de Variables Soportadas:
1. **Variables de tabla** (binario): 21 TAGs × 15 variables
2. **Variables individuales** (ASCII): Float/Int32 ilimitadas
3. **Escritura selectiva**: Solo variables apropiadas
4. **Notación científica**: Soporte completo para rangos amplios

## Autor

Jose Salamanca - PetroSantander SCADA Project  
Protocolo PAC Control completamente reverse-engineered e implementado

## Licencia

Proyecto privado - PetroSantander