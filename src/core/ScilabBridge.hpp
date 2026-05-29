#pragma once
#include "IComputeBackend.hpp"
#include "NodeGraph.hpp"
#include "ScilabCodeGen.hpp"   // CodegenSeedState para hot-reload
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------
// ScilabBridge — owns a scilab-cli subprocess and pipes commands /
// receives state vectors over its stdin / stdout.
//
// Lifecycle:
//   reset(graph)  — (re)spawn scilab-cli with a driver script generated
//                   from the graph topology. Idempotent: calling reset()
//                   again kills the previous child first.
//   step(dt)      — advance simulation by dt seconds; parses one state
//                   vector and updates per-sink ring buffers.
//   stop()        — send `quit`, wait, reap.
//
// Buffering: stdout from Scilab is line-buffered when piped (verified on
// Scilab 2026.0.1). Communication is therefore line-based and blocking.
//
// Graceful degradation: if scilab-cli cannot be located or spawned the
// bridge enters Status::Error with a populated lastError(); the caller
// (AppWindow) can render a banner and keep editing.
// -----------------------------------------------------------------------
class ScilabBridge {
public:
    // Tamaño visible por defecto de un Oscilloscope (en samples).  Los
    // buffers internos NO están limitados a esta constante — crecen
    // libremente con std::vector::push_back y acumulan toda la
    // simulación.  Esta constante solo la usan los renderers como
    // ancho de cámara cuando el sink no expone su propio param
    // "Time Window".
    static constexpr int DEFAULT_VISIBLE_SAMPLES = 512;

    enum class Status {
        NotStarted,   // never reset() — placeholder state
        Ready,        // child alive, waiting for `step`
        Running,      // currently stepping (transient)
        Stopped,      // child exited cleanly
        Error         // failed to spawn or solver diverged
    };

    ScilabBridge() = default;
    ~ScilabBridge();
    ScilabBridge(const ScilabBridge&)            = delete;
    ScilabBridge& operator=(const ScilabBridge&) = delete;

    // Inyectar un backend que reemplace al subproceso scilab-cli.
    // Debe llamarse ANTES de reset(). Si nunca se llama, el bridge usa el
    // path histórico (fork + pipe). Si se llama, reset()/step()/
    // sendParameter() routan a través del backend y no se spawnea ningún
    // hijo. Pensado como ruta de A/B testing controlada por env var en
    // AppWindow.
    void setBackend(std::unique_ptr<scinodes::IComputeBackend> backend);
    bool hasExternalBackend() const { return m_backend != nullptr; }

    // (Re)start the subprocess with a fresh driver script for this graph.
    // Returns true on success. On failure, status()==Error and lastError()
    // explains why; existing ring buffers are cleared.
    bool reset(const NodeGraph& graph);
    // Variante para hot-reload: regenera el driver con seedState
    // (estados acumulados por (nodeId, slotIdx) + t) tomados de un
    // captureState previo.  Slots del nuevo plan cuya identidad no
    // está en el seed caen al IC del codegen (típicamente 0); slots
    // del seed que no existen en el nuevo plan se descartan.
    bool reset(const NodeGraph& graph, const CodegenSeedState& seed);

private:
    bool resetImpl(const NodeGraph& graph,
                   const CodegenSeedState* seed);
public:

    // Captura el estado actual del driver (t + vector de estado por
    // (nodeId, slot)).  Bloqueante: envía "dump_state" al subproceso y
    // espera STATE_BEGIN/END.  Devuelve false si el bridge no está
    // Ready/Running o si el backend in-process no expone esto.
    bool captureState(CodegenSeedState& out);

    // Terminate the child cleanly. No-op if not started.
    void stop();

    // Vacía los ring buffers, índices y el tiempo simulado SIN tocar el
    // proceso Scilab.  Lo usa AppWindow::simReset para que las gráficas
    // vuelvan a su estado inicial (t=0, sin trazas) cuando el usuario
    // pulsa Reset.  Si la simulación está corriendo, el solver thread
    // sigue activo y volverá a llenar los buffers desde cero.
    void clearBuffers();

    // Advance one timestep. Writes "step <t>" to the child and reads back
    // one "STATE …" line. Updates ring buffers. Returns false on protocol
    // error or solver divergence (status() becomes Error).
    //
    // Thread-safety: meant for SYNCHRONOUS use (one caller). When the
    // dedicated solver thread is running, AppWindow must not invoke step()
    // — the thread is the sole stepper.
    bool step(float dt);

    // ---- Dedicated solver thread ------------------------------------
    // Spawn a worker that calls step(dt) on a paced cadence using
    // std::chrono::steady_clock. The thread is the sole writer to the
    // Scilab pipe while it's alive; UI callers go through sendParameter
    // which queues into a pending list that the thread drains at step
    // boundaries.
    //
    // Returns false if a thread is already running, or if status() is
    // not Ready.
    bool startSolverThread(float dt);

    // Toggle stepping without joining. Cheap (atomic store).
    void setPaused(bool paused) { m_paused.store(paused); }
    bool isPaused()       const { return m_paused.load(); }
    bool isThreadRunning()const { return m_threadRunning.load(); }

    // Signal stop, wait, join. Safe to call when the thread isn't running.
    void stopSolverThread();

    // Live-tune a parameter. In synchronous mode the value is written
    // immediately; in threaded mode it is queued and applied at the next
    // step boundary (still atomic w.r.t. ODE integration).
    //
    // Two overloads:
    //   • (nodeId, paramIdx, value)       — top-level nodes (path = {nodeId}).
    //   • (path, paramIdx, value)         — para nodos dentro de SubGraphs.
    //     El path es la cadena [sgN_id, ..., child_id] desde el top-level
    //     hasta el nodo objetivo.  El bridge usa el `idForPath` del último
    //     plan generado para traducir al `flatNodeId` del script real.
    bool sendParameter(int nodeId, int paramIdx, double value);
    bool sendParameter(const std::vector<int>& path,
                       int paramIdx, double value);

