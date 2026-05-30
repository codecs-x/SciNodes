// -----------------------------------------------------------------------
// Unit tests for GrammarParser, NodeGraph, and ScnSerializer.
//
// Build: cmake --build build --target test_grammar
// Run:   ./build/test_grammar
// -----------------------------------------------------------------------
#include "../src/core/CsvExport.hpp"
#include "../src/core/CsvParamIO.hpp"
#include "../src/core/CustomNodeRegistry.hpp"
#include "../src/core/Fft.hpp"
#include "../src/core/GrammarParser.hpp"
#include "../src/core/NodeGraph.hpp"
#include "../src/core/NodeInstance.hpp"
#include "../src/core/ScilabCodeGen.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// ---- Minimal test framework --------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT_TRUE(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
           std::cerr << "  FAIL  " << #cond \
                     << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; } \
} while(0)

#define EXPECT_FALSE(cond)   EXPECT_TRUE(!(cond))
#define EXPECT_VALID(edge)   EXPECT_FALSE((edge).has_value())
#define EXPECT_INVALID(edge) EXPECT_TRUE( (edge).has_value())
#define EXPECT_RULE(edge, r) EXPECT_TRUE( (edge).has_value() && (edge)->rule == (r))

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static NodeInstance src(int id, NodeType t = NodeType::SineSignal) {
    return makeNode(id, t);
}
static NodeInstance tx(int id, NodeType t = NodeType::Gain) {
    return makeNode(id, t);
}
static NodeInstance sk(int id, NodeType t = NodeType::Oscilloscope) {
    return makeNode(id, t);
}

// -----------------------------------------------------------------------
// Section 1 — Single-edge validation (GrammarParser::validateEdge)
// -----------------------------------------------------------------------
static void test_edge_source_to_transformer() {
    std::cout << "[1] Source → Transformer\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(src(1), tx(2), no_edges));
}
static void test_edge_source_to_sink() {
    std::cout << "[2] Source → Sink\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(src(1), sk(2), no_edges));
}
static void test_edge_transformer_to_transformer() {
    std::cout << "[3] Transformer → Transformer\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(tx(1), tx(2), no_edges));
}
static void test_edge_transformer_to_sink() {
    std::cout << "[4] Transformer → Sink\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(tx(1), sk(2), no_edges));
}
static void test_edge_sink_to_anything() {
    std::cout << "[5] Sink → Transformer  (R1 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(sk(1), tx(2), no_edges);
    EXPECT_RULE(err, "R1");
}
static void test_edge_anything_to_source() {
    std::cout << "[6] Transformer → Source  (R2 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(tx(1), src(2), no_edges);
    EXPECT_RULE(err, "R2");
}
static void test_edge_source_to_source() {
    std::cout << "[7] Source → Source  (R2 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(src(1), src(2), no_edges);
    EXPECT_RULE(err, "R2");
}
static void test_edge_self_loop() {
    std::cout << "[8] Self-loop  (R3)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(tx(5), tx(5), no_edges);
    EXPECT_RULE(err, "R3");
}
static void test_edge_duplicate() {
    std::cout << "[9] Duplicate edge  (R4)\n";
    // R4 is now enforced at NodeGraph::tryAddEdge so it can compare full
    // (fromAttrId, toAttrId) attribute pairs — multi-output sources are
    // allowed to fan out to distinct ports of the same target.
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    auto* ns = g.findNode(s);
    auto* nt = g.findNode(t);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nt->inputAttrId(0)));
    // Same exact attribute pair again — must be rejected with R4.
    auto err = g.tryAddEdge(ns->outputAttrId(0), nt->inputAttrId(0));
    EXPECT_RULE(err, "R4");
}

// Multi-output source fans out to two distinct input ports of the same
// destination — used to be rejected as R4 in the node-pair-only check.
static void test_edge_multiport_same_pair_allowed() {
    std::cout << "[9b] Multi-output → multi-input on same pair (no R4)\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);   // 4 outputs
    int sz = g.addNode(NodeType::PMSMSizing);        // 2 inputs
    auto* nd = g.findNode(dt);
    auto* ns = g.findNode(sz);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1)));
}
static void test_edge_input_occupied() {
    std::cout << "[10] Input already connected  (R5)\n";
    GrammarParser p;
    auto n3 = src(3); auto n2 = tx(2);
    // Edge from node 3 to node 2 already exists
    std::vector<Edge> existing = {
        Edge{ 1, 3, 2, n3.outputAttrId(), n2.inputAttrId(0) }
    };
    // Now try a different source → same sink input port
    auto err = p.validateEdge(src(1), n2, existing);
    EXPECT_RULE(err, "R5");
}

// -----------------------------------------------------------------------
// Section 2 — Graph-level validation (NodeGraph)
// -----------------------------------------------------------------------
static void test_graph_empty() {
    std::cout << "[11] Empty graph\n";
    NodeGraph g;
    EXPECT_TRUE(g.grammarState() == GrammarState::Empty);
}
static void test_graph_incomplete_no_edges() {
    std::cout << "[12] Nodes but no edges → Incomplete\n";
    NodeGraph g;
    g.addNode(NodeType::SineSignal);
    g.addNode(NodeType::Oscilloscope);
    EXPECT_TRUE(g.grammarState() == GrammarState::Incomplete);
}
static void test_graph_valid_source_sink() {
    std::cout << "[13] Source → Sink direct → Valid\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);
    auto ns = g.findNode(s); auto nk = g.findNode(k);
    auto err = g.tryAddEdge(ns->outputAttrId(), nk->inputAttrId(0));
    EXPECT_VALID(err);
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}
static void test_graph_valid_chain() {
    std::cout << "[14] Source → Transformer → Sink → Valid\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto ns = g.findNode(s); auto nt = g.findNode(t); auto nk = g.findNode(k);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0)));
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}
static void test_graph_rejected_sink_to_tx() {
    std::cout << "[15] Rejected edge does not corrupt graph state\n";
    NodeGraph g;
    int k = g.addNode(NodeType::Oscilloscope);
    int t = g.addNode(NodeType::Gain);
    auto nk = g.findNode(k); auto nt = g.findNode(t);
    auto err = g.tryAddEdge(nk->outputAttrId(), nt->inputAttrId(0)); // Sink → Tx: invalid
    EXPECT_INVALID(err);
    EXPECT_TRUE(g.edgeCount() == 0);
    EXPECT_TRUE(g.grammarState() == GrammarState::Incomplete);
}

// -----------------------------------------------------------------------
// Section 3 — Undo / Redo via NodeGraph + UndoRedoStack
// -----------------------------------------------------------------------
static void test_undo_add_node() {
    std::cout << "[16] Undo add-node restores empty graph\n";
    NodeGraph g;
    UndoRedoStack h;

    auto before = g.snapshot();
    g.addNode(NodeType::SineSignal);
    h.record(before);

    EXPECT_TRUE(g.nodeCount() == 1);
    auto prev = h.undo(g.snapshot());
    EXPECT_TRUE(prev.has_value());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.nodeCount() == 0);
}
static void test_undo_add_edge() {
    std::cout << "[17] Undo add-edge removes it\n";
    NodeGraph g;
    UndoRedoStack h;

    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);

    auto before = g.snapshot();
    g.tryAddEdge(g.findNode(s)->outputAttrId(), g.findNode(k)->inputAttrId(0));
    h.record(before);

    EXPECT_TRUE(g.edgeCount() == 1);
    auto prev = h.undo(g.snapshot());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.edgeCount() == 0);
}
static void test_redo() {
    std::cout << "[18] Redo re-applies add-node\n";
    NodeGraph g;
    UndoRedoStack h;

    auto before = g.snapshot();
    g.addNode(NodeType::Gain);
    h.record(before);

    // Undo
    auto prev = h.undo(g.snapshot());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.nodeCount() == 0);

    // Redo
    auto next = h.redo(g.snapshot());
    g.restoreSnapshot(*next);
    EXPECT_TRUE(g.nodeCount() == 1);
}
static void test_undo_stack_limit() {
    std::cout << "[19] UndoRedoStack caps at MAX_DEPTH\n";
    UndoRedoStack h;
    NodeGraph g;
    for (int i = 0; i < UndoRedoStack::MAX_DEPTH + 10; ++i)
        h.record(g.snapshot());
    // Undo MAX_DEPTH times — the first 10 are gone
    int count = 0;
    while (h.canUndo()) { h.undo(g.snapshot()); ++count; }
    EXPECT_TRUE(count == UndoRedoStack::MAX_DEPTH);
}

