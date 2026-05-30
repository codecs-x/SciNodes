#include "ScilabBridge.hpp"
#include "ScilabCodeGen.hpp"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


// ===========================================================================
// findScilabCli
// ===========================================================================
std::string ScilabBridge::findScilabCli() {
    if (const char* env = std::getenv("SCN_SCILAB_PATH"); env && *env)
        return env;

    static const char* candidates[] = {
        "/opt/scilab-2026.0.1/bin/scilab-cli",
        "/opt/scilab/bin/scilab-cli",
        "/usr/bin/scilab-cli",
        "/usr/local/bin/scilab-cli",
    };
    for (const char* p : candidates)
        if (access(p, X_OK) == 0) return p;
    return "scilab-cli";   // hope PATH covers it
}

// ===========================================================================
// dtor / stop / killChild
// ===========================================================================
ScilabBridge::~ScilabBridge() { stopSolverThread(); stop(); }

void ScilabBridge::clearBuffers() {
    std::lock_guard<std::mutex> lock(m_mtx);
    for (auto& kv : m_buffers) {
        for (auto& ch : kv.second) ch.clear();
    }
    for (auto& kv : m_writeIdx) {
        for (auto& ch : kv.second) ch = 0;
    }
    m_time = 0.0f;
    m_publicTime.store(0.0f);
    m_offendingNodeId.store(0);
}

void ScilabBridge::stop() {
    stopSolverThread();
    if (m_backend) {
        // OJO: NO llamamos backend->shutdown() aquí.  TerminateScilab+
        // StartScilab en el mismo proceso falla por el JVM (limitación
        // del embedding).  El backend se apaga cuando la unique_ptr
        // muere con el bridge; entre resets solo limpia su workspace.
        m_status = Status::Stopped;
        return;
    }
    if (m_childPid < 0) return;
    if (m_toChildFd >= 0) {
        // Best effort — child may already be gone.
        const char* msg = "quit\n";
        (void)::write(m_toChildFd, msg, std::strlen(msg));
    }
    killChild();
    m_status = Status::Stopped;
}

