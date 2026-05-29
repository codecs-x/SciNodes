#pragma once
#include "NodeGraph.hpp"
#include <atomic>
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
    static constexpr int BUFFER_SIZE = 512;     // per-sink ring-buffer length

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

    // (Re)start the subprocess with a fresh driver script for this graph.
    // Returns true on success. On failure, status()==Error and lastError()
    // explains why; existing ring buffers are cleared.
    bool reset(const NodeGraph& graph);

    // Terminate the child cleanly. No-op if not started.
    void stop();

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
    bool sendParameter(int nodeId, int paramIdx, double value);

    // ---- accessors used by PlotPanel ---------------------------------
    // Snapshot of a sink's ring buffer. `channel` selects between the
    // outputs a multi-channel sink (e.g. PhasePortrait) emits;
    // single-channel sinks always use channel 0.
    std::vector<float> buffer(int sinkNodeId, int channel = 0) const;
    int                writeIndex(int sinkNodeId, int channel = 0) const;
    int                channelCount(int sinkNodeId) const;
    float              time() const { return m_publicTime.load(); }

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
    std::atomic<int>    m_offendingNodeId{ 0 };

    struct ParamUpdate { int nodeId; int paramIdx; double value; };
    std::vector<ParamUpdate> m_pendingParams;   // guarded by m_mtx

    void solverLoop(float dt);
    bool writeParamLine(int nodeId, int paramIdx, double value);
};