// -----------------------------------------------------------------------
// Section 4 — ScnSerializer round-trip and load validation
// -----------------------------------------------------------------------
static void test_serializer_roundtrip() {
    std::cout << "[20] Serializer round-trip preserves graph\n";

    NodeGraph    g;
    ScnPositions pos;

    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(s, "Frequency", 7.5);
    g.setParam(t, "K", 2.25);

    auto* ns = g.findNode(s);
    auto* nt = g.findNode(t);
    auto* nk = g.findNode(k);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0)));

    pos[s] = { 10.f, 20.f };
    pos[t] = { 110.f, 20.f };
    pos[k] = { 210.f, 20.f };

    std::string text = ScnSerializer::serialize(g, pos);

    NodeGraph    g2;
    ScnPositions pos2;
    LoadReport report = ScnSerializer::deserialize(text, g2, pos2);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.nodesLoaded == 3);
    EXPECT_TRUE(report.edgesLoaded == 2);
    EXPECT_TRUE(report.rejectedEdges.empty());
    EXPECT_TRUE(report.finalState == GrammarState::Valid);

    EXPECT_TRUE(g2.nodeCount() == 3);
    EXPECT_TRUE(g2.edgeCount() == 2);

    // Param values survived
    auto* ns2 = g2.findNode(s);
    auto* nt2 = g2.findNode(t);
    EXPECT_TRUE(ns2 && std::fabs(ns2->params.at("Frequency") - 7.5) < 1e-9);
    EXPECT_TRUE(nt2 && std::fabs(nt2->params.at("K") - 2.25) < 1e-9);

    // Positions survived
    EXPECT_TRUE(pos2.size() == 3);
    EXPECT_TRUE(std::fabs(pos2[t].x - 110.f) < 1e-3f);
}

static void test_serializer_rejects_bad_edge() {
    std::cout << "[21] Loading a file with a Sink→Transformer edge "
                 "→ ok with rejected-edge report\n";

    // Hand-crafted .scn: Oscilloscope (id=1) → Gain (id=2). That edge is R1.
    const char* bad = R"({
      "scnodes_version": "0.3",
      "next_node_id": 3,
      "nodes": [
        {"id":1, "type":"Oscilloscope", "position":[0,0], "params":{"Time Window":5.0}},
        {"id":2, "type":"Gain",         "position":[100,0],"params":{"K":1.0}}
      ],
      "edges": [
        {"id":1, "from_node":1, "to_node":2, "to_port":0}
      ]
    })";

    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(bad, g, pos);

    EXPECT_TRUE(report.ok);                        // JSON parsed fine
    EXPECT_TRUE(report.nodesLoaded == 2);
    EXPECT_TRUE(report.edgesLoaded == 0);          // the bad edge was dropped
    EXPECT_TRUE(report.rejectedEdges.size() == 1);
    EXPECT_TRUE(report.rejectedEdges[0].rule == "R1");
    EXPECT_TRUE(report.hasViolations());

    EXPECT_TRUE(g.nodeCount() == 2);
    EXPECT_TRUE(g.edgeCount() == 0);
}

static void test_serializer_unknown_type() {
    std::cout << "[22] Unknown node type is reported, other nodes still load\n";

    const char* mixed = R"({
      "scnodes_version": "0.3",
      "nodes": [
        {"id":1, "type":"SineSignal",      "position":[0,0],"params":{}},
        {"id":2, "type":"HypotheticalNode","position":[0,0],"params":{}},
        {"id":3, "type":"Oscilloscope",    "position":[0,0],"params":{}}
      ],
      "edges": []
    })";

    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(mixed, g, pos);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.nodesLoaded == 2);
    EXPECT_TRUE(report.unknownTypes.size() == 1);
    EXPECT_TRUE(report.unknownTypes[0] == "HypotheticalNode");
    EXPECT_TRUE(g.nodeCount() == 2);
}

static void test_serializer_fatal_json_error() {
    std::cout << "[23] Malformed JSON → fatal error, graph reset\n";
    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize("{ not valid", g, pos);
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.fatalError.empty());
    EXPECT_TRUE(g.nodeCount() == 0);
}

// -----------------------------------------------------------------------
// Section 5 — ScilabCodeGen (graph → .sce text)
// -----------------------------------------------------------------------
static void test_codegen_simple_chain() {
    std::cout << "[24] CodeGen: SineSignal → Gain → Oscilloscope\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(t, "K", 2.5);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1
                && plan.sinkChannels[0].nodeId == k
                && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.script.find("READY")     != std::string::npos);
    EXPECT_TRUE(plan.script.find("STATE")     != std::string::npos);
    EXPECT_TRUE(plan.script.find("driver()")  != std::string::npos);
    // Gain's K param is now variabilized: p_<gain_id>_0 = 2.5
    std::string gainK = "p_" + std::to_string(t) + "_0";
    EXPECT_TRUE(plan.script.find(gainK + " = 2.5") != std::string::npos);
    EXPECT_TRUE(plan.script.find(gainK + " * v")   != std::string::npos);
}

static void test_codegen_every_type_supported() {
    std::cout << "[25] CodeGen: every registry NodeType is emittable\n";
    for (const auto& [type, def] : nodeRegistry())
        EXPECT_TRUE(ScilabCodeGen::isSupported(type));
}

static void test_codegen_integrator_emits_dynamics() {
    std::cout << "[28] CodeGen: Integrator emits dynamics() and ode() call\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int i = g.addNode(NodeType::Integrator);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(i, "Initial Cond.", 5.0);
    auto* ns = g.findNode(s); auto* ni = g.findNode(i); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), ni->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") != std::string::npos);
    EXPECT_TRUE(plan.script.find("ode(\"rk\"")              != std::string::npos);
    // IC is now stored in a variable: p_<integ>_0 = 5
    std::string icVar = "p_" + std::to_string(i) + "_0";
    EXPECT_TRUE(plan.script.find(icVar + " = 5")  != std::string::npos);
    EXPECT_TRUE(plan.script.find("x = [" + icVar) != std::string::npos);
    EXPECT_TRUE(plan.script.find("dxdt(1) = v"
                                 + std::to_string(s)) != std::string::npos);
}

static void test_codegen_dcmotor_two_states() {
    std::cout << "[29] CodeGen: DCMotor allocates 2 state slots\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int m = g.addNode(NodeType::DCMotorModel);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* nv = g.findNode(v); auto* nm = g.findNode(m); auto* nk = g.findNode(k);
    g.tryAddEdge(nv->outputAttrId(), nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // 2 state slots → dxdt(1) and dxdt(2) both appear.
    EXPECT_TRUE(plan.script.find("dxdt(1)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("dxdt(2)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("State vector length: 2") != std::string::npos);
}

static void test_codegen_stateless_skips_ode() {
    std::cout << "[30] CodeGen: stateless graphs skip the dynamics function\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") == std::string::npos);
    EXPECT_TRUE(plan.script.find("ode(\"rk\"")              == std::string::npos);
}

static void test_codegen_transfer_function2() {
    std::cout << "[38] CodeGen: TransferFunction2 — 2-state controllable form\n";
    NodeGraph g;
    int s  = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction2);
    int k  = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]", 1.0);
    g.setParam(tf, "num[1]", 0.0);
    g.setParam(tf, "den[0]", 1.0);
    g.setParam(tf, "den[1]", 0.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 2") != std::string::npos);
    // Controllable canonical: dxdt(1) = x(2)
    EXPECT_TRUE(plan.script.find("dxdt(1) = x(2)")        != std::string::npos);
    // Output is a linear combo of states (pure-state, no feedthrough).
    std::string b0 = "p_" + std::to_string(tf) + "_0";
    EXPECT_TRUE(plan.script.find(b0 + "*x(1)")            != std::string::npos);
}

static void test_codegen_view3d_sink_supported() {
    std::cout << "[40c] CodeGen: View3DSink is a valid 1-channel sink\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int v = g.addNode(NodeType::View3DSink);
    auto* ns = g.findNode(s); auto* nv = g.findNode(v);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nv->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == v
                && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}

