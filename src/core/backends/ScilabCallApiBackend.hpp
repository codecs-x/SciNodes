#pragma once
#include "IComputeBackend.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scinodes {

// Implementación de IComputeBackend que embebe Scilab en el mismo proceso
// vía call_scilab.
//
// Constraints conocidos:
//   - La pila de Scilab es global y NO thread-safe. Una sola instancia de
//     ScilabCallApiBackend puede estar activa por proceso.
//   - StartScilab/TerminateScilab son del orden de segundos. No conviene
//     respawnear entre tests.
//   - El warning de Tcl es cosmético — se ignora.
//
// Para el costo de IPC ver experiments/call_scilab/motor_dc.cpp: medido en
// ~2.2 ms/tick con SendScilabJob por cada paso. Suficiente margen sobre
// el presupuesto de 16.67 ms del tick a 60 Hz.
class ScilabCallApiBackend : public IComputeBackend {
public:
    // SCIpath: ruta a `share/scilab` (usualmente <prefix>/share/scilab).
    // Si se pasa vacío, el constructor lee la variable de entorno SCI.
    explicit ScilabCallApiBackend(std::string sciPath = "");
    ~ScilabCallApiBackend() override;

    ScilabCallApiBackend(const ScilabCallApiBackend&)            = delete;
    ScilabCallApiBackend& operator=(const ScilabCallApiBackend&) = delete;

    bool prepare(const BackendPrepareSpec& spec) override;
    bool step(double                    dt,
              std::vector<SinkSample>&  outSamples,
              int*                      outOffendingNodeId) override;
    bool setParameter(int nodeId, int paramIdx, double value) override;
    bool exportHistory(const std::string& path, std::string* result) override;
    void shutdown() override;

    Status      status()    const override { return m_status; }
    std::string lastError() const override { return m_lastError; }

private:
    bool ensureStarted();
    bool runJob(const std::string& job);
    bool readScalar(const std::string& name, double* out);
    // Transferencias binarias (api_scilab) — sin string serialization en
    // el camino crítico.
    bool writeVector(const std::string& name, const std::vector<double>& v);
    bool readVector (const std::string& name, std::vector<double>& out);

    std::string m_sciPath;
    bool        m_scilabStarted = false;

    BackendPrepareSpec m_spec;
    double             m_t = 0.0;

    // Estado runtime — propiedad del backend, no de Scilab.  scn_step
    // recibe x_in / t_prev como argumentos y devuelve x_out + y; el
    // backend acumula la historia para exportHistory().
    std::vector<double>              m_state;
    double                           m_tPrev = 0.0;
    std::vector<double>              m_tHistory;
    std::vector<std::vector<double>> m_sinkHistory;

    // Mapeo (nodeId, paramIdx) → nombre de la variable Scilab para
    // setParameter rápido.
    std::unordered_map<long long, std::string> m_paramNames;

    Status      m_status = Status::NotStarted;
    std::string m_lastError;

    static long long key(int nodeId, int paramIdx) {
        return (static_cast<long long>(nodeId) << 32) |
               static_cast<unsigned int>(paramIdx);
    }
};

}  // namespace scinodes