void ScilabBridge::killChild() {
    if (m_toChildFd   >= 0) { ::close(m_toChildFd);   m_toChildFd   = -1; }
    if (m_fromChildFd >= 0) { ::close(m_fromChildFd); m_fromChildFd = -1; }
    if (m_childPid    >  0) {
        // Try graceful wait first, then SIGTERM, then SIGKILL.
        for (int i = 0; i < 30; ++i) {
            int st = 0;
            pid_t r = ::waitpid(m_childPid, &st, WNOHANG);
            if (r == m_childPid || r == -1) { m_childPid = -1; return; }
            ::usleep(20'000);
        }
        ::kill(m_childPid, SIGTERM);
        for (int i = 0; i < 25; ++i) {
            int st = 0;
            pid_t r = ::waitpid(m_childPid, &st, WNOHANG);
            if (r == m_childPid || r == -1) { m_childPid = -1; return; }
            ::usleep(20'000);
        }
        ::kill(m_childPid, SIGKILL);
        int st = 0;
        ::waitpid(m_childPid, &st, 0);
        m_childPid = -1;
    }
}

// ===========================================================================
// reset — kill any previous child, regenerate the driver, spawn anew
// ===========================================================================
bool ScilabBridge::reset(const NodeGraph& graph) {
    stop();
    m_status   = Status::NotStarted;
    m_lastError.clear();
    m_buffers.clear();
    m_writeIdx.clear();
    m_sinkLayout.clear();
    m_pendingParams.clear();
    m_pendingExports.clear();
    m_lastExportResult.clear();
    m_time = 0.0f;
    m_publicTime.store(0.0f);
    m_offendingNodeId.store(0);

    // Ruta alternativa: un backend externo (call_scilab u otro) maneja toda
    // la computación.  No se invoca generate() ni se hace fork.
    if (m_backend) return resetViaBackend(graph);

    auto plan = ScilabCodeGen::generate(graph);
    if (!plan.error.empty()) {
        m_lastError = plan.error;
        m_status    = Status::Error;
        return false;
    }
    m_driverScript = plan.script;
    m_idForPath    = plan.idForPath;       // path → flatId para live-tuning
    m_sinkLayout.clear();
    m_sinkLayout.reserve(plan.sinkChannels.size());
    for (const auto& sc : plan.sinkChannels)
        m_sinkLayout.push_back({ sc.nodeId, sc.channel });

    // Allocate per-channel ring buffers up front so accessors never
    // see a missing slot. Channel count for a sink = the highest
    // channel index seen in m_sinkLayout for that node, + 1.
    for (const auto& slot : m_sinkLayout) {
        auto& bufVec  = m_buffers[slot.nodeId];
        auto& idxVec  = m_writeIdx[slot.nodeId];
        if ((int)bufVec.size() <= slot.channel) {
            bufVec.resize(slot.channel + 1);
            idxVec.resize(slot.channel + 1, 0);
        }
        bufVec[slot.channel].clear();
        bufVec[slot.channel].reserve(DEFAULT_VISIBLE_SAMPLES * 4);
        idxVec[slot.channel] = 0;
    }

    // A graph with no sinks (or no sources) is not worth simulating —
    // pretend the bridge is "Ready" so the UI can still run/pause without
    // talking to Scilab. step() will become a no-op.
    if (m_sinkLayout.empty()) {
        m_status = Status::Ready;
        return true;
    }

    if (!spawnScilab(m_driverScript)) {
        m_status = Status::Error;
        return false;
    }
    m_status = Status::Ready;
    return true;
}

// ===========================================================================
// spawnScilab — fork + exec with bidirectional pipes
// ===========================================================================
bool ScilabBridge::spawnScilab(const std::string& driverScript) {
    // Write the driver script to a temp file (Scilab's -f loads from disk).
    char tmpl[] = "/tmp/scnodes_drv_XXXXXX.sce";
    int  scriptFd = ::mkstemps(tmpl, 4);
    if (scriptFd < 0) {
        m_lastError = std::string("mkstemps: ") + std::strerror(errno);
        return false;
    }
    ssize_t w = ::write(scriptFd, driverScript.data(), driverScript.size());
    ::close(scriptFd);
    if (w != (ssize_t)driverScript.size()) {
        m_lastError = "Failed to write driver script.";
        return false;
    }
    std::string scriptPath = tmpl;

    int inPipe[2]  = {-1, -1};  // parent → child stdin
    int outPipe[2] = {-1, -1};  // child stdout → parent
    if (::pipe(inPipe) < 0 || ::pipe(outPipe) < 0) {
        m_lastError = std::string("pipe: ") + std::strerror(errno);
        if (inPipe[0]  >= 0) ::close(inPipe[0]);
        if (inPipe[1]  >= 0) ::close(inPipe[1]);
        if (outPipe[0] >= 0) ::close(outPipe[0]);
        if (outPipe[1] >= 0) ::close(outPipe[1]);
        return false;
    }

    std::string sciPath = findScilabCli();

    pid_t pid = ::fork();
    if (pid < 0) {
        m_lastError = std::string("fork: ") + std::strerror(errno);
        ::close(inPipe[0]); ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);
        return false;
    }

    if (pid == 0) {
        // ---- child ------------------------------------------------------
        ::dup2(inPipe[0],  STDIN_FILENO);
        ::dup2(outPipe[1], STDOUT_FILENO);
        ::dup2(outPipe[1], STDERR_FILENO);
        ::close(inPipe[0]);  ::close(inPipe[1]);
        ::close(outPipe[0]); ::close(outPipe[1]);

        // Avoid the parent's signal mask interfering.
        ::signal(SIGPIPE, SIG_DFL);

        // scilab-cli -nb -nwni -f <script>
        // -nb: no banner, -nwni: terminal mode (no graphics).
        ::execl(sciPath.c_str(), sciPath.c_str(),
                "-nb", "-nwni", "-noatomsautoload",
                "-f", scriptPath.c_str(),
                (char*)nullptr);
        // exec failed
        std::perror("execl scilab-cli");
        std::_Exit(127);
    }

    // ---- parent ---------------------------------------------------------
    ::close(inPipe[0]);
    ::close(outPipe[1]);
    m_childPid    = pid;
    m_toChildFd   = inPipe[1];
    m_fromChildFd = outPipe[0];

    // Ignore SIGPIPE — writing to a dead child must yield EPIPE, not signal.
    ::signal(SIGPIPE, SIG_IGN);

    // Wait for the READY handshake. Scilab takes ~1-2 s to boot.
    std::string line;
    while (true) {
        if (!readLine(line, 15'000)) {
            m_lastError = "Scilab failed to emit READY within 15 s. "
                          "lastError: " + m_lastError;
            killChild();
            return false;
        }
        if (line.find("READY") != std::string::npos) break;
        // Anything else before READY is banner / module-load noise — drop it.
    }

    // Best-effort cleanup of the temp script file (Scilab already opened it).
    ::unlink(scriptPath.c_str());
    return true;
}

// ===========================================================================
// step — advance one tick and parse "STATE v1 v2 ... vN"
// ===========================================================================
bool ScilabBridge::step(float dt) {
    if (m_status == Status::Error || m_status == Status::Stopped) return false;
    m_time += dt;
    m_publicTime.store(m_time);
    m_dt.store(dt);

    // No sinks → nothing to do (still advance time for UI consistency).
    if (m_sinkLayout.empty()) return true;

    // Ruta backend externo: delegamos toda la computación al backend.
    if (m_backend) return stepViaBackend(dt);

    if (m_toChildFd < 0 || m_fromChildFd < 0) return false;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "step %.6f\n", m_time);
    if (!writeLine(buf)) {
        m_status    = Status::Error;
        m_lastError = "Pipe write failed (child likely died).";
        return false;
    }

    std::string line;
    // El primer step típicamente trae el coste de cargar Scilab (varios
    // segundos en Scilab 2026 + ode("rk") + módulos sciSparse).  Los
    // pasos posteriores son del orden de ms.  Damos hasta 60 s en
    // total, pero poleamos en chunks de 200 ms para que el solver
    // thread pueda atender m_threadStop (= Reset/Stop del usuario)
    // sin bloquear el join.
    constexpr int kTotalTimeoutMs = 60'000;
    constexpr int kChunkMs        =    200;
    int           elapsedMs       = 0;
    while (true) {
        if (m_threadStop.load()) {
            m_lastError = "step cancelado por Stop/Reset.";
            return false;
        }
        const bool got = readLine(line, kChunkMs);
        if (!got) {
            // ¿chunk timeout o error real?  Distinguimos por
            // m_lastError; "read timeout" significa "el chunk venció,
            // sigamos esperando hasta el total".
            if (m_lastError == "read timeout") {
                elapsedMs += kChunkMs;
                if (elapsedMs >= kTotalTimeoutMs) {
                    m_status    = Status::Error;
                    m_lastError = "Scilab no respondió en 60 s al "
                                  "comando step.  El subproceso "
                                  "scilab-cli puede haberse colgado.";
                    return false;
                }
                continue;
            }
            // Error genuino (pipe cerrada, etc.).
            m_status = Status::Error;
            return false;
        }
        // Skip noise; only "STATE …" lines carry our payload.
        if (line.rfind("STATE", 0) == 0) break;
        if (line.rfind("ERROR", 0) == 0) {
            m_status    = Status::Error;
            m_lastError = line;
            return false;
        }
    }

    // Parse "STATE <nanid> v1 v2 ... vN". nanid is the id of the first
    // node (in topo order) whose output became NaN/Inf during the step,
    // or 0 if the step finished cleanly. If nanid > 0 we don't even try
    // to parse the trailing sink values — Scilab prints them as the
    // literal "Nan", which std::istringstream's double parser rejects.
    std::istringstream ss(line);
    std::string tag; ss >> tag;   // "STATE"
    int nanid = 0;
    if (!(ss >> nanid)) {
        m_status    = Status::Error;
        m_lastError = "Malformed STATE line: '" + line + "'";
        return false;
    }
    if (nanid > 0) {
        m_offendingNodeId.store(nanid);
        m_status    = Status::Error;
        m_lastError = "Solver produced NaN/Inf at node "
                    + std::to_string(nanid) + ".";
        return false;
    }
    std::vector<float> samples(m_sinkLayout.size(), 0.0f);
    for (size_t i = 0; i < m_sinkLayout.size(); ++i) {
        double v = 0.0;
        if (!(ss >> v)) {
            m_status    = Status::Error;
            m_lastError = "Truncated STATE line: '" + line + "'";
            return false;
        }
        samples[i] = static_cast<float>(v);
    }
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        for (size_t i = 0; i < m_sinkLayout.size(); ++i) {
            const SinkSlot& slot = m_sinkLayout[i];
            auto& rb = m_buffers[slot.nodeId][slot.channel];
            int&  ix = m_writeIdx[slot.nodeId][slot.channel];
            rb.push_back(samples[i]);
            ++ix;
        }
    }
    return true;
}

