// -----------------------------------------------------------------------
// Unit tests for GrammarParser, NodeGraph, and ScnSerializer.
//
// Build: cmake --build build --target test_grammar
// Run:   ./build/test_grammar
// -----------------------------------------------------------------------
#include "../src/core/Fft.hpp"
#include "../src/core/GrammarParser.hpp"
#include "../src/core/NodeGraph.hpp"
#include "../src/core/NodeInstance.hpp"
#include "../src/core/ScilabCodeGen.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
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
    GrammarParser p;
    auto n1 = src(1); auto n2 = tx(2);
    std::vector<Edge> existing = {
        Edge{ 1, 1, 2, n1.outputAttrId(), n2.inputAttrId(0) }
    };
    auto err = p.validateEdge(n1, n2, existing);
    EXPECT_RULE(err, "R4");
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

    // Fft helper
    test_fft_pow2_check();
    test_fft_peak_at_expected_bin();
    test_fft_dc_only_input();
    test_fft_non_pow2_rejected();

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
