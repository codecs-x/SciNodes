# Registry de nodos custom

`CustomNodeRegistry` (en `src/core/`) es el *hook* del editor
para definiciones de nodos cargadas en *runtime*. Vive en
paralelo al `nodeRegistry()` *built-in* del `NodeType.cpp`: la
gramática consulta ambos al validar, y el codegen sustituye la
`expression` del descriptor cuando emite el *script* Scilab.

## Diseño

El catálogo *built-in* está keyado por la enumeración cerrada
`enum class NodeType` y vive como una constante de compilación.
El registry custom está keyado por una `std::string typeId` y
vive como un singleton mutable en *runtime*:

```cpp
class CustomNodeRegistry {
public:
    static CustomNodeRegistry& instance();

    bool loadFromJsonString(const std::string& json,
                            std::string* err = nullptr);
    bool loadFromFile(const std::string& path,
                      std::string* err = nullptr);

    const CustomNodeDef* find(const std::string& typeId) const;
    std::vector<std::string> typeIds() const;
};
```

`CustomNodeDef` carga lo mínimo para que la gramática y el
codegen puedan tratarlo igual que un nodo *built-in*:

```cpp
struct CustomNodeDef {
    std::string  typeId;        // clave única
    std::string  label;
    std::string  description;
    NodeCategory category;      // Source, Transformer, Sink
    int          inputPorts;
    int          outputPorts;
    std::vector<ParamDef> params;
    std::string  expression;    // Scilab expr, vacío para src/sink
};
```

## El archivo JSON

Cada descriptor en `doc/custom_nodes/` es un JSON con la
estructura documentada en
[Nodos personalizados](../user/custom-nodes.md). El loader es
permisivo respecto al orden de claves pero estricto respecto al
contenido:

- `type_id` no puede chocar con un *built-in* del enum
  `NodeType` ni con otro custom ya registrado.
- `category` se acepta como `"source" | "transformer" | "sink"`
  (string en minúsculas).
- `params[].name` debe ser un identificador C válido (la
  sustitución `p_<name>` lo usa como sufijo en el script).
- Si la carga falla, el registry queda sin tocar y `err`
  recibe el mensaje.

## Expression template

Para transformadores, la `expression` se interpreta como una
expresión Scilab con dos clases de placeholders:

- `u1`, `u2`, … — sustituidos por la expresión Scilab que
  produce cada puerto de entrada, en orden de cable.
- `p_<name>` — sustituido por el valor actual del parámetro
  con ese nombre.

La sustitución es por texto, sin parsing de Scilab: el codegen
hace un `replace_all` por cada placeholder. Es responsabilidad
del autor del descriptor escribir una expresión Scilab válida;
el editor no la pre-valida.

## Integración con grammar + codegen

Al cablear, `GrammarParser::validateEdge` consulta primero al
catálogo *built-in* y, si el tipo no está ahí, al
`CustomNodeRegistry`. Las reglas R0–R5 se aplican de la misma
manera; un custom se rechaza con el mismo `GrammarError` si
viola alguna.

En el codegen, `ScilabCodeGen` itera los nodos en orden
topológico y, cuando encuentra un nodo cuyo `type_id`
corresponde a un custom, busca la `expression` del descriptor,
sustituye los placeholders y emite la línea Scilab resultante.
La salida queda disponible para los cables aguas abajo igual
que cualquier salida *built-in*.

## Limitaciones de esta versión

- Sólo transformadores algebraicos. No hay forma de declarar
  estado propio en un custom; los nodos con estado siguen
  limitados al catálogo *built-in*.
- No hay *hot reload*: cambios en los `.json` requieren
  reiniciar el editor.
- La carga es explícita: `AppWindow` invoca
  `loadFromFile` por cada descriptor en `doc/custom_nodes/` al
  arrancar. Las versiones siguientes itinerarán el directorio
  automáticamente.

## Tests

`test_integration` ejerce el camino completo en el escenario
17 (`Step → Custom("Tripler", k=2) → Scope`): carga el
descriptor `tripler.json`, instancia el nodo, lo cablea, lanza
`scilab-cli` y verifica que la salida sea `3 · 2 · 1 = 6`. Si
la sustitución del *template* o la integración con el codegen
se rompen, el escenario falla.
