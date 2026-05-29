#pragma once

#include "NodeCanvas.hpp"

#include <string>

// -----------------------------------------------------------------------------
// AboutGraphPanel — ventana "Sobre este grafo" para leer y editar la
// metadata root del documento (id read-only, title, description, tags).
//
// La metadata vive embebida en el `.scn` (commits 60e3b86/d64d6f2) — esta
// ventana es el punto de entrada de usuario para llenarla.  Se abre desde
// `Ayuda → Sobre este grafo…` y persiste al apretar Aplicar; Cancelar
// descarta los cambios locales sin tocar el modelo.
//
// Diseño:
//   - El panel mantiene un buffer LOCAL editable; sólo en Apply lo copia
//     al modelo vía `NodeCanvas::setGraphTitle/Description/Tags`.  Eso
//     evita bumpear dirty en cada keystroke (lo que dispararía un check
//     de hot-reload del SimController por nada).
//   - El id es read-only — es identidad del documento, no algo que el
//     usuario edite en vivo.  Cambiar el id rompería referencias
//     potenciales desde otros .scn o desde tools externas.
//   - Tags se editan como una sola línea comma-separated.  Trim de
//     espacios al persistir.  Vacíos se filtran.
// -----------------------------------------------------------------------------
class AboutGraphPanel {
public:
    AboutGraphPanel() = default;

    // Lee el estado actual del canvas y abre la ventana.  Llamar cada
    // vez que el usuario hace clic en Ayuda → Sobre este grafo: garantiza
    // que el panel muestre la metadata más reciente.
    void open(const NodeCanvas& canvas);
    void close() { m_open = false; }

    // Render por frame.  Si está cerrado, no-op.
    void draw(NodeCanvas& canvas);

private:
    bool        m_open = false;
    // ImGui `InputTextMultiline` no soporta word-wrap nativamente — un
    // párrafo largo sin saltos manuales scrollea horizontalmente,
    // contra-intuitivo para el caso de uso "leo y edito un comentario".
    // Resolución: dual-mode controlado por este flag.  En `false`
    // (default) mostramos la descripción como `TextWrapped` (siempre
    // wrapea, scroll vertical si pasa los 10 renglones).  En `true`
    // intercambia por `InputTextMultiline` para editar.  El usuario
    // alterna con Editar / Listo.
    bool        m_editingDescription = false;
    // Buffers locales que reflejan los campos editables.  Se sincronizan
    // desde el modelo al abrir y se aplican al modelo en Apply.
    char        m_titleBuf[256]      = {};
    char        m_tagsBuf[512]       = {};
    std::string m_descriptionBuf;            // multilínea → string para evitar tope
    std::string m_idView;                    // sólo lectura
};
