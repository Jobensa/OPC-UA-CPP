# Breakpoints automáticos
break createNodes
break updateData
break UA_Server_writeValue

# Comandos automáticos al llegar a createNodes
commands 1
  printf "=== CREANDO NODOS ===\n"
  continue
end

# Comandos automáticos al llegar a updateData
commands 2
  printf "=== PRIMERA ACTUALIZACIÓN ===\n"
  continue
end

# Comandos automáticos al llegar a writeValue
commands 3
  printf "=== ESCRIBIENDO VALOR ===\n"
  printf "NodeId: %p, Value type: %d\n", nodeId, value.type->typeId.identifier.numeric
  bt 3
  continue
end

run