    // ---- .sod export -------------------------------------------------
    // Ask the Scilab driver to write its accumulated history (t_hist +
    // every sink channel) to `path` in Scilab's native .sod (HDF5)
    // format. In threaded mode the request is queued and processed at
    // the next solver-loop boundary; in synchronous mode the write is
    // issued immediately. The path must not contain spaces — Scilab's
    // mfscanf reads a single whitespace-delimited token.
    //
    // Returns true if the request was accepted. The actual disk write
    // happens asynchronously; UI callers poll `takeLastExportResult()`
    // every frame to learn the outcome.
    bool exportSod(const std::string& path);

    // Pop the most-recent export result string (success or error). Returns
    // empty if no result is pending since the last call.
    std::string takeLastExportResult();

    // ---- accessors used by PlotPanel ---------------------------------
    // Snapshot of a sink's ring buffer. `channel` selects between the
    // outputs a multi-channel sink (e.g. PhasePortrait) emits;
    // single-channel sinks always use channel 0.
    std::vector<float> buffer(int sinkNodeId, int channel = 0) const;
    int                writeIndex(int sinkNodeId, int channel = 0) const;
    int                channelCount(int sinkNodeId) const;
    float              time() const { return m_publicTime.load(); }

    // The dt last passed to step()/startSolverThread(). 0 until the first
    // step. Used by exporters to reconstruct per-sample timestamps.
    float              solverDt() const { return m_dt.load(); }

    Status             status()        const { return m_status; }
    const std::string& lastError()     const { return m_lastError; }
    const std::string& driverScript()  const { return m_driverScript; }

    // Node id of the first node (in topo order) whose output went
    // non-finite (NaN or Inf) during the last step, or 0 if no
    // divergence has been detected. NodeCanvas paints this id with a
    // red title bar so the user can see exactly which block blew up.
    int offendingNodeId() const { return m_offendingNodeId.load(); }

private:
    // Spawn child with stdin/stdout redirected to pipes. Sends the
    // generated driver script and waits for the "READY\n" handshake.
    bool spawnScilab(const std::string& driverScript);
    void killChild();

    // Locate scilab-cli (env var override → common /opt paths → PATH).
    static std::string findScilabCli();

    // Pipe helpers — blocking line-oriented I/O.
    bool writeLine(const std::string& s);          // appends "\n"
    bool readLine(std::string& out, int timeoutMs);

    Status      m_status = Status::NotStarted;
    std::string m_lastError;
    std::string m_driverScript;     // the .sce text last sent (kept for inspection)

    int   m_childPid    = -1;
    int   m_toChildFd   = -1;       // we write here (= child's stdin)
    int   m_fromChildFd = -1;       // we read here (= child's stdout)

    // Sink channel layout (matches the STATE line emitted by the driver).
    // Each entry is (nodeId, channelIdx).
    struct SinkSlot { int nodeId; int channel; };
    std::vector<SinkSlot> m_sinkLayout;

    // Layout absoluto del vector de estado `x` que el driver maneja,
    // indexado por orden de slot 0..N-1 con su (nodeId, slot-en-el-nodo)
    // — viene del GeneratedPlan del último reset().  Lo usa
    // captureState() para asociar cada valor dumped a su identidad y
    // así sobrevivir reasignaciones de slots tras una edición del
    // grafo (hot-reload).
    std::vector<std::pair<int,int>> m_stateLayout;

    // Per-sink ring buffers — outer key is the sink node id, inner index
    // is the channel (0 = single-channel sinks, 0..N-1 for multi-channel).
    std::unordered_map<int, std::vector<std::vector<float>>> m_buffers;
    std::unordered_map<int, std::vector<int>>                m_writeIdx;
    float m_time = 0.0f;     // solver-thread-local view of simulated time

    // ---- threading -----------------------------------------------------
    // Protects: m_buffers, m_writeIdx, m_pendingParams.
    mutable std::mutex  m_mtx;
    std::thread         m_solver;
    std::atomic<bool>   m_threadRunning{ false };
    std::atomic<bool>   m_threadStop{ false };
    std::atomic<bool>   m_paused{ false };
    std::atomic<float>  m_publicTime{ 0.0f };
    std::atomic<float>  m_dt{ 0.0f };
    std::atomic<int>    m_offendingNodeId{ 0 };

    struct ParamUpdate { int nodeId; int paramIdx; double value; };
    std::vector<ParamUpdate> m_pendingParams;   // guarded by m_mtx

    // Cached path→flatId del último plan generado por reset(graph).  El
    // sendParameter por path lo consulta para traducir antes de queue/write.
    std::map<std::vector<int>, int> m_idForPath;

    // Export queue + last-result slot (both guarded by m_mtx).
    std::vector<std::string> m_pendingExports;
    std::string              m_lastExportResult;

    void solverLoop(float dt);
    bool writeParamLine(int nodeId, int paramIdx, double value);

    // Sends "save <path>" and reads back until SAVED/ERROR. Used both
    // from the solver thread (threaded mode) and synchronously.
    bool runExport(const std::string& path, std::string& outResult);

    // Backend in-process opcional. Si está set, todas las operaciones
    // numéricas routan a través de él en vez de pasar por el pipe.
    std::unique_ptr<scinodes::IComputeBackend> m_backend;

    // Helpers privados para la ruta "external backend".
    bool resetViaBackend(const NodeGraph& graph);
    bool stepViaBackend(float dt);
};