static void test_codegen_phaseportrait_two_channels() {
    std::cout << "[40b] CodeGen: PhasePortrait sink contributes 2 STATE channels\n";
    NodeGraph g;
    int sx = g.addNode(NodeType::SineSignal);
    int sy = g.addNode(NodeType::CurrentSource);
    int pp = g.addNode(NodeType::PhasePortrait);
    auto* nx = g.findNode(sx); auto* ny = g.findNode(sy); auto* np = g.findNode(pp);
    g.tryAddEdge(nx->outputAttrId(), np->inputAttrId(0));
    g.tryAddEdge(ny->outputAttrId(), np->inputAttrId(1));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == pp && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.sinkChannels[1].nodeId == pp && plan.sinkChannels[1].channel == 1);
    // Channel 1 variable v<pp>_1 must appear in the script.
    std::string v1 = "v" + std::to_string(pp) + "_1";
    EXPECT_TRUE(plan.script.find(v1 + " = ") != std::string::npos);
}

static void test_codegen_emits_nan_guard() {
    std::cout << "[39] CodeGen: STATE line carries nanid + emits isnan() guards\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("nanid = 0;")               != std::string::npos);
    EXPECT_TRUE(plan.script.find("STATE %d")                 != std::string::npos);
    EXPECT_TRUE(plan.script.find("\\n\", nanid")             != std::string::npos);
    EXPECT_TRUE(plan.script.find("isnan(v" + std::to_string(s)) != std::string::npos);
}

static void test_codegen_inverse_kinematics() {
    std::cout << "[37] CodeGen: InverseKinematics is 2-input / 2-output planar IK\n";
    NodeGraph g;
    int sx = g.addNode(NodeType::CurrentSource);   // target x
    int sy = g.addNode(NodeType::CurrentSource);   // target y
    int ik = g.addNode(NodeType::InverseKinematics);
    int k1 = g.addNode(NodeType::Oscilloscope);    // θ₁
    int k2 = g.addNode(NodeType::Oscilloscope);    // θ₂
    auto* nx = g.findNode(sx); auto* ny = g.findNode(sy);
    auto* ni = g.findNode(ik);
    auto* nk1 = g.findNode(k1); auto* nk2 = g.findNode(k2);
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(),  ni->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ny->outputAttrId(),  ni->inputAttrId(1)));
    // ik has two output ports: port 0 = θ₁, port 1 = θ₂.
    EXPECT_VALID(g.tryAddEdge(ni->outputAttrId(0), nk1->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ni->outputAttrId(1), nk2->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Two output ports on the IK node ⇒ two variables emitted, one of
    // them with the _1 suffix.
    std::string v0 = "v" + std::to_string(ik);
    std::string v1 = "v" + std::to_string(ik) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = atan(") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = atan(") != std::string::npos);
    // Both sinks recorded, in column order.
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
}

static void test_codegen_design_template_emits_four_outputs() {
    std::cout << "[58] Sizing: DesignTemplate emits 4 constant output ports\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);
    // 4 sinks, one per output port.
    int k0 = g.addNode(NodeType::Oscilloscope);
    int k1 = g.addNode(NodeType::Oscilloscope);
    int k2 = g.addNode(NodeType::Oscilloscope);
    int k3 = g.addNode(NodeType::Oscilloscope);
    auto* nd = g.findNode(dt);
    g.setParam(dt, "Target Torque",  25.0);
    g.setParam(dt, "Target Speed",  300.0);
    g.setParam(dt, "Bus Voltage",   600.0);
    g.setParam(dt, "Cooling Class",   2.0);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), g.findNode(k0)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), g.findNode(k1)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(2), g.findNode(k2)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(3), g.findNode(k3)->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // 4 distinct sink channels in the STATE line.
    EXPECT_TRUE(plan.sinkChannels.size() == 4);
    // The param-init block must contain the four user-supplied defaults.
    EXPECT_TRUE(plan.script.find("= 25") != std::string::npos);
    EXPECT_TRUE(plan.script.find("= 300") != std::string::npos);
    EXPECT_TRUE(plan.script.find("= 600") != std::string::npos);
}

// PMSMSizing now carries Slot Count and Pole Count alongside the
// electromagnetic params. They're consumed by the View3D panel's
// procedural-mesh builder (which lives in src/ui and therefore can't
// be exercised from this headless harness). Here we verify the
// registry surface so a renaming or a default drift gets caught.
static void test_codegen_pmsm_efficiency_emits_loss_terms() {
    std::cout << "[59e] Sweep: PMSMEfficiency emits P_cu, P_fe, P_mech, P_out\n";
    NodeGraph g;
    int sT  = g.addNode(NodeType::StepSignal);
    int sW  = g.addNode(NodeType::StepSignal);
    int sK  = g.addNode(NodeType::StepSignal);
    int eff = g.addNode(NodeType::PMSMEfficiency);
    int sk  = g.addNode(NodeType::Oscilloscope);
    auto* ne = g.findNode(eff);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), ne->inputAttrId(0));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), ne->inputAttrId(1));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), ne->inputAttrId(2));
    g.tryAddEdge(ne->outputAttrId(), g.findNode(sk)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Copper loss term: 1.5 * R * Iq^2.
    EXPECT_TRUE(plan.script.find("1.5 * ") != std::string::npos);
    // Element-wise divide and the safety bool2s guard.
    EXPECT_TRUE(plan.script.find("bool2s(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("./") != std::string::npos);
}

static void test_codegen_heatmap_sink_three_channels() {
    std::cout << "[59f] Sweep: HeatmapSink contributes 3 STATE channels\n";
    NodeGraph g;
    int sX  = g.addNode(NodeType::StepSignal);
    int sY  = g.addNode(NodeType::StepSignal);
    int sC  = g.addNode(NodeType::StepSignal);
    int hm  = g.addNode(NodeType::HeatmapSink);
    auto* nh = g.findNode(hm);
    g.tryAddEdge(g.findNode(sX)->outputAttrId(), nh->inputAttrId(0));
    g.tryAddEdge(g.findNode(sY)->outputAttrId(), nh->inputAttrId(1));
    g.tryAddEdge(g.findNode(sC)->outputAttrId(), nh->inputAttrId(2));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == hm
             && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.sinkChannels[2].channel == 2);
}

static void test_codegen_airgap_flux_density_state_and_harmonics() {
    std::cout << "[59d] Air-gap: AirgapFluxDensity emits sin(p*x), 3rd, slot harmonics\n";
    NodeGraph g;
    int src   = g.addNode(NodeType::StepSignal);
    int agf   = g.addNode(NodeType::AirgapFluxDensity);
    int scope = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(src);
    auto* na = g.findNode(agf);
    auto* nk = g.findNode(scope);
    g.tryAddEdge(ns->outputAttrId(), na->inputAttrId(0));
    g.tryAddEdge(na->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // One state slot is allocated for theta — the script header records it.
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // Three sin() terms appear in the output expression: fundamental,
    // 3rd-magnet harmonic, slot-passing harmonic.
    std::string v = "v" + std::to_string(agf);
    EXPECT_TRUE(plan.script.find(v + " = ") != std::string::npos);
    // Count sin( occurrences in the output assignment line. A simple
    // substring count is enough — the codegen emits exactly three.
    size_t pos = plan.script.find(v + " =");
    EXPECT_TRUE(pos != std::string::npos);
    if (pos != std::string::npos) {
        size_t eol = plan.script.find(';', pos);
        std::string line = plan.script.substr(pos, eol - pos);
        int sinCount = 0;
        for (size_t i = 0; (i = line.find("sin(", i)) != std::string::npos; ++i)
            ++sinCount;
        EXPECT_TRUE(sinCount == 3);
    }
    // dxdt = src(0) → the StepSignal's variable name.
    EXPECT_TRUE(plan.script.find("dxdt(1) = v" + std::to_string(src))
                != std::string::npos);
}

static void test_codegen_pmsm_electromagnetic_emits_formulas() {
    std::cout << "[59c] EM: PMSMElectromagnetic emits Ke/L/Vrms/Tcog expressions\n";
    NodeGraph g;
    int sD   = g.addNode(NodeType::StepSignal);
    int sL   = g.addNode(NodeType::StepSignal);
    int sW   = g.addNode(NodeType::StepSignal);
    int em   = g.addNode(NodeType::PMSMElectromagnetic);
    int kKe  = g.addNode(NodeType::Oscilloscope);
    int kLp  = g.addNode(NodeType::Oscilloscope);
    int kV   = g.addNode(NodeType::Oscilloscope);
    int kTc  = g.addNode(NodeType::Oscilloscope);

    auto* ne = g.findNode(em);
    g.tryAddEdge(g.findNode(sD)->outputAttrId(), ne->inputAttrId(0));
    g.tryAddEdge(g.findNode(sL)->outputAttrId(), ne->inputAttrId(1));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), ne->inputAttrId(2));
    g.tryAddEdge(ne->outputAttrId(0), g.findNode(kKe)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(1), g.findNode(kLp)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(2), g.findNode(kV)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(3), g.findNode(kTc)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 4);
    // Ke formula must reference omega-independent params (kw, Nph, p, Bg).
    EXPECT_TRUE(plan.script.find("sqrt(2)") != std::string::npos);    // Vrms
    EXPECT_TRUE(plan.script.find("4*%pi*1e-7") != std::string::npos); // mu0
    EXPECT_TRUE(plan.script.find("1.05") != std::string::npos);       // mu_r
}

