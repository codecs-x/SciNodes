#pragma once
#include "NodeGraph.hpp"
#include <string>
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
    bool step(float dt);

    // Live-tune a parameter while the bridge is running. Writes
    // "param <nodeId> <paramIdx> <value>\n" to Scilab; the new value
    // takes effect at the next step boundary. paramIdx must match the
    // node's NodeDef::params order (the same index NodeCanvas uses for
    // its DragFloat widgets). No-op when the bridge isn't Ready.
    bool sendParameter(int nodeId, int paramIdx, double value);

    // ---- accessors used by PlotPanel ---------------------------------
    const std::vector<float>& buffer(int sinkNodeId) const;
    int  writeIndex(int sinkNodeId) const;
    float time() const { return m_time; }

    Status      status()     const { return m_status; }
    const std::string& lastError() const { return m_lastError; }
    const std::string& driverScript() const { return m_driverScript; }

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

    // Sink order in the state vector (matches the order the generator
    // emitted sinks in the driver script).
    std::vector<int> m_sinkOrder;

    std::unordered_map<int, std::vector<float>> m_buffers;
    std::unordered_map<int, int>                m_writeIdx;
    float m_time = 0.0f;

    static const std::vector<float> s_empty;
};
