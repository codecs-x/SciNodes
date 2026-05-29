# IComputeBackend y el bridge a Scilab

`IComputeBackend` es el contrato entre la capa modelo y
cualquier motor de cálculo.  La implementación actual habla
Scilab, pero el contrato no menciona Scilab.

## Interfaz (`src/core/IComputeBackend.hpp`)

```cpp
struct SinkSample {
    int    nodeId;
    int    channel;
    double value;
};

class IComputeBackend {
public:
    enum class Status { NotStarted, Ready, Running, Stopped, Error };

    virtual ~IComputeBackend() = default;

    // Carga el grafo compilado. Idempotente: una segunda llamada
    // limpia el estado previo.
    virtual bool prepare(const BackendPrepareSpec& spec) = 0;

    // Avanza la simulación dt segundos. Llena outSamples con un valor
    // por cada SinkChannel declarado en la spec, en el mismo orden.
    // Si algún canal produce NaN/Inf, *outOffendingNodeId queda con el
    // id del nodo culpable (en orden topológico).
    virtual bool step(double dt,
                      std::vector<SinkSample>& outSamples,
                      int* outOffendingNodeId) = 0;

    // Actualiza un parámetro vivo. Se aplica en o antes del próximo step().
    virtual bool setParameter(int nodeId, int paramIdx, double value) = 0;

    // Exporta el historial acumulado al disco. Formato lo decide la impl.
    virtual bool exportHistory(const std::string& path,
                               std::string*       result) = 0;

    virtual void        shutdown()    = 0;
    virtual Status      status()    const = 0;
    virtual std::string lastError() const = 0;
};
```

## BackendPrepareSpec

La spec que `ScilabCodeGen` produce y el backend consume:

```cpp
struct BackendPrepareSpec {
    std::string         dynamicsFunction;   // f(t,x) — function ... endfunction
    int                 stateSize;          // dim del vector x
    std::vector<double> initialState;       // IC (vacío = ceros)
    std::string         outputEvalScript;   // recomputa v<id>_<port> tras ode()
    std::string         stepFunction;       // wrapper scn_step para in-process

    struct SinkChannel {
        int         nodeId;
        int         channel;
        std::string expression;       // ej. "v17_0"
    };
    std::vector<SinkChannel> sinkChannels;       // los del UI
    std::vector<SinkChannel> bufferedChannels;   // TODOS los outputs escalares

    struct ParamSlot {
        int         nodeId;
        int         paramIdx;
        std::string scilabName;
        double      initialValue;
    };
    std::vector<ParamSlot> params;
};
```

`sinkChannels` ⊆ `bufferedChannels`: la primera es lo que ve la
UI; la segunda lo que el bridge bufferea para que los walkers
(3D, sub-tap-finder) puedan leer en cualquier punto del grafo,
no sólo en sinks.  Distinción introducida en etapa 6J.8 — ver
[Codegen](codegen.md).

## Implementación actual — ScilabBridge

`src/core/ScilabBridge.cpp` implementa el contrato con un
subproceso `scilab-cli` + pipe.  Su `prepare()` lanza el
subproceso, le manda `dynamicsFunction` + `stepFunction` para
ser compiladas una vez.  Cada `step(dt, …)` envía un comando
"step dt" por el pipe y parsea la respuesta.

Protocolo del pipe:

```
> step 0.001
STATE 0.001 0.998 0.001 0.123 4.567
> step 0.001
STATE 0.002 1.994 0.003 0.245 9.001
> param 17 0 5.0
OK
> shutdown
DONE
```

Cada línea STATE contiene el tiempo + los valores
muestreados de todos los `bufferedChannels` declarados en la
spec, en orden.

## Por qué subprocess y no embedding

Datos medidos:

| Approach          | Overhead por step                  | GC churn  |
|-------------------|------------------------------------|-----------|
| `call_scilab`     | ~5 ms fijo + picos 168 ms (GC JVM) | alto      |
| Subprocess + pipe | ~0.3 ms                            | nulo      |