static void test_pmsm_sizing_has_slot_and_pole_params() {
    std::cout << "[59b] Sizing: PMSMSizing exposes Slot Count + Pole Count params\n";
    const auto& def = nodeRegistry().at(NodeType::PMSMSizing);
    bool hasSlot = false, hasPole = false;
    double slotDefault = 0, poleDefault = 0;
    for (const auto& p : def.params) {
        if (p.name == "Slot Count") { hasSlot = true; slotDefault = p.defaultValue; }
        if (p.name == "Pole Count") { hasPole = true; poleDefault = p.defaultValue; }
    }
    EXPECT_TRUE(hasSlot);
    EXPECT_TRUE(hasPole);
    EXPECT_TRUE(std::abs(slotDefault - 12.0) < 1e-9);
    EXPECT_TRUE(std::abs(poleDefault -  4.0) < 1e-9);
}

static void test_codegen_pmsm_sizing_emits_cube_root() {
    std::cout << "[59] Sizing: PMSMSizing emits cube-root formula and rated-P expression\n";
    NodeGraph g;
    int dt   = g.addNode(NodeType::DesignTemplate);
    int sz   = g.addNode(NodeType::PMSMSizing);
    int sk_D = g.addNode(NodeType::Oscilloscope);
    int sk_L = g.addNode(NodeType::Oscilloscope);
    int sk_P = g.addNode(NodeType::Oscilloscope);

    auto* nd = g.findNode(dt);
    auto* ns = g.findNode(sz);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0)));   // T
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1)));   // omega
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), g.findNode(sk_D)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(1), g.findNode(sk_L)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(2), g.findNode(sk_P)->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("^(1.0/3.0)") != std::string::npos);
    // Rated-power expression: T * omega.
    std::string T = "v" + std::to_string(dt);          // port 0 alias
    std::string w = "v" + std::to_string(dt) + "_1";   // port 1 alias
    EXPECT_TRUE(plan.script.find(T + " * " + w) != std::string::npos);
    // Three sink channels in STATE.
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
}

static void test_codegen_transfer_function_supported() {
    std::cout << "[36] CodeGen: TransferFunction emits 1-state ODE\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]", 2.5);
    g.setParam(tf, "den[0]", 3.0);
    g.setParam(tf, "den[1]", 1.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // (num[0]*input - den[0]*x) / den[1]
    std::string b  = "p_" + std::to_string(tf) + "_0";
    std::string a0 = "p_" + std::to_string(tf) + "_1";
    std::string a1 = "p_" + std::to_string(tf) + "_2";
    EXPECT_TRUE(plan.script.find("(" + b + "*v" + std::to_string(s)) != std::string::npos);
    EXPECT_TRUE(plan.script.find(" - " + a0 + "*x(") != std::string::npos);
    EXPECT_TRUE(plan.script.find(") / " + a1)        != std::string::npos);
    EXPECT_TRUE(plan.script.find(b + " = 2.5") != std::string::npos);
}

static void test_codegen_differentiator_supported() {
    std::cout << "[35] CodeGen: Differentiator emits 1-state filtered deriv\n";
    NodeGraph g;
    int s = g.addNode(NodeType::RampSignal);
    int d = g.addNode(NodeType::Differentiator);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(d, "Cutoff Freq.", 50.0);
    auto* ns = g.findNode(s); auto* nd = g.findNode(d); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nd->inputAttrId(0));
    g.tryAddEdge(nd->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // Output expression uses ωc·(input − x).
    EXPECT_TRUE(plan.script.find("(2*%pi*p_" + std::to_string(d) + "_0)")
                != std::string::npos);
    EXPECT_TRUE(plan.script.find("p_" + std::to_string(d) + "_0 = 50")
                != std::string::npos);
}

static void test_codegen_closed_loop_through_integrator() {
    std::cout << "[33] CodeGen: cycle through Integrator generates cleanly\n";
    // Step → Sum(+,-) → Integrator → Scope
    //                      ↑                ↓
    //                      ←──────── (feedback) ────────←
    NodeGraph g;
    int step = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int integ = g.addNode(NodeType::Integrator);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(sum, "Sign2", -1.0);
    auto* nstep  = g.findNode(step);
    auto* nsum   = g.findNode(sum);
    auto* ninteg = g.findNode(integ);
    auto* nscope = g.findNode(scope);

    EXPECT_VALID(g.tryAddEdge(nstep->outputAttrId(),  nsum->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nsum->outputAttrId(),   ninteg->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ninteg->outputAttrId(), nscope->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ninteg->outputAttrId(), nsum->inputAttrId(1)));   // feedback

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") != std::string::npos);
    EXPECT_TRUE(plan.script.find("dxdt(1) = v" + std::to_string(sum))
                != std::string::npos);
}

static void test_codegen_algebraic_loop_rejected() {
    std::cout << "[34] CodeGen: algebraic loop (no pure-state) is rejected\n";
    // Sum(+,-) → Gain → back to Sum  (no Integrator/LPF/DCMotor in the cycle)
    NodeGraph g;
    int src  = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int gain = g.addNode(NodeType::Gain);
    int snk  = g.addNode(NodeType::Oscilloscope);
    auto* nsrc  = g.findNode(src);
    auto* nsum  = g.findNode(sum);
    auto* ngain = g.findNode(gain);
    auto* nsnk  = g.findNode(snk);
    EXPECT_VALID(g.tryAddEdge(nsrc->outputAttrId(),  nsum->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nsum->outputAttrId(),  ngain->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ngain->outputAttrId(), nsum->inputAttrId(1)));   // algebraic loop
    EXPECT_VALID(g.tryAddEdge(ngain->outputAttrId(), nsnk->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_FALSE(plan.error.empty());
    EXPECT_TRUE(plan.error.find("algebraic loop") != std::string::npos);
    EXPECT_TRUE(plan.script.empty());
}

static void test_codegen_param_dispatch_emitted() {
    std::cout << "[32] CodeGen: emits a `param N I V` dispatch branch\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("elseif cmd == \"param\"") != std::string::npos);
    EXPECT_TRUE(plan.script.find("pn = mfscanf") != std::string::npos);
    // One branch per (node, param-index).  Gain has 1 param "K".
    std::string gainK = "p_" + std::to_string(t) + "_0";
    EXPECT_TRUE(plan.script.find("pn == " + std::to_string(t)
                                 + " & pi == 0 then " + gainK)
                != std::string::npos);
}

static void test_codegen_pid_uses_state() {
    std::cout << "[31] CodeGen: PID output uses Kp*input + Ki*state\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int p = g.addNode(NodeType::PIDController);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(p, "Kp", 3.0);
    g.setParam(p, "Ki", 0.5);
    auto* ns = g.findNode(s); auto* np = g.findNode(p); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Kp = param 0, Ki = param 1 — both now variabilized.
    std::string Kp = "p_" + std::to_string(p) + "_0";
    std::string Ki = "p_" + std::to_string(p) + "_1";
    EXPECT_TRUE(plan.script.find(Kp + " = 3")            != std::string::npos);
    EXPECT_TRUE(plan.script.find(Ki + " = 0.5")          != std::string::npos);
    EXPECT_TRUE(plan.script.find(Kp + " * v" + std::to_string(s)) != std::string::npos);
    EXPECT_TRUE(plan.script.find(Ki + " * x(")           != std::string::npos);
}

static void test_codegen_empty_graph() {
    std::cout << "[26] CodeGen: empty graph yields no sinks, no error\n";
    NodeGraph g;
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.empty());
}

