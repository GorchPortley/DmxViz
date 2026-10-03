#include "ui/FileDialogs.h"

#include "core/Log.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#endif

#include "portable-file-dialogs.h"

namespace dmxviz::ui {

namespace {

// pfd speaks UTF-8 strings; std::filesystem::path needs explicit conversion on Windows.
std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

struct FileDialogs::Impl {
    std::unique_ptr<pfd::open_file> open;
    std::unique_ptr<pfd::save_file> save;
};

FileDialogs::FileDialogs() : impl_(std::make_unique<Impl>()) { pfd::settings::verbose(false); }
FileDialogs::~FileDialogs() = default;

bool FileDialogs::available() { return pfd::settings::available(); }

bool FileDialogs::busy() const { return impl_->open != nullptr || impl_->save != nullptr; }

bool FileDialogs::requestOpen(const std::string& title, const std::filesystem::path& startPath,
                              const std::vector<std::string>& filters) {
    if (busy()) return false;
    if (!available()) {
        log::warn("ui", "no native file dialog available (on Linux install zenity or kdialog)");
        return false;
    }
    impl_->open = std::make_unique<pfd::open_file>(title, pathToUtf8(startPath), filters);
    return true;
}

bool FileDialogs::requestSave(const std::string& title, const std::filesystem::path& startPath,
                              const std::vector<std::string>& filters) {
    if (busy()) return false;
    if (!available()) {
        log::warn("ui", "no native file dialog available (on Linux install zenity or kdialog)");
        return false;
    }
    impl_->save = std::make_unique<pfd::save_file>(title, pathToUtf8(startPath), filters);
    return true;
}

std::optional<FileDialogs::Result> FileDialogs::poll() {
    Result result;
    if (impl_->open) {
        if (!impl_->open->ready(0)) return std::nullopt;
        const std::vector<std::string> files = impl_->open->result();
        result.kind = Kind::Open;
        if (!files.empty()) result.path = pathFromUtf8(files.front());
        impl_->open.reset();
        return result;
    }
    if (impl_->save) {
        if (!impl_->save->ready(0)) return std::nullopt;
        const std::string file = impl_->save->result();
        result.kind = Kind::Save;
        if (!file.empty()) result.path = pathFromUtf8(file);
        impl_->save.reset();
        return result;
    }
    return std::nullopt;
}

}  // namespace dmxviz::ui
