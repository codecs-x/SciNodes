#pragma once
#include <atomic>
#include <mutex>
#include <string>

// -----------------------------------------------------------------------
// FileDialog — async native file picker via zenity (GTK) or kdialog (KDE).
//
// The dialog runs in a background thread so the render loop is never
// blocked. Usage pattern (called every frame):
//
//   if (ImGui::Button("Open"))
//       dlg.open(FileDialog::Mode::Open, "Open Graph",
//                {"SciNodes graph", "*.scn"});
//
//   if (dlg.isOpen()) /* show "Opening..." */ ;
//   if (std::string p = dlg.take(); !p.empty()) handleResult(p);
//
// take() consumes the result once: a subsequent call without a new open()
// returns an empty string.
// -----------------------------------------------------------------------
class FileDialog {
public:
    enum class Mode { Open, Save };

    struct Filter {
        std::string label;      // e.g. "SciNodes graph (*.scn)"
        std::string patterns;   // space-separated globs, e.g. "*.scn"
    };

    // Launch the dialog. No-op if one is already in flight.
    void open(Mode mode,
              const std::string& title,
              const Filter& filter,
              const std::string& suggestedName = "");

    bool isOpen() const { return m_busy.load(); }

    // Consume the pending result (path picked by the user). Returns "" if
    // nothing is ready or the user cancelled.
    std::string take();

private:
    std::atomic<bool> m_busy{false};
    std::mutex        m_mtx;
    std::string       m_pending;
};