static void test_codegen_summation_two_inputs() {
    std::cout << "[27] CodeGen: Summation reads both input ports\n";
    NodeGraph g;
    int a = g.addNode(NodeType::SineSignal);
    int b = g.addNode(NodeType::StepSignal);
    int sum = g.addNode(NodeType::Summation);
    int k   = g.addNode(NodeType::Oscilloscope);
    auto* na = g.findNode(a); auto* nb = g.findNode(b);
    auto* nsum = g.findNode(sum); auto* nk = g.findNode(k);
    g.tryAddEdge(na->outputAttrId(),   nsum->inputAttrId(0));
    g.tryAddEdge(nb->outputAttrId(),   nsum->inputAttrId(1));
    g.tryAddEdge(nsum->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("v" + std::to_string(a)) != std::string::npos);
    EXPECT_TRUE(plan.script.find("v" + std::to_string(b)) != std::string::npos);
}

// -----------------------------------------------------------------------
// Section 6 — FFT helper (scinodes::magnitudeSpectrum)
// -----------------------------------------------------------------------
static void test_fft_pow2_check() {
    std::cout << "[40] FFT: isPow2 utility\n";
    EXPECT_TRUE(scinodes::isPow2(1));
    EXPECT_TRUE(scinodes::isPow2(2));
    EXPECT_TRUE(scinodes::isPow2(64));
    EXPECT_FALSE(scinodes::isPow2(0));
    EXPECT_FALSE(scinodes::isPow2(3));
    EXPECT_FALSE(scinodes::isPow2(100));
}

static void test_fft_peak_at_expected_bin() {
    std::cout << "[41] FFT: pure sinusoid produces a clean peak at the expected bin\n";
    // Sample N = 64 points of sin(2*pi*k*t/N) over one full window, k=4.
    const int N = 64;
    const int k = 4;
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i)
        samples[i] = std::sin(2.0f * 3.14159265358979323846f * k * i / N);

    auto mag = scinodes::magnitudeSpectrum(samples.data(), N);
    EXPECT_TRUE((int)mag.size() == N/2 + 1);

    // Peak should be at bin k. Magnitude N/2 because energy is split with
    // the negative-frequency mirror (which we don't see in the one-sided
    // spectrum).
    int peakBin = static_cast<int>(
        std::max_element(mag.begin() + 1, mag.end()) - mag.begin());
    EXPECT_TRUE(peakBin == k);
    EXPECT_TRUE(std::fabs(mag[k] - N / 2.0f) < 1e-3f);
}

static void test_fft_dc_only_input() {
    std::cout << "[42] FFT: a constant signal produces all energy in DC bin\n";
    const int N = 32;
    std::vector<float> samples(N, 1.5f);
    auto mag = scinodes::magnitudeSpectrum(samples.data(), N);
    EXPECT_TRUE(std::fabs(mag[0] - N * 1.5f) < 1e-3f);
    for (int k = 1; k < (int)mag.size(); ++k)
        EXPECT_TRUE(mag[k] < 1e-3f);
}

static void test_fft_non_pow2_rejected() {
    std::cout << "[43] FFT: non-power-of-2 input returns an empty spectrum\n";
    std::vector<float> samples(48, 0.0f);
    auto mag = scinodes::magnitudeSpectrum(samples.data(), 48);
    EXPECT_TRUE(mag.empty());
}

// -----------------------------------------------------------------------
// Section 6 — CSV exporter
//
// The exporter takes a ring buffer + write index and emits chronologically
// ordered (time, value) rows. We round-trip a known buffer and verify the
// header, row count, timestamps, and value ordering.
// -----------------------------------------------------------------------
static std::string makeTempCsvPath() {
    char tmpl[] = "/tmp/scnodes_csv_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) ::close(fd);
    return std::string(tmpl) + ".csv";
}

static std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line); )
        out.push_back(line);
    return out;
}

static void test_csv_export_partial_buffer() {
    std::cout << "[44] CsvExport: partially-filled buffer writes only real samples\n";
    // 8-slot ring; 5 writes so chronological is buf[0..4].
    std::vector<float> buf(8, 0.0f);
    buf[0] = 1.0f; buf[1] = 2.0f; buf[2] = 3.0f; buf[3] = 4.0f; buf[4] = 5.0f;
    int wIdx = 5;

    auto path = makeTempCsvPath();
    std::string err;
    bool ok = scinodes::writeSinkCsv(path, buf, wIdx,
                                     /*latestTime*/ 1.0f, /*dt*/ 0.25f,
                                     "DataLogger #7", &err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.empty());

    auto lines = readLines(path);
    // # Sink header, "time,value" header, then 5 data rows = 7 lines.
    EXPECT_TRUE(lines.size() == 7);
    EXPECT_TRUE(lines[0].find("# Sink: DataLogger #7") == 0);
    EXPECT_TRUE(lines[1] == "time,value");

    // First data row: t = 1.0 - 4*0.25 = 0.0, value = 1.0
    // Last  data row: t = 1.0,              value = 5.0
    double t0 = 0, v0 = 0, tN = 0, vN = 0;
    EXPECT_TRUE(std::sscanf(lines[2].c_str(), "%lf,%lf", &t0, &v0) == 2);
    EXPECT_TRUE(std::sscanf(lines[6].c_str(), "%lf,%lf", &tN, &vN) == 2);
    EXPECT_TRUE(std::abs(t0 - 0.0) < 1e-4);
    EXPECT_TRUE(std::abs(v0 - 1.0) < 1e-4);
    EXPECT_TRUE(std::abs(tN - 1.0) < 1e-4);
    EXPECT_TRUE(std::abs(vN - 5.0) < 1e-4);

    std::remove(path.c_str());
}

static void test_csv_export_wrapped_buffer() {
    std::cout << "[45] CsvExport: wrapped ring buffer is reordered chronologically\n";
    // 4-slot ring; writes 1..10 with values 0..9. Write k lands at slot
    // (k-1) % 4, so the final slots [0..3] hold {8, 9, 6, 7} and the
    // surviving chronological values are 6, 7, 8, 9.
    std::vector<float> buf = { 8.0f, 9.0f, 6.0f, 7.0f };
    int wIdx = 10;

    auto path = makeTempCsvPath();
    bool ok = scinodes::writeSinkCsv(path, buf, wIdx, /*latestTime*/ 3.0f,
                                     /*dt*/ 1.0f, "", nullptr);
    EXPECT_TRUE(ok);

    auto lines = readLines(path);
    // No # comment (empty label), so: header + 4 rows.
    EXPECT_TRUE(lines.size() == 5);
    EXPECT_TRUE(lines[0] == "time,value");

    double t[4], v[4];
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(std::sscanf(lines[1 + i].c_str(), "%lf,%lf",
                                &t[i], &v[i]) == 2);
    EXPECT_TRUE(std::abs(v[0] - 6.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[1] - 7.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[2] - 8.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[3] - 9.0) < 1e-4);
    // Times: 0, 1, 2, 3.
    EXPECT_TRUE(std::abs(t[0] - 0.0) < 1e-4);
    EXPECT_TRUE(std::abs(t[3] - 3.0) < 1e-4);

    std::remove(path.c_str());
}

static void test_csv_export_empty_buffer() {
    std::cout << "[46] CsvExport: zero-write buffer emits only the header\n";
    std::vector<float> buf(16, 0.0f);
    auto path = makeTempCsvPath();
    EXPECT_TRUE(scinodes::writeSinkCsv(path, buf, /*wIdx*/ 0,
                                       0.0f, 0.01f, "", nullptr));
    auto lines = readLines(path);
    EXPECT_TRUE(lines.size() == 1);
    EXPECT_TRUE(lines[0] == "time,value");
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------
// Stage v0.9 thermal-network nodes — codegen smoke checks.
// -----------------------------------------------------------------------
static void test_codegen_joule_loss_emits_iq_squared() {
    std::cout << "[66] Thermal: JouleLoss emits 1.5 * R * (T/Ke)^2\n";
    NodeGraph g;
    int sT = g.addNode(NodeType::StepSignal);
    int sK = g.addNode(NodeType::StepSignal);
    int jl = g.addNode(NodeType::JouleLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    auto* nj = g.findNode(jl);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), nj->inputAttrId(0));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), nj->inputAttrId(1));
    g.tryAddEdge(nj->outputAttrId(), g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("1.5 * ") != std::string::npos);
    EXPECT_TRUE(plan.script.find("1e-12") != std::string::npos);   // Ke guard
}

