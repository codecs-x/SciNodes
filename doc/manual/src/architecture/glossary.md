# Glosario de términos

Términos del proyecto en orden alfabético.  Marcado **[H]**
los que se usan también en la tesis al estudiante de
mecatrónica; **[L]** los puramente de implementación.

| Término                       | Significado                                                                  |
|-------------------------------|------------------------------------------------------------------------------|
| **Anti-corruption layer** [L]  | Capa que traduce entre el idioma del proyecto y el de una librería externa. |
| **Asset** [H]                  | Recurso externo: típicamente un `.gltf` para visualización 3D.               |
| **Backend** [L]                | Motor de cálculo (Scilab subprocess, FMI, …) detrás de `IComputeBackend`.    |
| **Bridge** [L]                 | Implementación concreta del backend.  Ej.: `ScilabBridge`.                   |
| **Bufferización** [L]          | Cada escalar producido por el solver se guarda en buffer accesible para walkers — no sólo los sinks. |
| **Cable** [H]                  | Arista del grafo entre dos puertos.                                          |
| **Canvas** [H]                 | El área 2D donde se dibujan los nodos.                                       |
| **Custom node** [H]            | Nodo definido en JSON, cargado en runtime sin recompilar.                    |
| **Diagnóstico** [L]            | Mensaje de error o warning producido por la gramática.                       |
| **Dispatch polimórfico** [L]   | `variant + visit` para procesar cada NodeKind según su tipo.                 |
| **DSL** [L]                    | Domain-Specific Language. Aquí: el JSON de custom nodes y el `.scn`.         |
| **Editor space** [L]           | Coordenadas internas del canvas, antes de aplicar pan/zoom.                  |
| **Field** [L]                  | Parámetro de un nodo expresable como `Quantity`.                              |
| **Flatten** [L]                | Aplanar un sub-grafo en el codegen para que el `.sce` no tenga scopes.        |
| **Grafo** [H]                  | El modelo entero — nodos + cables.                                            |
| **Gramática** [H]              | Las reglas R1–R7 que validan un grafo.                                       |
| **Hot-reload** [H]             | Cambiar un parámetro durante la simulación sin reiniciar.                    |
| **i18n** [L]                   | Internacionalización.  Sistema key→string en JSON.                           |
| **IComputeBackend** [L]        | Interfaz que abstrae el motor de cálculo (Scilab, FMI, …).                   |
| **INodeRenderer** [L]          | Interfaz que abstrae el render del canvas (NativeNodeRenderer, FakeRenderer).|
| **Nodo** [H]                   | Una caja con puertos en el grafo.                                            |
| **NodeKind** [L]               | Etiqueta de gramática: Builtin, Custom, SubGraph*, etc.                      |
| **NodeType** [H]               | Identificador del tipo de nodo (`Gain`, `Integrator`, …).                    |
| **Path-key** [L]               | Ruta jerárquica del SubGraph activo: `/`, `/5/`, `/5/2/`.                    |
| **Plan** [L]                   | Estructura intermedia producida por ScilabCodeGen antes del script final.   |
| **Puerto** [H]                 | Punto de conexión de un nodo. Input (izq) u Output (der).                    |
| **Quantity** [L]               | `(value, Unit)`. Reemplaza al `double` puro para los parámetros físicos.     |
| **R1..R7** [H/L]               | Las siete reglas de gramática.                                                |
| **Registry** [L]               | Tabla global de NodeTypes conocidos.                                          |
| **`.scn`** [H]                 | Formato JSON del grafo.                                                       |
| **SI** [H]                     | Sistema Internacional.  Base del catálogo de unidades.                       |
| **SceneCollector** [L]         | Walker que extrae la escena 3D del sub-grafo geométrico.                     |
| **SubGraph** [H]               | Nodo compuesto que contiene otro grafo en su interior.                       |
| **SubGraphInput/Output** [L]   | Stubs que materializan aristas que cruzan la frontera del SubGraph.          |
| **Stateful** [L]               | Nodo que tiene memoria (Integrator, Delay, …).                               |
| **Sink** [H]                   | Sumidero — nodo terminal que recibe pero no produce.                         |
| **STATE line** [L]             | Línea del pipe Scilab: `STATE t ch1 ch2 …`.                                   |
| **TypeExpr** [L]               | Expresión de tipo del puerto: scalar, vec3, geometry.                        |
| **Unit** [L]                   | Vector de 8 exponentes SI + magnitud.                                         |
| **Walker** [L]                 | Algoritmo que recorre el grafo (DFS típicamente).                            |
| **WYSIWYG** [L]                | What You See Is What You Get — el zoom escala todo proporcionalmente.        |
