# `TypeExpr`: tipos de puerto generalizados

`enum class PortType` se reemplaza por un sistema de tipos
algebraico.

## Estructura

```cpp
struct TensorType {
    std::vector<int> dims;   // dims.empty() = escalar
};

struct TypeExpr {
    std::variant<ScalarType, TensorType, /* futuros: SymType, ... */> impl;
};
```

## Migración

Cada `NodeDef` declara `inputPortTypes[]` y
`outputPortTypes[]` como `vector<TypeExpr>` en lugar del
viejo `vector<PortType>`.  Los nodos clásicos quedan con
`TensorType{}` (escalar puro); los nuevos `Vec3*` con
`TensorType{3}`.

## R6 — compatibilidad de tipos

`GrammarParser::checkR6` compara los `TypeExpr` de los dos
puertos.  Igualdad estructural (mismas `dims`) es la
condición.  El mensaje de error reporta el *shape* esperado
y el recibido.
