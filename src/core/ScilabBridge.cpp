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

void ScilabBridge::stop() {
    stopSolverThread();
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
    m_time = 0.0f;
    m_publicTime.store(0.0f);
    m_offendingNodeId.store(0);

    auto plan = ScilabCodeGen::generate(graph);
    if (!plan.error.empty()) {
        m_lastError = plan.error;
        m_status    = Status::Error;
        return false;
    }
    m_driverScript = plan.script;
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
        bufVec[slot.channel].assign(BUFFER_SIZE, 0.0f);
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

    // No sinks → nothing to do (still advance time for UI consistency).
    if (m_sinkLayout.empty()) return true;
    if (m_toChildFd < 0 || m_fromChildFd < 0) return false;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "step %.6f\n", m_time);
    if (!writeLine(buf)) {
        m_status    = Status::Error;
        m_lastError = "Pipe write failed (child likely died).";
        return false;
    }

    std::string line;
    while (true) {
        if (!readLine(line, 5'000)) {
            m_status    = Status::Error;
            m_lastError = "Scilab did not respond within 5 s of step.";
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
            rb[ix % BUFFER_SIZE] = samples[i];
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
    if (m_toChildFd < 0) return false;

    if (m_threadRunning.load()) {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_pendingParams.push_back({ nodeId, paramIdx, value });
        return true;
    }
    return writeParamLine(nodeId, paramIdx, value);
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
        if (!m_paused.load()) {
            // Drain pending param updates first so they take effect at this
            // step boundary (matches sendParameter() docs).
            std::vector<ParamUpdate> updates;
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                updates.swap(m_pendingParams);
            }
            for (const auto& u : updates)
                if (!writeParamLine(u.nodeId, u.paramIdx, u.value)) break;

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
