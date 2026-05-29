#pragma once

#include <string>
#include <vector>

// Forward decls — el interfaz no debe arrastrar headers pesados.
class NodeGraph;
struct CodegenSeedState;

namespace scinodes {

// ---------------------------------------------------------------------------
// ISimSession — contrato del orquestador de una sesión de simulación.
//
// Sobre la capa de cómputo (`IComputeBackend`, que define `step(dt) → samples`
// stateless), ISimSession gobierna el CICLO DE VIDA y la TEMPORALIDAD:
//
//   - preparar la sesión con un grafo, o regenerarla con un seed (hot-reload).
//   - ciclo del thread del solver (start/stop/pause/resume).
//   - captura del estado actual para sembrar el próximo reset.
//   - edición en vivo de parámetros.
//   - consultas: status, tiempo simulado, NaN-culpable, ¿está produciendo
//     samples ahora mismo?
//
// SimController depende SOLO de esta interfaz.  La implementación concreta
// (ScilabBridge hoy; otros backends posibles mañana) decide cómo se materializa
// — proceso aparte, embedding, solver propio, etc.  El controlador no
// distingue entre esos casos: solo le importa cuando la sesión está lista,
// cuando está produciendo y cómo se le pide capturar/aplicar estado.
//
// Esta separación elimina el acoplamiento anterior: SimController solía mirar
// directamente `bridge.time()` para inferir "¿avanzó la sim?", filtrando el
// comportamiento específico de Scilab (boot de 1-2 s, JVM GC, pipe handshake)
// hacia el dominio "estado y tiempo".  Ahora ISimSession::isProducing() es
// la pregunta semántica correcta y cada backend decide cómo responderla.
// ---------------------------------------------------------------------------
class ISimSession {
public:
    enum class Status {
        NotStarted,   // nunca se llamó reset()
        Ready,        // listo para step()
        Running,      // en medio de un step (transient)
        Stopped,      // sesión terminada limpiamente
        Error         // falló spawn o el solver divergió
    };

    virtual ~ISimSession() = default;

    // ---- Lifecycle -------------------------------------------------------
    // Inicializa la sesión con el grafo dado.  Si seed != null, los estados
    // iniciales se siembran con esos valores y `t_prev` arranca en seed.t
    // (hot-reload tras una edición aditiva durante Pause).  Idempotente:
    // la segunda llamada limpia la sesión previa y arranca de nuevo.
    virtual bool reset(const NodeGraph& g)                          = 0;
    virtual bool reset(const NodeGraph& g, const CodegenSeedState& seed) = 0;

    // Termina la sesión (mata recursos asociados, p.ej. subproceso).
    // Idempotente.
    virtual void stop()                                             = 0;

    // Vacía buffers de plot e índices de tiempo SIN tocar el backend
    // numérico.  Si el thread del solver sigue corriendo, lo siguiente
    // que produzca empieza a llenar buffers desde 0.
    virtual void clearBuffers()                                     = 0;

    // ---- Solver thread ---------------------------------------------------
    // Lanza el worker que dispara step() a `dt` periódico.  Devuelve false
    // si ya hay un thread vivo o si status() != Ready.
    virtual bool startSolverThread(float dt)                        = 0;

    // Detiene el thread y espera su join.  Tras esto, captureState() es
    // seguro porque ningún writer está tocando el pipe/backend.  No-op
    // si no había thread.
    virtual void stopSolverThread()                                 = 0;

    // Toggle de pausa sin matar el thread.  Mientras esté pausado, el
    // thread duerme y no llama a step().
    virtual void setPaused(bool paused)                             = 0;
    virtual bool isPaused()                  const                  = 0;

    // ---- Captura de estado (hot-reload) ---------------------------------
    // Pide al backend el (t, x) actual.  El caller debe llamar
    // stopSolverThread() antes (no se puede mezclar con step()).  Devuelve
    // false si la sesión no está Ready/Running o el backend no soporta
    // captura.
    virtual bool captureState(CodegenSeedState& out)                = 0;

    // ---- Live-tune de parámetros ----------------------------------------
    // Aplica un cambio de valor a un parámetro durante la simulación.
    // `path` es la cadena de SubGraph ids para localizar nodos dentro de
    // jerarquías; backend traduce al id aplanado.
    virtual bool sendParameter(const std::vector<int>& path,
                               int paramIdx, double value)          = 0;

    // ---- Consultas -------------------------------------------------------
    virtual Status      status()          const                     = 0;
    virtual const std::string& lastError() const                    = 0;

    // Tiempo simulado acumulado desde el último reset (en segundos).
    // Avanza monotónicamente con cada step() exitoso.
    virtual float       time()            const                     = 0;

    // Id del primer nodo (en orden topo) cuya salida fue no-finita en el
    // step más reciente.  0 si nada ha divergido.  La UI lo usa para
    // resaltar el nodo culpable.
    virtual int         offendingNodeId() const                     = 0;

    // ¿La sesión está activamente produciendo samples?  Falso durante el
    // boot (p.ej. spawn de un subproceso, carga de scripts), true a partir
    // del primer step exitoso después de un reset/startSolverThread.
    // SimController lo usa para que el factor tiempo-real no cuente wall
    // time mientras el backend aún no produce; sin esto, el indicador
    // baja a rojo durante el spawn aunque la sim luego corra al ritmo
    // correcto.
    virtual bool        isProducing()     const                     = 0;

    // ---- Acceso a los buffers de muestras (lectura) ----------------------
    // Los panels (PlotPanel, View3DPanel) leen los buffers acumulados
    // de cada sink para graficar o animar.  Antes tomaban `ScilabBridge&`
    // concreto; ahora consultan la sesión por su interfaz para que
    // cualquier backend (Scilab subprocess, call_scilab embebido, solver
    // propio) pueda exponer sus muestras sin tocar a los clientes.
    //
    // Convenciones:
    //   - `channel` 0 = single-channel sinks (DataLogger, FFT) o canal
    //     primario; 0..N-1 para multi-channel (PhasePortrait, Oscilloscope).
    //   - Si el sinkNodeId no existe o no tiene muestras todavía,
    //     buffer() devuelve vacío y writeIndex() devuelve 0.
    //   - El vector que devuelve buffer() es una COPIA — la sesión
    //     bloquea su mutex internamente para sacar el snapshot.
    virtual std::vector<float> buffer(int sinkNodeId,
                                      int channel = 0) const       = 0;
    virtual int writeIndex(int sinkNodeId, int channel = 0) const   = 0;
    virtual int channelCount(int sinkNodeId)             const      = 0;

    // dt efectivo del último step()/startSolverThread().  0 hasta el
    // primer step.  Los exporters lo usan para reconstruir timestamps
    // por sample; las plots para mapear índice a tiempo.
    virtual float solverDt() const                                  = 0;
};

}  // namespace scinodes
