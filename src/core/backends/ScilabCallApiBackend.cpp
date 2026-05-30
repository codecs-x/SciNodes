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

bool ScilabCallApiBackend::prepare(const BackendPrepareSpec& spec) {
    if (!ensureStarted()) return false;

    m_spec = spec;
    m_t    = 0.0;
    m_paramNames.clear();
    m_status    = Status::NotStarted;
    m_lastError.clear();

    // Limpiar workspace de cualquier prepare anterior: variables p_*,
    // v*, x, t, t_prev y la función dynamics.  En Scilab `clear` borra
    // variables y funciones de usuario\;  no toca built-ins.
    if (!runJob("clear")) return false;

    // 1) Declarar cada parámetro vivo como variable global de Scilab.
    //    El cuerpo de la función `dynamics` los referenciará por nombre.
    for (const auto& p : spec.params) {
        std::ostringstream os;
        os << p.scilabName << " = " << p.initialValue << ";";
        if (!runJob(os.str())) return false;
        m_paramNames[key(p.nodeId, p.paramIdx)] = p.scilabName;
    }

    // 2) Definir la función dynamics, si hay estados integrados.
    if (!spec.dynamicsFunction.empty()) {
        if (!runJob(spec.dynamicsFunction)) return false;
    }

    // 3) Inicializar el vector de estado.
    if (spec.stateSize > 0) {
        std::ostringstream os;
        os << "x = zeros(" << spec.stateSize << ", 1);";
        if (!spec.initialState.empty()) {
            // Carga elemento a elemento para no depender del parseo de
            // vectores literales largos.
            for (size_t i = 0; i < spec.initialState.size(); ++i) {
                os << " x(" << (i + 1) << ") = "
                   << spec.initialState[i] << ";";
            }
        }
        os << " t_prev = 0;";
        if (!runJob(os.str())) return false;
    }

    // 4) Buffers de historia para export .sod. Naming compatible con el
    //    subprocess (t_hist + v<id>_hist por sumidero).
    {
        std::ostringstream os;
        os << "t_hist = [];";
        for (const auto& sc : spec.sinkChannels) {
            os << " " << histVarName(sc) << " = [];";
        }
        if (!runJob(os.str())) return false;
    }

    m_status = Status::Ready;
    return true;
}

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

    // 1) Construir un único job que: (a) integra (si hay estados),
    //    (b) recomputa las variables intermedias del grafo
    //    (outputEvalScript), y (c) asigna cada sumidero a una variable
    //    __y<i>.  Una sola llamada a SendScilabJob es mucho más barata
    //    que una por cada canal.
    std::ostringstream step;
    if (m_spec.stateSize > 0 && !m_spec.dynamicsFunction.empty()) {
        step << "x = ode(\"rk\", x, t_prev, " << m_t
             << ", dynamics); t_prev = " << m_t << ";\n";
    }
    // Las expresiones de sumideros pueden referenciar `t` (ej. StepSignal
    // emite "(t >= t0) * amp").  En el path subproceso `t` queda definido
    // por el mfscanf del driver\;  embebido tenemos que asignarlo antes
    // de correr el topo-eval o el sink ve un símbolo no definido.
    step << "t = " << m_t << ";\n";
    if (!m_spec.outputEvalScript.empty()) {
        step << m_spec.outputEvalScript;
        if (m_spec.outputEvalScript.back() != '\n') step << '\n';
    }
    for (size_t i = 0; i < m_spec.sinkChannels.size(); ++i) {
        step << "__y" << i << " = " << m_spec.sinkChannels[i].expression
             << ";\n";
    }
    // Apendizar a los buffers de historia.  Una asignación por sumidero;
    // sigue dentro del mismo SendScilabJob para no sumar roundtrips.
    step << "t_hist($+1) = " << m_t << ";\n";
    for (size_t i = 0; i < m_spec.sinkChannels.size(); ++i) {
        step << histVarName(m_spec.sinkChannels[i])
             << "($+1) = __y" << i << ";\n";
    }
    if (!step.str().empty()) {
        if (!runJob(step.str())) return false;
    }

    // 2) Leer cada __y<i> de vuelta a C++.
    outSamples.clear();
    outSamples.reserve(m_spec.sinkChannels.size());

    for (size_t i = 0; i < m_spec.sinkChannels.size(); ++i) {
        const auto&  sc = m_spec.sinkChannels[i];
        double       v  = 0.0;
        std::string  vn = "__y" + std::to_string(i);
        if (!readScalar(vn, &v)) return false;

        if (outOffendingNodeId && *outOffendingNodeId == 0
            && (std::isnan(v) || std::isinf(v))) {
            *outOffendingNodeId = sc.nodeId;
        }

        outSamples.push_back({ sc.nodeId, sc.channel, v });
    }

    m_status = Status::Ready;
    return true;
}

bool ScilabCallApiBackend::setParameter(int    nodeId,
                                        int    paramIdx,
                                        double value) {
    auto it = m_paramNames.find(key(nodeId, paramIdx));
    if (it == m_paramNames.end()) {
        m_lastError = "Parámetro desconocido en setParameter.";
        return false;
    }
    std::ostringstream os;
    os << it->second << " = " << value << ";";
    return runJob(os.str());
}

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
    // Scilab's mfscanf en el path subprocess reservaba el primer token sin
    // espacios; aquí podríamos aceptar rutas con espacios, pero por
    // simetría con el subprocess mantenemos la misma restricción.
    if (path.find(' ') != std::string::npos) {
        if (result) *result = "ERROR la ruta no puede contener espacios";
        return false;
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
