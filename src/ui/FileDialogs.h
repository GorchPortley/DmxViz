#pragma once
// FileDialogs: native open/save dialogs (portable-file-dialogs) that do not
// block the frame loop. Ask for a dialog, then call poll() once per frame until
// it returns the result.
//
//   dialogs.requestSave("Save project", dir, {"DmxViz projects", "*.dmxviz"});
//   ...every frame:
//   if (auto result = dialogs.poll()) { if (!result->path.empty()) save(result->path); }
//
// Only one dialog can be open at a time. The pfd header stays inside the .cpp.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dmxviz::ui {

class FileDialogs {
public:
    enum class Kind { Open, Save };

    struct Result {
        Kind kind = Kind::Open;
        std::filesystem::path path;  // empty when the user cancelled
    };

    FileDialogs();
    ~FileDialogs();
    FileDialogs(const FileDialogs&) = delete;
    FileDialogs& operator=(const FileDialogs&) = delete;

    // False when the system has no dialog helper (e.g. no zenity on Linux).
    static bool available();
    // Turns native dialogs off for the whole process (unit tests, headless screenshot runs): a real
    // dialog would wait for a user who is not there.
    static void setEnabled(bool enabled);
    bool busy() const;

    // `filters` alternates a description and a glob list: {"Projects", "*.dmxviz", "All files", "*"}.
    // Returns false if a dialog is already open or none is available.
    bool requestOpen(const std::string& title, const std::filesystem::path& startPath,
                     const std::vector<std::string>& filters);
    bool requestSave(const std::string& title, const std::filesystem::path& startPath,
                     const std::vector<std::string>& filters);

    // The finished dialog's result, once; std::nullopt while it is still open or when none is open.
    std::optional<Result> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dmxviz::ui