// ===========================================================================
// sendParameter — live tuning.
//
// • Synchronous mode (no solver thread): writes directly to the pipe.
// • Threaded mode: queues the update; the solver thread drains the queue
//   at the next step boundary, so updates are still atomic w.r.t. ODE
//   integration but UI callers never touch the pipe.
// ===========================================================================
bool ScilabBridge::sendParameter(int nodeId, int paramIdx, double value) {
    if (m_status != Status::Ready && m_status != Status::Running) return false;

    // Ruta backend externo: la cola y el mutex siguen siendo del bridge;
    // solo cambia el destinatario del cambio.
    if (m_backend) {
        if (m_threadRunning.load()) {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_pendingParams.push_back({ nodeId, paramIdx, value });
            return true;
        }
        return m_backend->setParameter(nodeId, paramIdx, value);
    }

    if (m_toChildFd < 0) return false;

    if (m_threadRunning.load()) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_pendingParams.push_back({ nodeId, paramIdx, value });
        return true;
    }
    return writeParamLine(nodeId, paramIdx, value);
}

// Overload por path: traduce a flatId vía el mapa cacheado del último
// plan.  Si el path no se encuentra (p. ej., el grafo cambió tras un
// reset incompleto), se ignora silenciosamente para no bloquear la GUI.
bool ScilabBridge::sendParameter(const std::vector<int>& path,
                                 int paramIdx, double value) {
    auto it = m_idForPath.find(path);
    if (it == m_idForPath.end()) return false;
    return sendParameter(it->second, paramIdx, value);
}