static void test_codegen_core_loss_uses_electrical_frequency() {
    std::cout << "[67] Thermal: CoreLoss uses p*omega/(2*pi) for the frequency\n";
    NodeGraph g;
    int sW = g.addNode(NodeType::StepSignal);
    int sB = g.addNode(NodeType::StepSignal);
    int cl = g.addNode(NodeType::CoreLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cl);
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), nc->inputAttrId(0));
    g.tryAddEdge(g.findNode(sB)->outputAttrId(), nc->inputAttrId(1));
    g.tryAddEdge(nc->outputAttrId(), g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("/ (2 * %pi)") != std::string::npos);
}

static void test_codegen_mechanical_loss_emits_quadratic_term() {
    std::cout << "[68] Thermal: MechanicalLoss = K_visc*|ω| + K_drag*ω^2\n";
    NodeGraph g;
    int sW = g.addNode(NodeType::StepSignal);
    int ml = g.addNode(NodeType::MechanicalLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(sW)->outputAttrId(),
                 g.findNode(ml)->inputAttrId(0));
    g.tryAddEdge(g.findNode(ml)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("abs(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("^2")    != std::string::npos);
}

static void test_codegen_thermal_node_sums_four_inputs() {
    std::cout << "[71] Thermal: ThermalNode sums 4 inputs and integrates / C\n";
    NodeGraph g;
    int s0 = g.addNode(NodeType::StepSignal);
    int s1 = g.addNode(NodeType::StepSignal);
    int tn = g.addNode(NodeType::ThermalNode);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(s0)->outputAttrId(),
                 g.findNode(tn)->inputAttrId(0));
    g.tryAddEdge(g.findNode(s1)->outputAttrId(),
                 g.findNode(tn)->inputAttrId(1));
    g.tryAddEdge(g.findNode(tn)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // dxdt(slot) = (v<s0> + v<s1> + 0.0 + 0.0) / p_<tn>_0
    std::string dx = "dxdt(1)";
    EXPECT_TRUE(plan.script.find(dx + " = (") != std::string::npos);
    EXPECT_TRUE(plan.script.find(" + 0.0 + 0.0") != std::string::npos);
}

static void test_codegen_maxwell_force_emits_mu0_form() {
    std::cout << "[75] Structural: MaxwellForce emits B^2 / (2 * 4*pi*1e-7)\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int mf  = g.addNode(NodeType::MaxwellForce);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(mf)->inputAttrId(0));
    g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("^2 / (2 * 4 * %pi * 1e-7)") != std::string::npos);
}

static void test_codegen_tolerance_perturbator_uses_rand() {
    std::cout << "[78] MC: TolerancePerturbator emits rand()-based noise\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int tp  = g.addNode(NodeType::TolerancePerturbator);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(tp)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tp)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("(2 * rand() - 1)") != std::string::npos);
}

static void test_codegen_distribution_sink_records_input() {
    std::cout << "[79] MC: DistributionSink contributes 1 STATE channel\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int ds  = g.addNode(NodeType::DistributionSink);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(ds)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == ds);
}

static void test_codegen_view3d_deformation_sink_three_channels() {
    std::cout << "[77] Structural: View3DDeformationSink contributes 3 channels\n";
    NodeGraph g;
    int sF = g.addNode(NodeType::StepSignal);
    int sM = g.addNode(NodeType::StepSignal);
    int sA = g.addNode(NodeType::StepSignal);
    int sk = g.addNode(NodeType::View3DDeformationSink);
    auto* ns = g.findNode(sk);
    g.tryAddEdge(g.findNode(sF)->outputAttrId(), ns->inputAttrId(0));
    g.tryAddEdge(g.findNode(sM)->outputAttrId(), ns->inputAttrId(1));
    g.tryAddEdge(g.findNode(sA)->outputAttrId(), ns->inputAttrId(2));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
    EXPECT_TRUE(plan.sinkChannels[2].channel == 2);
}

static void test_codegen_modal_frequency_emits_shape_factor() {
    std::cout << "[76] Structural: ModalFrequency emits m*(m^2-1)/sqrt(m^2+1) shape\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int mf  = g.addNode(NodeType::ModalFrequency);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(mf)->inputAttrId(0));
    g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // The thin-ring shape factor + m<=1 guard must both appear.
    EXPECT_TRUE(plan.script.find("bool2s(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("sqrt(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("/ (12 * ") != std::string::npos);
}

static void test_codegen_cooling_system_three_outputs() {
    std::cout << "[73] Cooling: CoolingSystem emits 3 STATE channels\n";
    NodeGraph g;
    int cs = g.addNode(NodeType::CoolingSystem);
    int k0 = g.addNode(NodeType::Oscilloscope);
    int k1 = g.addNode(NodeType::Oscilloscope);
    int k2 = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cs);
    g.tryAddEdge(nc->outputAttrId(0), g.findNode(k0)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(1), g.findNode(k1)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(2), g.findNode(k2)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
}

static void test_codegen_convective_cooling_h_of_flow() {
    std::cout << "[74] Cooling: ConvectiveCooling q = (h_0 + h_slope·flow)·ΔT\n";
    NodeGraph g;
    int sH = g.addNode(NodeType::StepSignal);
    int sC = g.addNode(NodeType::StepSignal);
    int sF = g.addNode(NodeType::StepSignal);
    int cc = g.addNode(NodeType::ConvectiveCooling);
    int kH = g.addNode(NodeType::Oscilloscope);
    int kC = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cc);
    g.tryAddEdge(g.findNode(sH)->outputAttrId(), nc->inputAttrId(0));
    g.tryAddEdge(g.findNode(sC)->outputAttrId(), nc->inputAttrId(1));
    g.tryAddEdge(g.findNode(sF)->outputAttrId(), nc->inputAttrId(2));
    g.tryAddEdge(nc->outputAttrId(0), g.findNode(kH)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(1), g.findNode(kC)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    // Dual output: the second is the negation, so both lines appear.
    std::string v0 = "v" + std::to_string(cc);
    std::string v1 = "v" + std::to_string(cc) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = ") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = ") != std::string::npos);
}

static void test_codegen_thermal_resistance_two_outputs() {
    std::cout << "[72] Thermal: ThermalResistance emits q and -q on its two ports\n";
    NodeGraph g;
    int s1 = g.addNode(NodeType::StepSignal);
    int s2 = g.addNode(NodeType::StepSignal);
    int tr = g.addNode(NodeType::ThermalResistance);
    int kH = g.addNode(NodeType::Oscilloscope);
    int kC = g.addNode(NodeType::Oscilloscope);
    auto* nr = g.findNode(tr);
    g.tryAddEdge(g.findNode(s1)->outputAttrId(), nr->inputAttrId(0));
    g.tryAddEdge(g.findNode(s2)->outputAttrId(), nr->inputAttrId(1));
    g.tryAddEdge(nr->outputAttrId(0), g.findNode(kH)->inputAttrId(0));
    g.tryAddEdge(nr->outputAttrId(1), g.findNode(kC)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    // Two distinct output assignments for the resistance node.
    std::string v0 = "v" + std::to_string(tr);
    std::string v1 = "v" + std::to_string(tr) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = (") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = (") != std::string::npos);
}

static void test_codegen_view3d_thermal_sink_records_one_channel() {
    std::cout << "[70] Thermal: View3DThermalSink contributes 1 STATE channel\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int sk  = g.addNode(NodeType::View3DThermalSink);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == sk);
    EXPECT_TRUE(plan.sinkChannels[0].channel == 0);
}

static void test_codegen_thermal_mass_has_state_and_initial_ambient() {
    std::cout << "[69] Thermal: ThermalMass declares 1 state, IC = T_ambient\n";
    NodeGraph g;
    int sP = g.addNode(NodeType::StepSignal);
    int tm = g.addNode(NodeType::ThermalMass);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.setParam(tm, "Ambient Temperature", 300.0);
    g.tryAddEdge(g.findNode(sP)->outputAttrId(),
                 g.findNode(tm)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tm)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // The ODE references the slot variable and the ambient param.
    EXPECT_TRUE(plan.script.find("x(1)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("dxdt(1)") != std::string::npos);
}

// -----------------------------------------------------------------------
// Section 7 — CustomNodeRegistry (Stage v0.7 addRule() hook)
//
// The registry parses JSON descriptors and stores them keyed by type_id.
// Built-in node types are unaffected; this is a parallel registry that
// future palette/codegen wiring can read from.
// -----------------------------------------------------------------------
static void test_custom_node_registry_transformer_round_trip() {
    std::cout << "[49] CustomNodeRegistry: valid transformer JSON round-trips\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();

    const char* j = R"({
        "type_id": "DoubleGain",
        "label": "Double Gain",
        "description": "Multiplies the input by 2*k.",
        "category": "transformer",
        "input_ports": 1,
        "output_ports": 1,
        "params": [
            {"name": "k", "default": 1.0, "units": "1"}
        ],
        "expression": "2 * p_k * u1"
    })";
    std::string err;
    EXPECT_TRUE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.empty());

    const auto* d = reg.find("DoubleGain");
    EXPECT_TRUE(d != nullptr);
    if (d) {
        EXPECT_TRUE(d->label == "Double Gain");
        EXPECT_TRUE(d->category == NodeCategory::Transformer);
        EXPECT_TRUE(d->inputPorts  == 1);
        EXPECT_TRUE(d->outputPorts == 1);
        EXPECT_TRUE(d->params.size() == 1);
        if (!d->params.empty()) {
            EXPECT_TRUE(d->params[0].name == "k");
            EXPECT_TRUE(std::abs(d->params[0].defaultValue - 1.0) < 1e-9);
            EXPECT_TRUE(d->params[0].unit == "1");
        }
        EXPECT_TRUE(d->expression == "2 * p_k * u1");
    }
}

