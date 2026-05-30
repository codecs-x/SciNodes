#include "ScilabCallApiBackend.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>

extern "C" {
    #include <call_scilab.h>
    #include <api_scilab.h>
    #include <api_stack_double.h>
}

namespace scinodes {

// Naming compatible con el subprocess: `v<id>_hist` o `v<id>_<channel>_hist`.
// Permite que un .sod producido por este backend se consuma con los mismos
// nombres de variable que produce el subprocess (ej. para CSV externo).
static std::string histVarName(const BackendPrepareSpec::SinkChannel& sc) {
    std::ostringstream os;
    os << "v" << sc.nodeId;
    if (sc.channel > 0) os << "_" << sc.channel;
    os << "_hist";
    return os.str();
}

ScilabCallApiBackend::ScilabCallApiBackend(std::string sciPath)
    : m_sciPath(std::move(sciPath)) {
    if (m_sciPath.empty()) {
        if (const char* env = std::getenv("SCI")) {
            m_sciPath = env;
        }
    }
}

ScilabCallApiBackend::~ScilabCallApiBackend() {
    shutdown();
}

bool ScilabCallApiBackend::ensureStarted() {
    if (m_scilabStarted) return true;
    if (m_sciPath.empty()) {
        m_status    = Status::Error;
        m_lastError = "SCI no definido (variable de entorno o constructor).";
        return false;
    }
    if (!StartScilab(const_cast<char*>(m_sciPath.c_str()), nullptr, 0)) {
        m_status    = Status::Error;
        m_lastError = "StartScilab() retornó FALSE.";
        return false;
    }
    m_scilabStarted = true;
    return true;
}

bool ScilabCallApiBackend::runJob(const std::string& job) {
    if (SendScilabJob(const_cast<char*>(job.c_str())) != 0) {
        m_status    = Status::Error;
        m_lastError = "SendScilabJob falló: " + job.substr(0, 80);
        return false;
    }
    return true;
}

bool ScilabCallApiBackend::readScalar(const std::string& name, double* out) {
    int rc = getNamedScalarDouble(nullptr,
                                  const_cast<char*>(name.c_str()),
                                  out);
    if (rc != 0) {
        m_status    = Status::Error;
        m_lastError = "getNamedScalarDouble(\"" + name + "\") falló.";
        return false;
    }
    return true;
}

bool ScilabCallApiBackend::writeVector(const std::string&         name,
                                       const std::vector<double>& v) {
    SciErr err = createNamedMatrixOfDouble(
        nullptr,
        const_cast<char*>(name.c_str()),
        static_cast<int>(v.size()), 1,
        v.empty() ? nullptr : v.data());
    if (err.iErr != 0) {
        m_status    = Status::Error;
        m_lastError = "createNamedMatrixOfDouble(\"" + name + "\") falló.";
        return false;
    }
    return true;
}

bool ScilabCallApiBackend::readVector(const std::string&   name,
                                      std::vector<double>& out) {
    int rows = 0, cols = 0;
    SciErr err = readNamedMatrixOfDouble(
        nullptr, const_cast<char*>(name.c_str()),
        &rows, &cols, nullptr);
    if (err.iErr != 0) {
        m_status    = Status::Error;
        m_lastError = "readNamedMatrixOfDouble(\"" + name + "\") dims falló.";
        return false;
    }
    out.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), 0.0);
    if (out.empty()) return true;
    err = readNamedMatrixOfDouble(
        nullptr, const_cast<char*>(name.c_str()),
        &rows, &cols, out.data());
    if (err.iErr != 0) {
        m_status    = Status::Error;
        m_lastError = "readNamedMatrixOfDouble(\"" + name + "\") data falló.";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// prepare — siembra params (con `global p_X_Y; p_X_Y = init;` para que
// dynamics() y scn_step() los vean), define dynamics + scn_step una vez,
// y resetea el estado runtime que el backend posee en C++.
// ---------------------------------------------------------------------------
bool ScilabCallApiBackend::prepare(const BackendPrepareSpec& spec) {
    if (!ensureStarted()) return false;

    m_spec      = spec;
    m_t         = 0.0;
    m_tPrev     = 0.0;
    m_paramNames.clear();
    m_status    = Status::NotStarted;
    m_lastError.clear();

    // Inicializar estado y buffers de historia en C++.
    if (spec.stateSize > 0) {
        m_state.assign(spec.stateSize, 0.0);
        for (size_t i = 0; i < spec.initialState.size() &&
                           i < m_state.size(); ++i) {
            m_state[i] = spec.initialState[i];
        }
    } else {
        m_state.clear();
    }
    m_tHistory.clear();
    m_sinkHistory.assign(spec.sinkChannels.size(), {});

    // Limpiar workspace de cualquier sesión anterior.
    if (!runJob("clear")) return false;

    // 1) Sembrar los parámetros como globales del workspace.  dynamics() y
    //    scn_step() los declaran `global p_X_Y` adentro; la asignación
    //    inicial debe hacerse en una línea que declare la misma global
    //    antes del igual para que ambos referencien el mismo símbolo.
    for (const auto& p : spec.params) {
        std::ostringstream os;
        os << "global " << p.scilabName << "; "
           << p.scilabName << " = " << p.initialValue << ";";
        if (!runJob(os.str())) return false;
        m_paramNames[key(p.nodeId, p.paramIdx)] = p.scilabName;
    }

    // 2) Definir dynamics(t, x) si hay estados.  El codegen ya emite
    //    `global p_X_Y` dentro del cuerpo.
    if (!spec.dynamicsFunction.empty()) {
        if (!runJob(spec.dynamicsFunction)) return false;
    }

    // 3) Definir scn_step(t_new, t_prev, x_in) — almacenada una sola vez.
    //    Cada step() del backend la invoca con una línea fija; Scilab no
    //    reparsea el cuerpo en cada tick (esa era la causa del lag del
    //    path antiguo que mandaba el outputEvalScript completo por tick).
    if (!spec.stepFunction.empty()) {
        if (!runJob(spec.stepFunction)) return false;
    }

    m_status = Status::Ready;
    return true;
}

// ---------------------------------------------------------------------------
// step — empuja x_in binario, invoca scn_step, lee x_out + y binarios.
// Toda la persistencia (x, t_prev, historia) vive en C++.
// ---------------------------------------------------------------------------
bool ScilabCallApiBackend::step(double                   dt,
                                std::vector<SinkSample>& outSamples,
                                int*                     outOffendingNodeId) {
    if (m_status != Status::Ready && m_status != Status::Running) {
        m_lastError = "step() llamado en estado inválido.";
        return false;
    }
    if (outOffendingNodeId) *outOffendingNodeId = 0;

    m_status = Status::Running;
    m_t += dt;

    if (m_spec.stepFunction.empty()) {
        m_status    = Status::Error;
        m_lastError = "scn_step no definida (spec.stepFunction vacía).";
        return false;
    }

    // (1) Empujar x_in al workspace en binario.
    if (!writeVector("__x_in", m_state)) return false;

    // (2) Despachar scn_step — línea fija, Scilab no reparsea el cuerpo.
    {
        std::ostringstream call;
        call << "[__x_out, __y] = scn_step(" << m_t
             << ", " << m_tPrev << ", __x_in);";
        if (!runJob(call.str())) return false;
    }

    // (3) Leer x_out y y binarios.
    if (m_spec.stateSize > 0) {
        if (!readVector("__x_out", m_state)) return false;
    }
    std::vector<double> y;
    if (!m_spec.sinkChannels.empty()) {
        if (!readVector("__y", y)) return false;
        if (y.size() < m_spec.sinkChannels.size()) {
            m_status    = Status::Error;
            m_lastError = "scn_step devolvió menos valores que sinkChannels.";
            return false;
        }
    }

    // (4) Avanzar reloj + appendear historia + producir samples.
    m_tPrev = m_t;
    m_tHistory.push_back(m_t);

    outSamples.clear();
    outSamples.reserve(m_spec.sinkChannels.size());
    for (size_t i = 0; i < m_spec.sinkChannels.size(); ++i) {
        const auto& sc = m_spec.sinkChannels[i];
        const double v = y[i];
        m_sinkHistory[i].push_back(v);

        if (outOffendingNodeId && *outOffendingNodeId == 0
            && (std::isnan(v) || std::isinf(v))) {
            *outOffendingNodeId = sc.nodeId;
        }
        outSamples.push_back({ sc.nodeId, sc.channel, v });
    }

    m_status = Status::Ready;
    return true;
}

// ---------------------------------------------------------------------------
// setParameter — escritura inmediata al workspace global.  Una sola
// SendScilabJob; la próxima invocación de scn_step / dynamics lee el
// nuevo valor a través de su declaración `global p_X_Y`.
// ---------------------------------------------------------------------------
bool ScilabCallApiBackend::setParameter(int    nodeId,
                                        int    paramIdx,
                                        double value) {
    auto it = m_paramNames.find(key(nodeId, paramIdx));
    if (it == m_paramNames.end()) {
        m_lastError = "Parámetro desconocido en setParameter.";
        return false;
    }
    std::ostringstream os;
    os << "global " << it->second << "; "
       << it->second << " = " << value << ";";
    return runJob(os.str());
}

// ---------------------------------------------------------------------------
// exportHistory — sube la historia C++ a Scilab y deja que save() haga el
// formato .sod (compatible con el subprocess).
// ---------------------------------------------------------------------------
bool ScilabCallApiBackend::exportHistory(const std::string& path,
                                         std::string*       result) {
    if (m_status != Status::Ready && m_status != Status::Running) {
        if (result) *result = "ERROR backend no listo";
        return false;
    }
    if (path.empty()) {
        if (result) *result = "ERROR ruta vacía";
        return false;
    }
    if (path.find(' ') != std::string::npos) {
        if (result) *result = "ERROR la ruta no puede contener espacios";
        return false;
    }

    if (!writeVector("t_hist", m_tHistory)) {
        if (result) *result = "ERROR " + m_lastError;
        return false;
    }
    for (size_t i = 0; i < m_spec.sinkChannels.size(); ++i) {
        if (!writeVector(histVarName(m_spec.sinkChannels[i]),
                         m_sinkHistory[i])) {
            if (result) *result = "ERROR " + m_lastError;
            return false;
        }
    }

    std::ostringstream save;
    save << "save(\"" << path << "\", \"t_hist\"";
    for (const auto& sc : m_spec.sinkChannels) {
        save << ", \"" << histVarName(sc) << "\"";
    }
    save << ");";

    if (!runJob(save.str())) {
        if (result) *result = "ERROR " + m_lastError;
        return false;
    }
    if (result) *result = "SAVED " + path;
    return true;
}

void ScilabCallApiBackend::shutdown() {
    if (m_scilabStarted) {
        TerminateScilab(nullptr);
        m_scilabStarted = false;
    }
    m_status = Status::Stopped;
}

}  // namespace scinodes