bool ScilabBridge::writeParamLine(int nodeId, int paramIdx, double value) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "param %d %d %.10g\n",
                  nodeId, paramIdx, value);
    if (!writeLine(buf)) {
        m_status    = Status::Error;
        m_lastError = "Pipe write failed during sendParameter.";
        return false;
    }
    return true;
}

// ===========================================================================
// .sod export — queued in threaded mode, immediate in synchronous mode.
// ===========================================================================
bool ScilabBridge::exportSod(const std::string& path) {
    if (m_status != Status::Ready && m_status != Status::Running) return false;

    // Compartido entre ambos backends: rechazar paths con espacios para
    // simetría con la restricción del subprocess (mfscanf %s).
    if (path.find(' ') != std::string::npos) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_lastExportResult = "SOD export failed: path must not contain spaces ("
                           + path + ")";
        return false;
    }

    if (m_threadRunning.load()) {
        // En modo threaded la solicitud se encola; el solver loop la drena
        // y, según haya backend o no, llama exportHistory o runExport.
        std::lock_guard<std::mutex> lock(m_mtx);
        m_pendingExports.push_back(path);
        return true;
    }

    // Modo síncrono.
    std::string result;
    bool ok;
    if (m_backend) {
        ok = m_backend->exportHistory(path, &result);
    } else {
        if (m_toChildFd < 0) return false;
        ok = runExport(path, result);
    }
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_lastExportResult = std::move(result);
    }
    return ok;
}

std::string ScilabBridge::takeLastExportResult() {
    std::lock_guard<std::mutex> lock(m_mtx);
    std::string out = std::move(m_lastExportResult);
    m_lastExportResult.clear();
    return out;
}