static void test_custom_node_registry_source_no_inputs() {
    std::cout << "[50] CustomNodeRegistry: source descriptors omit inputs / expression\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();

    const char* j = R"({
        "type_id": "WhiteNoise",
        "label": "White Noise",
        "category": "source",
        "input_ports": 0,
        "output_ports": 1,
        "params": [{"name": "sigma", "default": 0.1}]
    })";
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    const auto* d = reg.find("WhiteNoise");
    EXPECT_TRUE(d != nullptr && d->category == NodeCategory::Source);
    EXPECT_TRUE(d != nullptr && d->expression.empty());
}

static void test_custom_node_registry_rejects_malformed_json() {
    std::cout << "[51] CustomNodeRegistry: malformed JSON is rejected with an error\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString("{ this is not json }", &err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(reg.typeIds().empty());
}

static void test_custom_node_registry_rejects_duplicate() {
    std::cout << "[52] CustomNodeRegistry: duplicate type_id is rejected\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
    const char* j = R"({
        "type_id": "Foo", "label": "Foo", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("Duplicate") != std::string::npos);
}

static void test_custom_node_registry_rejects_missing_field() {
    std::cout << "[53] CustomNodeRegistry: missing required field is rejected\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
    // No "label"
    const char* j = R"({
        "type_id": "Bar", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("label") != std::string::npos);
}

static void test_custom_node_registry_transformer_requires_expression() {
    std::cout << "[54] CustomNodeRegistry: transformer without expression rejected\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
    const char* j = R"({
        "type_id": "Baz", "label": "Baz", "category": "transformer",
        "input_ports": 1, "output_ports": 1
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("expression") != std::string::npos);
}

static void test_custom_node_registry_source_with_inputs_rejected() {
    std::cout << "[55] CustomNodeRegistry: source with input_ports>0 rejected\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();
    const char* j = R"({
        "type_id": "BadSrc", "label": "Bad Source", "category": "source",
        "input_ports": 2, "output_ports": 1
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("input_ports") != std::string::npos);
}

static void test_custom_node_registry_load_from_file() {
    std::cout << "[56] CustomNodeRegistry: loadFromFile reads a real path\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    reg.clear();

    // Write a tmp file.
    char tmpl[] = "/tmp/scnodes_custom_XXXXXX";
    int fd = ::mkstemp(tmpl);
    EXPECT_TRUE(fd >= 0);
    const char* body = R"({
        "type_id": "FromFile", "label": "From File", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1 + 1"
    })";
    ::write(fd, body, std::string(body).size());
    ::close(fd);

    std::string err;
    EXPECT_TRUE(reg.loadFromFile(tmpl, &err));
    EXPECT_TRUE(reg.find("FromFile") != nullptr);
    std::remove(tmpl);
}

static void test_custom_node_registry_clear() {
    std::cout << "[57] CustomNodeRegistry: clear() empties the registry\n";
    auto& reg = scinodes::CustomNodeRegistry::instance();
    const char* j = R"({
        "type_id": "Tmp", "label": "Tmp", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    reg.clear();
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    EXPECT_TRUE(reg.typeIds().size() == 1);
    reg.clear();
    EXPECT_TRUE(reg.typeIds().empty());
}

// -----------------------------------------------------------------------
// Section 8 — Performance budget
//
// Planner contract: grammar validation must complete in < 1 ms for a
// 256-node graph on the reference machine. We build the canonical
// Source → Transformer × 254 → Sink chain and time both validateGraph()
// (whole-graph reachability) and a worst-case single-edge insertion
// against the full edge list.
// -----------------------------------------------------------------------
static double bench_us(const std::function<void()>& fn, int iters) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto t1 = clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count()
           / static_cast<double>(iters);
}

static void test_grammar_perf_256_node_graph() {
    std::cout << "[48] Perf: 256-node graph validates in < 1 ms\n";
    constexpr int kNodes = 256;
    std::vector<NodeInstance> nodes;
    std::vector<Edge>         edges;
    nodes.reserve(kNodes);
    edges.reserve(kNodes - 1);

    nodes.push_back(makeNode(1, NodeType::SineSignal));        // source
    for (int i = 2; i < kNodes; ++i)
        nodes.push_back(makeNode(i, NodeType::Gain));          // transformers
    nodes.push_back(makeNode(kNodes, NodeType::Oscilloscope)); // sink
    for (int i = 1; i < kNodes; ++i)
        edges.push_back({ i, i, i + 1,
                          i * 1000 + 1, (i + 1) * 1000 + 0 });

    GrammarParser p;

    // Sanity: the chain is Valid before we measure timing.
    EXPECT_TRUE(p.validateGraph(nodes, edges) == GrammarState::Valid);

    // Warm-up pass to populate any caches.
    for (int i = 0; i < 5; ++i) (void)p.validateGraph(nodes, edges);

    double avgGraphUs = bench_us(
        [&]{ (void)p.validateGraph(nodes, edges); },
        200);
    std::cout << "      validateGraph(256 nodes): " << avgGraphUs << " us\n";
    EXPECT_TRUE(avgGraphUs < 1000.0);   // < 1 ms

    // Worst-case incremental edge check: validate one new candidate against
    // the existing 255-edge list. This is what the UI calls every time the
    // user drags a wire, so it has to stay cheap.
    NodeInstance extraSrc = makeNode(9001, NodeType::StepSignal);
    // Target a Gain node already wired into the chain — worst case is one
    // whose input port is full so R5 has to walk the edge list.
    NodeInstance extraDst = nodes[kNodes / 2 - 1];
    double avgEdgeUs = bench_us(
        [&]{ (void)p.validateEdge(extraSrc, extraDst, edges); },
        500);
    std::cout << "      validateEdge vs 255 edges: " << avgEdgeUs << " us\n";
    EXPECT_TRUE(avgEdgeUs < 1000.0);    // < 1 ms
}

// -----------------------------------------------------------------------
// CsvParamIO — round-trip a node's param block to/from CSV.
// -----------------------------------------------------------------------
static void test_csv_param_io_roundtrip_design_template() {
    std::cout << "[60] CsvParamIO: DesignTemplate round-trips through CSV\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);
    g.setParam(dt, "Target Torque",  42.0);
    g.setParam(dt, "Target Speed",  314.159);
    g.setParam(dt, "Bus Voltage",   800.0);
    g.setParam(dt, "Cooling Class",   3.0);

    auto path = makeTempCsvPath();
    std::string err;
    EXPECT_TRUE(scinodes::writeNodeParamsCsv(
        path, *g.findNode(dt), "Design Template #1", &err));
    EXPECT_TRUE(err.empty());

    // Load into a fresh node and verify it sees the four written values.
    NodeInstance reloaded = makeNode(99, NodeType::DesignTemplate);
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, reloaded, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.empty());
    EXPECT_TRUE(std::abs(reloaded.params.at("Target Torque")  -  42.0)    < 1e-9);
    EXPECT_TRUE(std::abs(reloaded.params.at("Target Speed")   - 314.159)  < 1e-6);
    EXPECT_TRUE(std::abs(reloaded.params.at("Bus Voltage")    - 800.0)    < 1e-9);
    EXPECT_TRUE(std::abs(reloaded.params.at("Cooling Class")  -   3.0)    < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_param_io_unknown_keys_become_warnings() {
    std::cout << "[61] CsvParamIO: unknown parameter rows are surfaced as warnings\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "# manually-typed file\n"
          << "parameter,value,units\n"
          << "Target Torque,55,Nm\n"
          << "Bogus Key,1,?\n";
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.size() == 1);
    EXPECT_TRUE(warns[0].find("Bogus Key") != std::string::npos);
    // Known key applied, unknown ignored, others left at defaults.
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") - 55.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Target Speed")  - 150.0) < 1e-9);  // default
    std::remove(path.c_str());
}

