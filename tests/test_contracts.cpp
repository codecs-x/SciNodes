// =============================================================================
// test_contracts.cpp — Tests del ContractRegistry y DeviceAssetLoader.
//
// G1: parseo + validación del JSON + lookup del registry.
// G3: carga de glTF de prueba (mínimo, escrito a mano como string),
//     binding contra contrato, validación de missing/warnings.
// =============================================================================

#include "core/ContractRegistry.hpp"
#include "core/DeviceAsset.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
int g_pass = 0, g_fail = 0;

void expect_true(bool cond, const char* msg) {
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s\n", msg);
    }
}

void expect_eq(const std::string& got, const std::string& want, const char* msg) {
    if (got == want) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s — got '%s', want '%s'\n",
                     msg, got.c_str(), want.c_str());
    }
}

void expect_near(double got, double want, double tol, const char* msg) {
    if (std::fabs(got - want) <= tol) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s — got %.6f, want %.6f\n",
                     msg, got, want);
    }
}

// Helper que escribe `content` a `path` y devuelve true si OK.
bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return f.good();
}

// glTF mínimo de prueba: 2 parts (housing, shaft), 1 joint con axis Z+,
// 2 anchors con posiciones distintas.  Sin meshes ni buffers — el loader
// tolera nodes "part" sin geometría asociada (el caso de un proto que
// aún no se modeló).  Suficiente para validar el camino completo
// metadata → DeviceAsset.
const char* kMotorGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3, 4] } ],
  "nodes": [
    {
      "name": "Cuerpo",
      "extras": { "scinodes": { "role": "part", "name": "housing" } }
    },
    {
      "name": "Rotor",
      "extras": { "scinodes": { "role": "part", "name": "shaft" } }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes": {
          "role": "joint", "name": "shaft_bearing",
          "axis": [0.0, 0.0, 1.0]
        }
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.02, 0.0, 0.05],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_plus" } }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [-0.02, 0.0, 0.05],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_minus" } }
    }
  ]
})";

// Variante que usa la forma PLANA de extras (la que produce Blender por
// default al exportar Custom Properties).  Cubre el mismo grafo del
// motor pero con extras = { "scinodes.role": "...", "scinodes.name": "...",
// "scinodes.axis": [...] } en vez del objeto anidado.
const char* kMotorGltfFlatExtras = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3, 4] } ],
  "nodes": [
    {
      "name": "Cuerpo",
      "extras": { "scinodes.role": "part", "scinodes.name": "housing" }
    },
    {
      "name": "Rotor",
      "extras": { "scinodes.role": "part", "scinodes.name": "shaft" }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes.role": "joint",
        "scinodes.name": "shaft_bearing",
        "scinodes.axis": [0.0, 0.0, 1.0]
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.02, 0.0, 0.05],
      "extras": { "scinodes.role": "anchor", "scinodes.name": "terminal_plus" }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [-0.02, 0.0, 0.05],
      "extras": { "scinodes.role": "anchor", "scinodes.name": "terminal_minus" }
    }
  ]
})";

