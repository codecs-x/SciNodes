// =============================================================================
// make_pid_scn.cpp — Construye el grafo del walkthrough D1 (PID + motor DC)
// y lo guarda como /tmp/walkthrough_pid.scn para que el usuario pueda
// abrirlo con File → Open en SciNodes.
//
// Útil para probar la GUI con SCINODES_BACKEND=callapi sin tener que
// re-armar el grafo a mano cada vez.
// =============================================================================

#include "core/NodeGraph.hpp"
#include "core/NodeType.hpp"
#include "core/ScnSerializer.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::string path = "/tmp/walkthrough_pid.scn";
    if (argc > 1) path = argv[1];

    NodeGraph g;
    int setpt = g.addNode(NodeType::StepSignal);
    int sum   = g.addNode(NodeType::Summation);
    int pid   = g.addNode(NodeType::PIDController);
    int motor = g.addNode(NodeType::DCMotorModel);
    int scope = g.addNode(NodeType::Oscilloscope);
    int v3d   = g.addNode(NodeType::View3DSink);  // para que el panel 3D
                                                  // responda al omega real

    g.setParam(setpt, "Amplitude", 50.0);
    g.setParam(sum,   "Sign2",     -1.0);
    g.setParam(pid,   "Kp",        0.5);
    g.setParam(pid,   "Ki",        2.0);

    auto* nset = g.findNode(setpt);
    auto* nsm  = g.findNode(sum);
    auto* np   = g.findNode(pid);
    auto* nm   = g.findNode(motor);
    auto* nk   = g.findNode(scope);
    auto* n3d  = g.findNode(v3d);
    g.tryAddEdge(nset->outputAttrId(), nsm->inputAttrId(0));
    g.tryAddEdge(nsm->outputAttrId(),  np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(),   nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   nk->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(),   n3d->inputAttrId(0));  // 3D ← omega
    g.tryAddEdge(nm->outputAttrId(),   nsm->inputAttrId(1));  // feedback

    // Posiciones razonables para que el grafo se vea ordenado en NodeCanvas.
    // El View3DSink va debajo del Oscilloscope porque comparten input.
    ScnPositions positions;
    positions[setpt] = {  50, 200};
    positions[sum  ] = { 250, 200};
    positions[pid  ] = { 450, 200};
    positions[motor] = { 650, 200};
    positions[scope] = { 850, 140};
    positions[v3d  ] = { 850, 260};

    if (!ScnSerializer::saveToFile(path, g, positions)) {
        std::fprintf(stderr, "[ERROR] No pude escribir %s\n", path.c_str());
        return 1;
    }
    std::fprintf(stdout, "Escrito %s\n", path.c_str());
    return 0;
}
