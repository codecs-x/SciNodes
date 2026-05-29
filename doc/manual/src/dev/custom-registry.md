# `CustomNodeRegistry`

`src/core/CustomNodeRegistry.{cpp,hpp}` es un *singleton*
que carga descriptores `.json` y los expone al *popup* y al
codegen.

## API

```cpp
class CustomNodeRegistry {
public:
    static CustomNodeRegistry& instance();
    bool loadFromFile(const std::string& path, std::string* err = nullptr);
    const CustomNodeDef* find(const std::string& typeId) const;
    std::vector<std::string> typeIds() const;
    void clear();
};
```

## Esquema del descriptor

`CustomNodeDef` con `typeId`, `label`, `description`,
`category`, `inputPorts`, `outputPorts`, `params`,
`expression`.  Cargado con `nlohmann/json` con esquema
declarativo mínimo; campos extra se ignoran.

## Sustitución de placeholders

Cuando `ScilabCodeGen::planNode` toca un nodo `Custom`,
busca el `customType` en el registry, toma la plantilla y
sustituye `u<i>` por las expresiones upstream y `p_<name>`
por los valores de los parámetros.
