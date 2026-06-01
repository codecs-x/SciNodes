# Pruebas — unit, integración, headless

SciNodes tiene seis binarios de test.  Cada uno corre por
separado y tiene su propio set de dependencias.

## `test_grammar` — headless, sin Scilab

Pruebas de la capa modelo: grafo, gramática, análisis dimensional,
codegen offline.  No requieren Scilab, ni Vulkan, ni ventana.

```bash
cmake --build build --target test_grammar -j
./build/test_grammar
```

## `test_canvas` — headless, sin Scilab

Pruebas del editor de nodos sin levantar Vulkan.  Usa
`FakeNodeRenderer` para verificar que las operaciones
estructurales (encapsulate, paste, undo) llegan al renderer
con los argumentos esperados.

```bash
./build/test_canvas
```

## `test_i18n` — headless

Verifica que las JSON de `i18n/` parseen y que todas las keys
referenciadas por código existan en `es.json` y `en.json`.

```bash
./build/test_i18n
```

## `test_example_library` — headless

Carga cada `.scn` de `examples/graphs/` y corre validación de
gramática (R1–R7) + propagación dimensional sobre cada uno.
Falla si algún ejemplo del repo está corrupto.

```bash
./build/test_example_library
```

## `test_contracts` — headless

Tests de contrato: `IComputeBackend`, `INodeRenderer`,
`IAssetService`.  Cada implementación pasa por el mismo set
de aserciones para garantizar que respeta el contrato (no
sólo el fake, también la implementación real cuando hay
forma de fake-ear sus dependencias).

```bash
./build/test_contracts
```

## `test_integration` — con Scilab real

Pruebas end-to-end del pipeline: graph → codegen → scilab-cli →
buffer → assertion sobre el valor.

```bash
cmake --build build --target test_integration -j
./build/test_integration
```

Requiere `scilab-cli` accesible (respeta `SCN_SCILAB_PATH`).

Cubre:

- `ScilabCodeGen::generate` produce script ejecutable.
- Sistemas canónicos: integrador, oscilador armónico,
  motor DC.  Comparación contra solución analítica con
  tolerancia 1e-6.
- Hot-reload: cambiar param a la mitad de la sim produce el
  efecto esperado.

Corre en ~30-60 segundos (cada test lanza un `scilab-cli`).

## Auditorías sobre fixtures

Complementarias a los tests: corren sobre los `.scn` reales del
repo en lugar de fixtures embebidos.  No usan `ASSERT_*` — son
herramientas que devuelven exit-code y reporte humano.

### `audit_examples`

Itera `examples/graphs/*.scn`, los deserializa con **R7
enforcement ON** (default desde v0.1.1) y reporta cuáles tienen
aristas rechazadas o tipos desconocidos.  Sirve para detectar
drift entre fixtures pre-existentes y cambios en la gramática
dimensional.

```bash
cmake --build build --target audit_examples -j
./build/audit_examples                  # default ./examples/graphs
./build/audit_examples otro_dir/
```

Exit-code `1` si algún `.scn` tiene rechazos, `0` si todos
parsean limpios.  Útil en CI tras tocar el catálogo de
unidades o `tryAddEdge`.

Linkea sólo `scinodes_units + scinodes_graph`, así que arranca
en milisegundos.

## Estilo de aserciones

El proyecto usa un macro casero `ASSERT_*` definido en
`test/Assertions.hpp`:

```cpp
ASSERT_EQ(graph.nodeCount(), 5);
ASSERT_NEAR(buffer.back(), 1.0, 1e-6);
ASSERT_DIAG(diags, 1, "edge.unit_mismatch");
```

Por qué no Catch2/GoogleTest: dependencia extra, output
verboso, mucho macro magic.  Para ~500 aserciones totales el
macro casero alcanza.

## Cómo agregar un test

1. Crear `test/test_<nombre>.cpp` con función `int main()` que
   llama macros `ASSERT_*`.
2. Agregar al `CMakeLists.txt`:
   ```cmake
   add_executable(test_mything test/test_mything.cpp)
   target_link_libraries(test_mything PRIVATE scinodes_graph)
   ```
3. Si el test debe correr en CI: agregalo al runner en
   `.github/workflows/ci.yml`.

## Cuando un test rompe

El proyecto tiene una regla blanda:

> **No commitear con tests rotos.**

Si el cambio introduce un fallo legítimo, actualizá el test
en el mismo commit.  Si el cambio rompe algo que no debería
romper, arreglá el código.  Skip o disable de tests
("`.skip`", `xfail`) **sólo** con condición de eliminación
escrita arriba del skip:

```cpp
// SKIP: el codegen del nodo Custom expone un orden no
// determinístico de los placeholders.  Eliminar este skip
// cuando la etapa 6P lo arregle (TODO #234).
ASSERT_SKIP("CustomNode placeholder ordering");
```

## Coverage

No medimos coverage automáticamente.  La meta es **cubrir
las APIs públicas** de cada librería interna + los caminos
críticos del pipeline (codegen, R7 enforcement, hot-reload).

Las funciones de UI directa no se testean por unit — se
testean indirectamente vía `FakeNodeRenderer`.
