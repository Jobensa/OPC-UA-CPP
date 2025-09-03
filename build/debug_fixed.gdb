# Breakpoints automáticos
break createNodes
break updateData
break UA_Server_writeValue

# Comandos automáticos mejorados
commands 3
  printf "=== ESCRIBIENDO VALOR A NODO ===\n"
  # Imprimir información del NodeId
  if nodeId.identifierType == 2
    printf "NodeId STRING: %s\n", (char*)nodeId.identifier.string.data
  end
  # Imprimir tipo del valor de forma segura
  if value.type != 0
    printf "Tipo de valor: %u\n", value.type.typeId.identifier.numeric
  else
    printf "Valor sin tipo\n"
  end
  # Stack trace reducido
  bt 5
  continue
end

run
