#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Explorador de estructura del servidor OPC-UA
"""

from opcua import Client

def explore_node(node, level=0):
    """Explora recursivamente un nodo y sus hijos"""
    indent = "  " * level
    try:
        node_id = node.nodeid
        browse_name = node.get_browse_name()
        display_name = node.get_display_name()
        
        print(f"{indent}📁 {display_name.Text} (BrowseName: {browse_name}, NodeId: {node_id})")
        
        # Solo explorar hasta nivel 3 para evitar demasiada salida
        if level < 3:
            try:
                children = node.get_children()
                for child in children:
                    explore_node(child, level + 1)
            except Exception as e:
                print(f"{indent}  ❌ Error explorando hijos: {e}")
                
    except Exception as e:
        print(f"{indent}❌ Error: {e}")

def main():
    print("🔍 Explorador de estructura OPC-UA")
    print("=" * 50)
    
    client = Client("opc.tcp://localhost:4840")
    
    try:
        client.connect()
        print("✅ Conectado al servidor\n")
        
        root = client.get_root_node()
        objects = root.get_child(["0:Objects"])
        
        print("📂 Explorando Objects:")
        children = objects.get_children()
        
        for child in children:
            try:
                display_name = child.get_display_name()
                if "TT_11006" in display_name.Text:
                    print(f"\n🎯 Encontrado: {display_name.Text}")
                    explore_node(child, 0)
            except:
                continue
                
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        try:
            client.disconnect()
            print("\n🔌 Desconectado")
        except:
            pass

if __name__ == "__main__":
    main()
