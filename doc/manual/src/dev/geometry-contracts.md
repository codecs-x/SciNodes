# Contratos de geometría y assets glTF

## `ContractRegistry` (`src/core/`)

Singleton que carga contratos JSON desde `contracts/`.

```cpp
class ContractRegistry {
public:
    bool loadFromDirectory(const std::string& dir, std::string* err);
    const Contract* find(const std::string& deviceType) const;
    std::vector<std::string> deviceTypes() const;
};
```

El `Contract` tiene `device_type`, lista de `joints` con su
axis canónico, y `mappings` puerto-del-nodo → bone-del-asset.

## `DeviceAssetLoader` (`src/core/`)

Wrapper de `tinygltf` que carga `.gltf`/`.glb`.  Devuelve
una `DeviceAsset` con malla, transformaciones por nodo y
una tabla de bones por nombre para el lookup desde el
contrato.

## `NodeCategory::Device`

Categoría nueva en el enum.  Se comporta como `Transformer`
en gramática y codegen (acepta inputs, emite outputs), pero
el resto del sistema (UI, asset binding, outliner) lo trata
como "dispositivo físico con geometría asignable".

## `OutlinerPanel` (`src/ui/`)

Vista jerárquica de los devices instanciados en el grafo
con sus *parts* expandibles.  El panel es read-only en esta
versión; servir como un editor del binding está fuera de
alcance.

## `View3DPanel` con `tinygltf`

Cuando un device está activo, `View3DPanel` lo renderiza
con depth buffer real (no solo wireframe).  Las
transformaciones aplicadas a los joints vienen del mapeo
`outputPort → bone` del contrato.
