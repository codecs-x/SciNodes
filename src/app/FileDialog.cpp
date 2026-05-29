#include "FileDialog.hpp"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

// ---------------------------------------------------------------------------
// Run a shell command, return its first line of stdout (path), or "" on
// error / cancellation. Used to invoke zenity or kdialog.
// ---------------------------------------------------------------------------
static std::string runDialogCommand(const std::string& cmd) {
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[4096] = {};
    bool ok = (std::fgets(buf, sizeof(buf), f) != nullptr);
    int  rc = pclose(f);
    if (!ok || rc != 0) return {};
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

// Single-quote-escape an arg for inclusion inside a shell string-literal.
//   foo'bar  ->  foo'\''bar
static std::string sh(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 4);
    for (char c : in) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    return out;
}

void FileDialog::open(Mode mode,
                      const std::string& title,
                      const Filter& filter,
                      const std::string& suggestedName) {
    if (m_busy.load()) return;
    m_busy.store(true);

    std::thread([this, mode, title, filter, suggestedName]() {
        std::string result;

        // --- zenity (GTK / GNOME) ----------------------------------------
        {
            std::ostringstream cmd;
            cmd << "zenity --file-selection";
            if (mode == Mode::Save) cmd << " --save --confirm-overwrite";
            cmd << " --title='" << sh(title) << "'";
            if (!filter.label.empty() && !filter.patterns.empty())
                cmd << " --file-filter='"
                    << sh(filter.label) << " | " << sh(filter.patterns) << "'";
            if (mode == Mode::Save && !suggestedName.empty())
                cmd << " --filename='" << sh(suggestedName) << "'";
            cmd << " 2>/dev/null";
            result = runDialogCommand(cmd.str());
        }

        // --- kdialog fallback --------------------------------------------
        if (result.empty()) {
            std::ostringstream cmd;
            if (mode == Mode::Save)
                cmd << "kdialog --getsavefilename";
            else
                cmd << "kdialog --getopenfilename";
            cmd << " '" << sh(suggestedName.empty() ? std::string(".") : suggestedName) << "'";
            if (!filter.patterns.empty())
                cmd << " '" << sh(filter.patterns)
                    << "|" << sh(filter.label) << "'";
            cmd << " 2>/dev/null";
            result = runDialogCommand(cmd.str());
        }

        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_pending = std::move(result);
        }
        m_busy.store(false);
    }).detach();
}

std::string FileDialog::take() {
    if (m_busy.load()) return {};
    std::lock_guard<std::mutex> lock(m_mtx);
    std::string out = std::move(m_pending);
    m_pending.clear();
    return out;
}
