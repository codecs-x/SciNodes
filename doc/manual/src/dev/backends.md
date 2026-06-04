# Backends del solver

El backend **primario y recomendado** es el subproceso `scilab-cli`,
implementado dentro del propio `ScilabBridge`. A partir de v0.0.7 ese
delegado numérico se formalizó detrás de una interface,
`IComputeBackend`, lo que dejó la puerta abierta a otros motores.

`call_scilab` (Scilab in-process) entró por esa puerta como un
experimento: al principio parecía una alternativa más rápida —sin
*pipe*—, pero al medirlo resultó **inferior al subproceso** para el
solver real-time (la inicialización de la JVM del intérprete y el
costo del *dispatcher* dominan; el *pipe* del subprocess no era el
cuello de botella). Como ya existía la interface, quedó como un
backend que *funciona* pero no se recomienda como solver del grafo;
sirve para usos *one-shot* del motor Scilab.

## La interface: `IComputeBackend`

```cpp
class IComputeBackend {
public:
    enum class Status { /* NotPrepared, Ready, Error, … */ };

    virtual bool prepare(const BackendPrepareSpec& spec) = 0;
    virtual bool step(double dt, std::vector<SinkSample>& outSamples, /*…*/) = 0;
    virtual bool setParameter(int nodeId, int paramIdx, double value) = 0;
    virtual bool exportHistory(const std::string& path, /*…*/) = 0;
    virtual void shutdown() = 0;
    virtual Status      status()    const = 0;
    virtual std::string lastError() const = 0;
};
```

`BackendPrepareSpec` reúne lo que un backend necesita para arrancar:
el código generado por `ScilabCodeGen`, los identificadores de los
sumideros con su número de canales, los parámetros iniciales. Cada
`step` devuelve las muestras del paso en `outSamples` (un
`SinkSample` por canal). El resto del editor sólo conoce este
contrato.

## `ISimSession`: la cara que ve el editor

Por encima del backend hay una interface más alta, `ISimSession`
(`src/core/ISimSession.hpp`), que es lo que los paneles (`PlotPanel`,
`View3DPanel`, `StatusBar`) ven cuando consultan la simulación
corriente. Expone `status()`, `time()`, `channelCount(nodeId)`,
`buffer(nodeId, ch)`, `isProducing()`. **`ScilabBridge` implementa
`ISimSession`**; los paneles no conocen al backend concreto, sólo
hablan con la sesión.

## El subproceso, dentro de `ScilabBridge` (primario)

El subproceso `scilab-cli` no es una clase `IComputeBackend` aparte:
es el camino propio del `ScilabBridge`. Lanza `scilab-cli` con las
banderas `-nb -nwni -noatomsautoload`, escribe el *driver* al *stdin*
y lee las muestras línea-a-línea del *stdout* (detalle en
[Puente con Scilab](scilab-bridge.md)). Es el backend por defecto: no
requiere ningún paquete extra más allá de Scilab instalado, y es el
único que soporta *hot-reload*.

## `ScilabCallApiBackend` (opt-in)

La **única** implementación concreta de `IComputeBackend`
(`src/core/backends/ScilabCallApiBackend.{hpp,cpp}`). Embebe Scilab
en el mismo proceso del editor vía `StartScilab` / `SendScilabJob`.
El `ScilabBridge` lo toma como delegado opcional
(`std::unique_ptr<IComputeBackend> m_backend`, inyectado con
`setBackend(...)`); cuando está presente, el bridge le delega
`prepare`/`step`/`setParameter` en vez de usar su propio subproceso.

Para compilar el editor con este backend incluido:

```bash
cmake -B build -DSCINODES_WITH_CALLAPI=ON \
    -DSCILAB_PREFIX=/opt/scilab-2026.0.1
```

(`SCINODES_WITH_CALLAPI` está `OFF` por defecto.) Si las libs de
desarrollo de Scilab no se encuentran en `$SCILAB_PREFIX`, CMake
reporta el problema y la compilación falla pronto.

## Selección en runtime

No hay menú ni selector en la UI: la elección es la variable de
entorno `SCINODES_BACKEND`. Al arrancar, `AppWindow` lee `getenv`:

- sin definir (o cualquier valor ≠ `callapi`) → subproceso `scilab-cli`.
- `SCINODES_BACKEND=callapi` → si el binario se compiló con
  `SCINODES_WITH_CALLAPI=ON`, inyecta `ScilabCallApiBackend` en el
  bridge; si no, imprime un aviso y cae al subproceso.

## El precedente de la anti-corruption layer

`IComputeBackend` es el ejemplo canónico del patrón
*anti-corruption layer* en SciNodes: el editor habla su propio
idioma —`prepare(spec)`, `step(dt, …)`, `setParameter`— y el backend
traduce a llamadas del intérprete (escribir al *pipe*, mandar
`SendScilabJob`, etc.). El experimento de `call_scilab` demostró el
valor del patrón aunque el backend resultara inferior: probar un
motor alternativo no obligó a tocar el resto del editor, y descartarlo
como recomendación tampoco. Cuando llegue un tercer backend
(p. ej. Julia, GNU Octave o un solver propio en C++), se implementa
la interface y el resto del editor no se entera.
