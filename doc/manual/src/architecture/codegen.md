# Codegen `.sce` y propagación de buffers

`ScilabCodeGen` traduce un `NodeGraph` a un script `.sce`
listo para ejecutar en `scilab-cli`.

## Estructura del archivo generado

```scilab
// Header: declaración de parámetros, dt, t_final
dt = 0.001; t_final = 5.0;
p_Kp = 2.0; p_Ki = 0.5; ...

// Init: condiciones iniciales del estado
x = zeros(N_state, 1);

// Loop principal
for k = 1:N_steps
    t = k * dt;

    // Lee pipe para tunings en vivo
    apply_param_updates();

    // Computa derivadas (cada nodo aporta su línea)
    dxdt = compute_rhs(t, x);

    // Integra (rk explícito, paso fijo)
    x = x + dt * dxdt;

    // Emite STATE
    printf("STATE %.6f", t);
    for ch = 1:N_buffered
        printf(" %.6f", x(channel_index(ch)));
    end
    printf("\n");

    // Coordinación con UI
    handle_pause_resume();
end

printf("DONE\n");
```

## Dos rutas de salida

```cpp
// Ruta histórica — script .sce con REPL para el subproceso scilab-cli.
struct GeneratedPlan {
    std::string                       script;
    std::vector<SinkChannel>          sinkChannels;
    std::vector<SinkChannel>          bufferedChannels;
    std::string                       error;
    std::map<std::vector<int>, int>   idForPath;     // path canónico → flatId
    std::vector<std::pair<int,int>>   stateLayout;   // (nodeId, slot) por entrada de x
};

// Ruta nueva — BackendPrepareSpec consumible por cualquier IComputeBackend
// (subprocess, call_scilab, mock).  Reusa la planeación interna.
struct GeneratedSpec {
    scinodes::BackendPrepareSpec spec;
    std::string                  error;
};
```

`ScilabCodeGen::generate(graph)` devuelve `GeneratedPlan`.
`ScilabCodeGen::generateSpec(graph)` devuelve `GeneratedSpec`.
Las dos rutas comparten la misma lógica de planeación
por nodo; lo que cambia es el formato del entregable.

La distinción `sinkChannels` vs `bufferedChannels`:

- **sinkChannels**: lo que la UI muestra en `Oscilloscope` y
  similares.  Subset (típicamente N=2..5).
- **bufferedChannels**: todos los escalares que el solver
  produce.  Superset (N puede ser 20+).  El bridge los
  bufferea **todos** para que los walkers 3D y los taps
  intermedios puedan leer en vivo desde cualquier punto del
  grafo, no sólo desde sinks.

Este split se introdujo en etapa 6J.8 — antes los walkers
intentaban leer desde sinks y rompían cuando había
convertidores intermedios.

## planNode — tabla de hooks

Cada NodeType aporta una función `planNode` que escribe su
contribución al `scriptBody`:

```cpp
using PlanNodeFn = void(*)(PlanContext&, const NodeInstance&);

const std::unordered_map<std::string, PlanNodeFn> kPlanners = {
    { "Gain",        planGain },
    { "Integrator",  planIntegrator },
    { "Summation",   planSummation },
    { "StepSignal",  planStepSignal },
    // ...
};
```

`planGain`:

```cpp
void planGain(PlanContext& ctx, const NodeInstance& n) {
    const auto u  = ctx.inputExpr(n, 0);
    const auto k  = n.fields.at("Gain").asDouble();
    ctx.emit(ctx.outputVar(n, 0) + " = " + std::to_string(k) +
             " * " + u + ";");
}
```

Patrón: leer el contexto (qué variables Scilab representan los
inputs), generar la expresión Scilab, registrar la variable
output.

## SubGraph crossings

Los SubGraphs no aparecen en el `.sce` como tales — el codegen
los **flattenea**.  Los stubs `SubGraphInput`/`SubGraphOutput`
quedan eliminados; los nodos internos pasan a vivir directamente
en el alcance global del script.

Esto preserva el orden topológico del grafo aplanado y evita
overhead de scoping en Scilab.

## Custom nodes

Los `CustomKind` (cargados de JSON) producen su línea Scilab
directamente desde el campo `scilab` del JSON, con substitución
de placeholders:

- `u1`, `u2`, … → variables Scilab de los inputs.
- `p_<name>` → variable Scilab del parámetro.
- `t`, `dt` → variables globales del script.

Ver [Personalizar nodos vía JSON](../user/custom-nodes.md) para
el formato.

## Validación pre-codegen

Antes de generar, el codegen corre `validateCustom` sobre cada
nodo custom — chequea que los placeholders del JSON estén
declarados y que no haya nombres de variables Scilab que
colisionen con built-ins.

## Generación de tests headless

`ScilabCodeGen::generateSimulation(graph)` produce un `.sce` con
un loop hardcoded (sin pipe, sin pause/resume) — útil para
tests offline:

```bash
scilab-cli -nb -e "exec('test/output.sce', -1); quit;"
```

Esto se usa en `test_integration` para verificar resultados
numéricos exactos sin depender de la UI.
