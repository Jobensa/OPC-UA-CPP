#!/usr/bin/env python3
"""
Test de escritura OPC-UA para variables Set_xx
"""

from opcua import Client
from opcua import ua
import time
import sys

def test_write():
    # Conectar al servidor OPC-UA
    client = Client("opc.tcp://localhost:4840")
    
    try:
        print("🔌 Conectando al servidor OPC-UA...")
        client.connect()
        print("✅ Conectado exitosamente")
        
        # Lista de variables para probar escritura
        test_variables = [
            ("TT_11006.SetHH", 200.0),
            ("TT_11006.SetH", 150.0), 
            ("TT_11006.SetL", 50.0),
            ("TT_11006.SetLL", 25.0),
            ("TT_11006.SIM_Value", 75.0),
        ]
        
        # Navegar al directorio de objetos
        objects = client.get_objects_node()
        print("📁 Navegando objetos...")
        
        # Buscar el nodo TT_11006
        tt_11006 = objects.get_child("1:TT_11006")
        print("📊 Encontrado nodo TT_11006")
        
        # Probar escritura de cada variable
        for var_name, test_value in test_variables:
            try:
                print(f"\n✍️ Probando escritura: {var_name} = {test_value}")
                
                # Obtener el nodo de la variable
                var_parts = var_name.split('.')
                var_node = tt_11006.get_child(f"1:{var_parts[1]}")
                
                # Leer valor actual
                current_value = var_node.get_value()
                print(f"   📖 Valor actual: {current_value}")
                
                # Escribir nuevo valor con tipo FLOAT específico
                float_value = ua.Variant(test_value, ua.VariantType.Float)
                var_node.set_value(float_value)
                print(f"   📝 Escribiendo: {test_value}")
                
                # Esperar un poco y verificar
                time.sleep(0.5)
                new_value = var_node.get_value()
                print(f"   📗 Valor después de escritura: {new_value}")
                
                if abs(new_value - test_value) < 0.01:
                    print(f"   ✅ ÉXITO: Valor actualizado correctamente")
                else:
                    print(f"   ❌ FALLO: Valor no se actualizó (esperado: {test_value}, obtenido: {new_value})")
                    
            except Exception as e:
                print(f"   ❌ ERROR escribiendo {var_name}: {e}")
        
        print("\n🔍 Leyendo todos los valores finales:")
        all_vars = ["Input", "SetHH", "SetH", "SetL", "SetLL", "SIM_Value", "PV", "min", "max", "percent"]
        for var_name in all_vars:
            try:
                var_node = tt_11006.get_child(f"1:{var_name}")
                value = var_node.get_value()
                print(f"   {var_name}: {value}")
            except Exception as e:
                print(f"   {var_name}: ERROR - {e}")
                
    except Exception as e:
        print(f"❌ Error: {e}")
        return False
    finally:
        client.disconnect()
        print("\n🔌 Desconectado del servidor")
    
    return True

if __name__ == "__main__":
    print("🧪 Test de escritura OPC-UA")
    print("="*50)
    success = test_write()
    sys.exit(0 if success else 1)