`call_scilab` parece más simple en papel ("embeber la lib y
evitar IPC") pero el costo dominante en loops es la
entrada/salida del intérprete, no el pipe — y el GC del JVM
que arrastra Scilab introduce picos no acotados.

Ver memoria interna `feedback_scilab_backend.md` para datos
crudos.

## Live tuning (hot-reload)

`setParameter(nodeId, paramIdx, value)` envía un comando
`param <nodeId> <paramIdx> <value>` al subproceso.  El driver
Scilab actualiza la variable nombrada `scilabName` antes del
próximo `step()` — sin reset del solver.

Esta es la habilidad central de SciNodes: cambiar `Kp` del PID
durante una corrida sin perder el transitorio.

## Backends alternativos (post v0.2)

El contrato está pensado para soportar:

| Backend candidato            | Estado                                  |
|------------------------------|-----------------------------------------|
| `ScilabCallApiBackend`        | Variante in-process via `call_scilab`. Diseñada (`stepFunction` la apoya), no default. |
| SUNDIALS                      | Diseño esbozado. C nativo, sin pipe.    |
| FMI                           | Co-simulación con OpenModelica/Dymola.  |
| Modelica directo              | Vía OMCompiler.                         |
| Batch mode (sin clock real)   | Etapa 7 (#355), post-sustentación.      |

Cada uno se registra al runtime sin tocar UI ni ScilabCodeGen.
Quien construye el backend decide su nivel de fidelidad con el
contrato.

## Cómo testear sin Scilab

Hay un fake backend para tests headless (ver
[Testing](testing.md)).  Su `step()` evalúa una dinámica
hardcoded en C++ — sirve para validar que el resto del
pipeline (UI, codegen, propagación dimensional) responde al
backend sin necesitar `scilab-cli` corriendo.

## Lifecycle desde la perspectiva de la UI

| Evento UI       | Llamada al backend                              |
|-----------------|-------------------------------------------------|
| Click Run       | `prepare(spec)` + bucle de `step(dt, ...)`      |
| Click Pause     | El bucle se interrumpe; backend conserva estado |
| Click Resume    | El bucle continúa llamando `step()`            |
| Click Stop      | `shutdown()`                                    |
| Editar param    | `setParameter(...)`                             |
| Export CSV      | `exportHistory(path, &result)`                  |
| App exit        | `shutdown()`                                    |

## Timeouts del bridge

Definidos en `src/core/ScilabBridge.cpp:23-32`:

| Operación              | Constante              | Síntoma si lo excede                            |
|------------------------|------------------------|-------------------------------------------------|
| Step del solver        | `kTotalTimeoutMs = 60 s` | Sim abortada, error en statusbar               |
| Chunk de lectura       | `kChunkMs = 200 ms`    | Re-trigger del read en el siguiente tick        |
| Spawn de `scilab-cli`   | `kReadyTimeoutMs = 15 s` | Backend reporta error al arrancar              |
| Export (SOD / CSV)     | `kSaveTimeoutMs = 10 s` | El usuario ve "Export timed out"               |

## Caveat del marker NaN/Inf

`step()` setea `*outOffendingNodeId` con el **primer nodo en
orden topológico** cuyo output explotó.  No siempre es el
culpable real — un upstream defectuoso puede propagar el
`NaN` y el primer nodo a explotar es el que el walker
encontró primero.  Para debuggear, mirar nodos upstream del
marcado.

## Frontera de estabilidad RK4

El driver usa RK4 a paso fijo con 16 substeps internos
(`h_sub = (1/60)/16 = 1/960 s`).  Frontera de estabilidad
|hλ| < 2.78 → polos estables hasta ~2670 rad/s.  Nodos con
dinámica más rápida divergen.  Detalles + tabla de casos
prácticos en
[Features § Limitaciones numéricas](../user/features.md#17-limitaciones-numéricas-del-solver).

## Snap de denormales

Después de cada step, componentes del estado con
`|x| < 1e-30` se snapean a `0`.  Sin esto la FPU x86 entra en
modo denormal cerca del steady-state y los steps pasan de
2 ms a 200 ms.  Estados físicos legítimamente menores a
`1e-30` no son representables.