// runExport — does the actual save protocol: write "save <path>", then
// read lines until SAVED/ERROR (or timeout). Caller is responsible for
// holding/releasing m_mtx if cross-thread coordination is needed.
bool ScilabBridge::runExport(const std::string& path, std::string& outResult) {
    std::string cmd = "save " + path + "\n";
    if (!writeLine(cmd)) {
        outResult = "SOD export failed: pipe write error.";
        m_status    = Status::Error;
        m_lastError = outResult;
        return false;
    }

    std::string line;
    for (int i = 0; i < 50; ++i) {           // up to ~50 lines of slack
        if (!readLine(line, 10'000)) {       // 10 s allowance for save()
            outResult = "SOD export failed: no response from Scilab.";
            return false;
        }
        if (line.rfind("SAVED", 0) == 0) {
            outResult = "Exported to " + path;
            return true;
        }
        if (line.rfind("ERROR", 0) == 0) {
            outResult = "SOD export failed: " + line;
            return false;
        }
        // Anything else (warnings, banner remnants) — keep reading.
    }
    outResult = "SOD export failed: too many lines without SAVED.";
    return false;
}

// ===========================================================================
// Accessors — return mutex-protected snapshots so UI readers never tear
// against solver-thread writers.
// ===========================================================================
std::vector<float> ScilabBridge::buffer(int sinkNodeId, int channel) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_buffers.find(sinkNodeId);
    if (it == m_buffers.end() || channel < 0 ||
        channel >= (int)it->second.size())
        return {};
    return it->second[channel];
}
int ScilabBridge::writeIndex(int sinkNodeId, int channel) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_writeIdx.find(sinkNodeId);
    if (it == m_writeIdx.end() || channel < 0 ||
        channel >= (int)it->second.size())
        return 0;
    return it->second[channel];
}
int ScilabBridge::channelCount(int sinkNodeId) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_buffers.find(sinkNodeId);
    return (it != m_buffers.end()) ? (int)it->second.size() : 0;
}

// ===========================================================================
// Solver thread — drives step(dt) at a paced cadence using
// std::chrono::steady_clock. The thread is the only writer to the pipe
// while it's alive; sendParameter() queues from UI threads.
// ===========================================================================
bool ScilabBridge::startSolverThread(float dt) {
    if (m_threadRunning.load()) return false;
    if (m_status != Status::Ready) return false;
    m_threadStop.store(false);
    m_paused.store(false);
    m_threadRunning.store(true);
    m_dt.store(dt);
    m_solver = std::thread([this, dt]{ solverLoop(dt); });
    return true;
}

void ScilabBridge::stopSolverThread() {
    if (!m_threadRunning.load() && !m_solver.joinable()) return;
    m_threadStop.store(true);
    if (m_solver.joinable()) m_solver.join();
    m_threadRunning.store(false);
}

void ScilabBridge::solverLoop(float dt) {
    using clock = std::chrono::steady_clock;
    const auto tickNs = std::chrono::nanoseconds(int64_t(dt * 1e9));
    auto next = clock::now();

    while (!m_threadStop.load()) {
        // Drain pending param updates and export requests at step
        // boundaries (or pause boundaries). Both modify the pipe, so
        // they must run sequentially with step() — never concurrently.
        std::vector<ParamUpdate> updates;
        std::vector<std::string> exports;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            updates.swap(m_pendingParams);
            exports.swap(m_pendingExports);
        }
        for (const auto& u : updates) {
            bool ok = m_backend
                ? m_backend->setParameter(u.nodeId, u.paramIdx, u.value)
                : writeParamLine(u.nodeId, u.paramIdx, u.value);
            if (!ok) break;
        }
        for (const auto& p : exports) {
            std::string result;
            if (m_backend) {
                (void)m_backend->exportHistory(p, &result);
            } else {
                (void)runExport(p, result);
            }
            std::lock_guard<std::mutex> lock(m_mtx);
            m_lastExportResult = std::move(result);
        }

        if (!m_paused.load()) {
            if (!step(dt)) break;       // step() sets Error status on failure
        }
        next += tickNs;
        auto now = clock::now();
        if (now < next) std::this_thread::sleep_until(next);
        else            next = now;     // we're behind; don't accumulate slack
    }
    m_threadRunning.store(false);
}

