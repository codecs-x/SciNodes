#pragma once

#include "../core/AssetMapping.hpp"
#include "../core/ContractRegistry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// AssetMappingPanel — modal ImGui para vincular una geometría 3D (glTF/GLB)
// a las ranuras del contrato del dispositivo, generando el sidecar JSON.
//
// Caso de uso: el usuario tiene un .gltf venido de SolidWorks/FreeCAD/etc.
// sin la metadata `extras.scinodes` que el flujo Blender produce.  Este
// panel le permite asignar manualmente "este nodo del glTF = shaft",
// "ese empty = el bearing con eje +Y", etc., guardando todo en
// `<asset>.mapping.json` al lado del archivo.  El binario glTF queda
// intacto.
//
// Uso por NodeCanvas:
//   1. canvas.assetMappingPanel().openFor(path, contract);
//   2. cada frame: if (canvas.assetMappingPanel().drawFrame()) {
//                       // usuario aplicó: persistir y recargar
//                       const auto& m = canvas.assetMappingPanel().result();
//                       std::string err;
//                       m.saveToFile(canvas.assetMappingPanel().sidecarPath(),
//                                    &err);
//                       canvas.reloadAsset(nodeId);
//                   }
//
// La modal se cierra sola al pulsar Aplicar o Cancelar.  El consumidor
// solo necesita pollear isOpen()/drawFrame() — no debe manipular el
// estado interno.
// -----------------------------------------------------------------------------
class AssetMappingPanel {
public:
    AssetMappingPanel();

    // Re-parsea el glTF para listar sus nodos, pre-llena el mapping con
    // lo que ya haya en `<asset>.mapping.json` si existe, y abre la
    // modal en el próximo drawFrame().
    //
    // Devuelve false (panel no se abre) si:
    //   - assetPath está vacío,
    //   - tinygltf no puede parsear el archivo,
    //   - el contrato no tiene ningún slot que mapear (vacío).
    bool openFor(const std::string&            assetPath,
                 const scinodes::DeviceContract& contract);

    bool isOpen() const { return m_open; }

    // Renderiza la modal si está abierta.  Devuelve true UNA sola vez,
    // el frame en que el usuario haga click en "Aplicar y guardar".
    // En el mismo frame, la modal se cierra y result()/sidecarPath()
    // quedan disponibles.
    bool drawFrame();

    // Datos del último Apply.
    const scinodes::AssetMapping& result()      const { return m_mapping; }
    const std::string&            assetPath()   const { return m_assetPath; }
    const std::string&            sidecarPath() const { return m_sidecarPath; }

    // Modo del eje de un joint en la UI.  Las seis primeras opciones son
    // presets para ahorrar al usuario tipear (0,1,0); Custom desbloquea
    // tres inputs y guarda los valores tal cual.  Pública por
    // conveniencia (helpers de .cpp en namespace anónimo la nombran).
    enum class AxisMode { PX, NX, PY, NY, PZ, NZ, Custom };

private:

    // Helpers
    void inferAxisModeFromMapping();
    void applyAxisModeToSlot(const std::string& jointName);
    static AxisMode axisModeFromVec(const std::array<float, 3>& v);
    static std::array<float, 3> vecFromAxisMode(AxisMode m);

    // Dibuja un dropdown de nodos para una ranura dada.  Modifica
    // `out` al seleccionar.  `required` marca el slot como obligatorio
    // (afecta al cálculo de validez para habilitar Aplicar).
    bool drawNodeDropdown(const char*    label,
                          std::string&   out,
                          bool           required);

    // ---- estado ----
    bool m_open       = false;
    bool m_justOpened = false;  // primer frame tras openFor()
    bool m_applied    = false;  // se setea cuando Apply fue presionado

    std::string                  m_assetPath;
    std::string                  m_sidecarPath;
    scinodes::DeviceContract     m_contract;
    scinodes::AssetMapping       m_mapping;
    std::vector<std::string>     m_nodeNames;

    // Por-joint: modo de selección de eje (preset vs custom).
    std::unordered_map<std::string, AxisMode> m_jointAxisMode;
};