// Idem pero falta la part "housing" — para verificar que missing se llena.
const char* kMotorGltfMissingHousing = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3] } ],
  "nodes": [
    {
      "name": "Rotor",
      "extras": { "scinodes": { "role": "part", "name": "shaft" } }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes": { "role": "joint", "name": "shaft_bearing" }
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.0, 0.0, 0.0],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_plus" } }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [0.0, 0.0, 0.0],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_minus" } }
    }
  ]
})";

}  // namespace

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " test_contracts — ContractRegistry parser + lookup\n"
        "================================================================\n");

    auto& reg = scinodes::ContractRegistry::instance();
    reg.clear();

    // ---- 1. Contrato bien formado ------------------------------------------
    {
        const char* good =
            R"({
              "device_type": "MiniMotor",
              "version":     "0.1",
              "description": "Motor de prueba.",
              "parts": [
                { "name": "shaft",   "required": true },
                { "name": "housing", "required": true }
              ],
              "joints": [
                {
                  "name": "bearing", "type": "revolute",
                  "parent": "housing", "child": "shaft",
                  "driven_by": "omega"
                }
              ],
              "anchors": [
                { "name": "term_plus",  "kind": "electrical" },
                { "name": "term_minus", "kind": "electrical" }
              ]
            })";
        std::string err;
        expect_true(reg.loadFromJsonString(good, &err),
                    "contrato válido se acepta");
        expect_true(err.empty(), "no hay error al cargar válido");
        auto* c = reg.find("MiniMotor");
        expect_true(c != nullptr, "find('MiniMotor') no-nulo");
        if (c) {
            expect_eq(c->device_type, "MiniMotor", "device_type");
            expect_true(c->parts.size() == 2, "2 parts");
            expect_true(c->joints.size() == 1, "1 joint");
            expect_eq(c->joints[0].type, "revolute", "joint type");
            expect_true(c->anchors.size() == 2, "2 anchors");
        }
    }

    // ---- 2. Falta device_type ---------------------------------------------
    {
        std::string err;
        bool ok = reg.loadFromJsonString(R"({ "version": "0.1" })", &err);
        expect_true(!ok, "falla sin device_type");
        expect_true(!err.empty(), "error no vacío");
    }

    // ---- 3. Tipo de joint inválido ----------------------------------------
    {
        const char* bad =
            R"({
              "device_type": "BadJoint",
              "parts": [{ "name": "a" }, { "name": "b" }],
              "joints": [
                { "name": "x", "type": "wobble",
                  "parent": "a", "child": "b" }
              ]
            })";
        std::string err;
        expect_true(!reg.loadFromJsonString(bad, &err),
                    "joint type='wobble' rechazado");
        expect_true(err.find("not recognized") != std::string::npos,
                    "mensaje de error explica");
    }

    // ---- 4. Joint referencia part inexistente -----------------------------
    {
        const char* bad =
            R"({
              "device_type": "OrphanJoint",
              "parts": [{ "name": "real" }],
              "joints": [
                { "name": "x", "type": "fixed",
                  "parent": "real", "child": "imaginary" }
              ]
            })";
        std::string err;
        expect_true(!reg.loadFromJsonString(bad, &err),
                    "joint con child fantasma rechazado");
        expect_true(err.find("imaginary") != std::string::npos,
                    "error nombra el part faltante");
    }

    // ---- 5. Cargar el contrato real del DC motor de disco -----------------
    //  Asume que la cwd es la raíz del repo o el build dir; probamos ambas.
    {
        std::string err;
        bool loaded = reg.loadFromFile("contracts/dc_motor.json", &err);
        if (!loaded) {
            err.clear();
            loaded = reg.loadFromFile("../contracts/dc_motor.json", &err);
        }
        expect_true(loaded, "contracts/dc_motor.json carga sin error");
        auto* c = reg.find("DCMotorModel");
        expect_true(c != nullptr, "DCMotorModel en el registry");
        if (c) {
            expect_eq(c->version, "0.1", "version");
            expect_true(c->parts.size() == 2,   "DCMotorModel parts = 2");
            expect_true(c->joints.size() == 1,  "DCMotorModel joints = 1");
            expect_true(c->anchors.size() == 3, "DCMotorModel anchors = 3");
            expect_eq(c->joints[0].driven_by, "omega", "driven_by omega");
        }
    }

    // ========================================================================
    // G3 — DeviceAssetLoader
    // ========================================================================

    // Asumimos que el contrato DCMotorModel ya está en el registry (se cargó
    // en el caso 5 más arriba).  Si por alguna razón no está, los tests
    // siguientes se vuelven sin sentido.
    const scinodes::DeviceContract* dcContract = reg.find("DCMotorModel");
    expect_true(dcContract != nullptr,
                "DCMotorModel en el registry antes de los tests del loader");
    if (!dcContract) {
        std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
        return 1;
    }

    // ---- 6. Happy path: glTF que cumple el contrato ------------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_ok.gltf";
        expect_true(writeFile(path, kMotorGltf),
                    "escritura del glTF de prueba completo");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(err.empty(), "load sin error");
        expect_true(asset.valid(), "asset.valid() == true (todo cumple)");
        expect_true(asset.missing.empty(), "missing vacío");
        expect_true(asset.parts.count("shaft")   == 1, "part shaft presente");
        expect_true(asset.parts.count("housing") == 1, "part housing presente");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint shaft_bearing presente");
        expect_true(asset.anchors.count("terminal_plus") == 1,
                    "anchor terminal_plus presente");
        expect_true(asset.anchors.count("terminal_minus") == 1,
                    "anchor terminal_minus presente");

        // type/parent/child/driven_by deben copiarse desde el contrato.
        const auto& jf = asset.joints.at("shaft_bearing");
        expect_eq(jf.type,      "revolute", "joint type del contrato");
        expect_eq(jf.parent,    "housing",  "joint parent del contrato");
        expect_eq(jf.child,     "shaft",    "joint child del contrato");
        expect_eq(jf.driven_by, "omega",    "joint driven_by del contrato");
        // axis viene del glTF.
        expect_near(jf.axis[2], 1.0, 1e-6, "joint axis Z = 1");
        expect_near(jf.axis[0], 0.0, 1e-6, "joint axis X = 0");

        // anchor.kind se copia del contrato.
        expect_eq(asset.anchors.at("terminal_plus").kind,  "electrical",
                  "anchor kind del contrato");
        // anchor.position viene del glTF (terminal_plus = 0.02, 0, 0.05).
        expect_near(asset.anchors.at("terminal_plus").position[0],
                    0.02, 1e-6, "anchor terminal_plus.x");
        expect_near(asset.anchors.at("terminal_plus").position[2],
                    0.05, 1e-6, "anchor terminal_plus.z");

        // El glTF no publica `winding` (optional).  Debe estar en
        // warnings, no en missing.
        bool windingWarned = false;
        for (const auto& w : asset.warnings)
            if (w.find("winding") != std::string::npos) { windingWarned = true; break; }
        expect_true(windingWarned, "winding optional listado en warnings");

        std::remove(path.c_str());
    }

    // ---- 7. Falla un required: housing ausente ----------------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_no_housing.gltf";
        expect_true(writeFile(path, kMotorGltfMissingHousing),
                    "escritura del glTF sin housing");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(!asset.valid(), "asset.valid() == false (falta housing)");
        bool housingMissing = false;
        for (const auto& m : asset.missing)
            if (m == "part:housing") { housingMissing = true; break; }
        expect_true(housingMissing, "missing contiene 'part:housing'");
        // Lo demás SÍ está presente.
        expect_true(asset.parts.count("shaft") == 1,
                    "shaft sigue presente aunque housing falte");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint sigue presente");
        std::remove(path.c_str());
    }

    // ---- 7.5 Forma plana de extras (estilo Blender) -----------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_flat.gltf";
        expect_true(writeFile(path, kMotorGltfFlatExtras),
                    "escritura del glTF con extras planos");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(err.empty(), "load sin error (forma plana)");
        expect_true(asset.valid(),
                    "asset válido con extras planos (estilo Blender)");
        expect_true(asset.parts.count("shaft")   == 1, "shaft (flat)");
        expect_true(asset.parts.count("housing") == 1, "housing (flat)");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint (flat)");
        const auto& jf = asset.joints.at("shaft_bearing");
        expect_near(jf.axis[2], 1.0, 1e-6, "joint axis Z (flat)");
        expect_near(asset.anchors.at("terminal_plus").position[0],
                    0.02, 1e-6, "terminal_plus.x (flat)");
        std::remove(path.c_str());
    }

    // ---- 8. Archivo inexistente: todos los required quedan missing --------
    {
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(
            "/tmp/scinodes_no_such_file.gltf", *dcContract, &err);
        expect_true(!asset.valid(), "asset inválido si el archivo no existe");
        expect_true(!err.empty(),   "*err lleva el motivo de fallo");
        // Cada required del contrato debe estar en missing.
        expect_true(asset.missing.size() >= 5,
                    "missing cubre todos los required del contrato");
    }

    std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