// ===========================================================================
// Pipe primitives
// ===========================================================================
bool ScilabBridge::writeLine(const std::string& s) {
    if (m_toChildFd < 0) return false;
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::write(m_toChildFd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p    += n;
        left -= (size_t)n;
    }
    return true;
}

bool ScilabBridge::readLine(std::string& out, int timeoutMs) {
    out.clear();
    if (m_fromChildFd < 0) return false;

    char c;
    while (true) {
        pollfd pfd{ m_fromChildFd, POLLIN, 0 };
        int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr == 0)   { m_lastError = "read timeout"; return false; }
        if (pr  < 0)   { if (errno == EINTR) continue;
                         m_lastError = std::string("poll: ") + std::strerror(errno);
                         return false; }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            m_lastError = "child closed pipe";
            return false;
        }

        ssize_t n = ::read(m_fromChildFd, &c, 1);
        if (n == 0) { m_lastError = "EOF on child stdout"; return false; }
        if (n  < 0) {
            if (errno == EINTR) continue;
            m_lastError = std::string("read: ") + std::strerror(errno);
            return false;
        }
        if (c == '\n') return true;
        if (c != '\r') out.push_back(c);
    }
}

// ===========================================================================
// External-backend path: setBackend + helpers
// ===========================================================================
void ScilabBridge::setBackend(std::unique_ptr<scinodes::IComputeBackend> b) {
    // Si había uno antes, asegurarse de apagarlo antes de soltar el handle.
    if (m_backend) m_backend->shutdown();
    m_backend = std::move(b);
}

bool ScilabBridge::resetViaBackend(const NodeGraph& graph) {
    // Generar spec estructurada (mismo motor de planeación que el path
    // subproceso, pero sin emitir el while-loop del REPL).
    auto gs = ScilabCodeGen::generateSpec(graph);
    if (!gs.error.empty()) {
        m_lastError = gs.error;
        m_status    = Status::Error;
        return false;
    }

    // Layout de sumideros + ring buffers — idéntico al path subproceso.
    m_sinkLayout.clear();
    m_sinkLayout.reserve(gs.spec.sinkChannels.size());
    for (const auto& sc : gs.spec.sinkChannels)
        m_sinkLayout.push_back({ sc.nodeId, sc.channel });
    for (const auto& slot : m_sinkLayout) {
        auto& bufVec = m_buffers[slot.nodeId];
        auto& idxVec = m_writeIdx[slot.nodeId];
        if ((int)bufVec.size() <= slot.channel) {
            bufVec.resize(slot.channel + 1);
            idxVec.resize(slot.channel + 1, 0);
        }
        bufVec[slot.channel].clear();
        bufVec[slot.channel].reserve(DEFAULT_VISIBLE_SAMPLES * 4);
        idxVec[slot.channel] = 0;
    }

    // Sin sumideros, igual que el path subproceso: la simulación se queda
    // viva sin trabajo útil.
    if (m_sinkLayout.empty()) {
        m_status = Status::Ready;
        return true;
    }

    if (!m_backend->prepare(gs.spec)) {
        m_lastError = m_backend->lastError();
        m_status    = Status::Error;
        return false;
    }

    m_status = Status::Ready;
    return true;
}

bool ScilabBridge::stepViaBackend(float dt) {
    std::vector<scinodes::SinkSample> samples;
    int offending = 0;
    if (!m_backend->step(dt, samples, &offending)) {
        m_status    = Status::Error;
        m_lastError = m_backend->lastError();
        return false;
    }
    if (offending > 0) {
        m_offendingNodeId.store(offending);
        m_status    = Status::Error;
        m_lastError = "Solver produced NaN/Inf at node "
                    + std::to_string(offending) + ".";
        return false;
    }

    // Volcar al buffer acumulativo (mismo backend que el path subproceso):
    // SinkSample.{nodeId, channel} mapea directo a m_buffers / m_writeIdx.
    std::lock_guard<std::mutex> lock(m_mtx);
    for (const auto& s : samples) {
        auto& rb = m_buffers[s.nodeId][s.channel];
        int&  ix = m_writeIdx[s.nodeId][s.channel];
        rb.push_back(static_cast<float>(s.value));
        ++ix;
    }
    return true;
}