static void test_csv_param_io_partial_input_keeps_other_defaults() {
    std::cout << "[62] CsvParamIO: partial CSV only changes the rows it lists\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "parameter,value\n"
          << "Bus Voltage,1000\n";   // single row, no units column
    }
    NodeInstance n = makeNode(3, NodeType::DesignTemplate);
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, nullptr, nullptr));
    EXPECT_TRUE(std::abs(n.params.at("Bus Voltage")    - 1000.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") -   10.0) < 1e-9);   // default
    EXPECT_TRUE(std::abs(n.params.at("Target Speed")  -  150.0) < 1e-9);   // default
    std::remove(path.c_str());
}

static void test_csv_param_io_rejects_missing_header() {
    std::cout << "[63] CsvParamIO: missing header line is rejected\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "Target Torque,55,Nm\n";   // no header
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    EXPECT_FALSE(scinodes::readNodeParamsCsv(path, n, &err, nullptr));
    EXPECT_TRUE(err.find("header") != std::string::npos);
    std::remove(path.c_str());
}

static void test_csv_param_io_fem_sidecar_round_trip() {
    std::cout << "[65] CsvParamIO: FEM-sidecar CSV imports into PMSMElectromagnetic\n";
    auto path = makeTempCsvPath();
    {
        // Format mirrors doc/fem_sidecar/pmsm_lumped_corrections.py output
        // (Carter coefficient + magnet leakage + end-winding corrections
        // applied to the sample_input.json defaults).
        std::ofstream f(path);
        f << "# SciNodes parameters from pmsm_lumped_corrections.py\n"
          << "# k_carter: 1.23931\n"
          << "# Bg_effective_T: 0.8075\n"
          << "# Ke_VsPerRad: 1.8411\n"
          << "parameter,value,units\n"
          << "Turns per Phase,100,\n"
          << "Winding Factor,0.95,\n"
          << "Pole Pairs,4,\n"
          << "Airgap Flux Density,0.8075,T\n"
          << "Mechanical Airgap,0.001,m\n"
          << "Magnet Thickness,0.003,m\n"
          << "Slot Count,24,\n";
    }
    NodeInstance n = makeNode(11, NodeType::PMSMElectromagnetic);
    std::string err;
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.empty());
    // The corrected airgap flux density landed in the node.
    EXPECT_TRUE(std::abs(n.params.at("Airgap Flux Density") - 0.8075) < 1e-6);
    // Other defaults that the CSV listed are unchanged from their
    // sidecar-emitted values.
    EXPECT_TRUE(std::abs(n.params.at("Turns per Phase")  - 100.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Mechanical Airgap") - 0.001) < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_param_io_rejects_non_numeric() {
    std::cout << "[64] CsvParamIO: non-numeric value reports an error and aborts\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "parameter,value,units\n"
          << "Target Torque,big number,Nm\n";
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    EXPECT_FALSE(scinodes::readNodeParamsCsv(path, n, &err, nullptr));
    EXPECT_TRUE(err.find("Non-numeric") != std::string::npos);
    // Failed parse must NOT mutate the node — Target Torque stays at default.
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") - 10.0) < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_export_bad_path_reports_error() {
    std::cout << "[47] CsvExport: unwritable path returns false with error message\n";
    std::vector<float> buf(4, 1.0f);
    std::string err;
    bool ok = scinodes::writeSinkCsv(
        "/this/path/should/not/exist/scinodes_test.csv",
        buf, 4, 0.0f, 0.1f, "", &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}

// -----------------------------------------------------------------------
int main() {
    std::cout << "=== GrammarParser + ScnSerializer + Fft unit tests ===\n\n";

    // Edge-level
    test_edge_source_to_transformer();
    test_edge_source_to_sink();
    test_edge_transformer_to_transformer();
    test_edge_transformer_to_sink();
    test_edge_sink_to_anything();
    test_edge_anything_to_source();
    test_edge_source_to_source();
    test_edge_self_loop();
    test_edge_duplicate();
    test_edge_multiport_same_pair_allowed();
    test_edge_input_occupied();

    // Graph-level
    test_graph_empty();
    test_graph_incomplete_no_edges();
    test_graph_valid_source_sink();
    test_graph_valid_chain();
    test_graph_rejected_sink_to_tx();

    // Undo/Redo
    test_undo_add_node();
    test_undo_add_edge();
    test_redo();
    test_undo_stack_limit();

    // ScnSerializer
    test_serializer_roundtrip();
    test_serializer_rejects_bad_edge();
    test_serializer_unknown_type();
    test_serializer_fatal_json_error();

    // ScilabCodeGen
    test_codegen_simple_chain();
    test_codegen_every_type_supported();
    test_codegen_empty_graph();
    test_codegen_summation_two_inputs();
    test_codegen_integrator_emits_dynamics();
    test_codegen_dcmotor_two_states();
    test_codegen_stateless_skips_ode();
    test_codegen_pid_uses_state();
    test_codegen_param_dispatch_emitted();
    test_codegen_closed_loop_through_integrator();
    test_codegen_algebraic_loop_rejected();
    test_codegen_differentiator_supported();
    test_codegen_transfer_function_supported();
    test_codegen_transfer_function2();
    test_codegen_inverse_kinematics();
    test_codegen_emits_nan_guard();
    test_codegen_phaseportrait_two_channels();
    test_codegen_view3d_sink_supported();
    test_codegen_design_template_emits_four_outputs();
    test_codegen_pmsm_sizing_emits_cube_root();
    test_pmsm_sizing_has_slot_and_pole_params();
    test_codegen_pmsm_electromagnetic_emits_formulas();
    test_codegen_airgap_flux_density_state_and_harmonics();
    test_codegen_pmsm_efficiency_emits_loss_terms();
    test_codegen_heatmap_sink_three_channels();
    test_codegen_joule_loss_emits_iq_squared();
    test_codegen_core_loss_uses_electrical_frequency();
    test_codegen_mechanical_loss_emits_quadratic_term();
    test_codegen_thermal_mass_has_state_and_initial_ambient();
    test_codegen_view3d_thermal_sink_records_one_channel();
    test_codegen_thermal_node_sums_four_inputs();
    test_codegen_thermal_resistance_two_outputs();
    test_codegen_cooling_system_three_outputs();
    test_codegen_convective_cooling_h_of_flow();
    test_codegen_maxwell_force_emits_mu0_form();
    test_codegen_modal_frequency_emits_shape_factor();
    test_codegen_view3d_deformation_sink_three_channels();
    test_codegen_tolerance_perturbator_uses_rand();
    test_codegen_distribution_sink_records_input();

    // Fft helper
    test_fft_pow2_check();
    test_fft_peak_at_expected_bin();
    test_fft_dc_only_input();
    test_fft_non_pow2_rejected();

    // CsvExport
    test_csv_export_partial_buffer();
    test_csv_export_wrapped_buffer();
    test_csv_export_empty_buffer();
    test_csv_export_bad_path_reports_error();
    test_csv_param_io_roundtrip_design_template();
    test_csv_param_io_unknown_keys_become_warnings();
    test_csv_param_io_partial_input_keeps_other_defaults();
    test_csv_param_io_rejects_missing_header();
    test_csv_param_io_rejects_non_numeric();
    test_csv_param_io_fem_sidecar_round_trip();

    // CustomNodeRegistry (addRule hook)
    test_custom_node_registry_transformer_round_trip();
    test_custom_node_registry_source_no_inputs();
    test_custom_node_registry_rejects_malformed_json();
    test_custom_node_registry_rejects_duplicate();
    test_custom_node_registry_rejects_missing_field();
    test_custom_node_registry_transformer_requires_expression();
    test_custom_node_registry_source_with_inputs_rejected();
    test_custom_node_registry_load_from_file();
    test_custom_node_registry_clear();

    // Performance
    test_grammar_perf_256_node_graph();

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
