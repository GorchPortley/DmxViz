#include "ui/LogPanel.h"

#include "ui/PanelTitles.h"

namespace dmxviz::ui {

namespace {

constexpr double kRefreshSeconds = 0.25;

ImVec4 levelColor(log::Level level) {
    switch (level) {
        case log::Level::Debug:
            return ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
        case log::Level::Info:
            return ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
        case log::Level::Warn:
            return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
        case log::Level::Error:
            return ImVec4(1.00f, 0.40f, 0.35f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

}  // namespace

const char* LogPanel::title() const {
    return kLogTitle;
}

void LogPanel::refreshEntries() {
    const double now = ImGui::GetTime();
    if (lastRefresh_ >= 0.0 && now - lastRefresh_ < kRefreshSeconds) return;
    lastRefresh_ = now;
    entries_ = log::recent(1000);
}

void LogPanel::draw(EditorContext& ctx) {
    (void)ctx;
    refreshEntries();

    // Toolbar: minimum level, text filter, clear, auto-scroll.
    static constexpr log::Level kLevels[] = {log::Level::Debug, log::Level::Info, log::Level::Warn, log::Level::Error};
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::BeginCombo("##level", log::levelName(minLevel_))) {
        for (log::Level level : kLevels)
            if (ImGui::Selectable(log::levelName(level), level == minLevel_)) minLevel_ = level;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Show messages of this level and above");
    ImGui::SameLine();
    textFilter_.Draw("##filter", 180.0f);
    ImGui::SameLine();
    if (ImGui::Button("Clear") && !entries_.empty()) clearedBefore_ = entries_.back().timeSeconds;
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll_);

    ImGui::Separator();
    ImGui::BeginChild("##logLines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const log::Entry& e : entries_) {
        if (e.timeSeconds <= clearedBefore_ || e.level < minLevel_) continue;
        if (textFilter_.IsActive() && !textFilter_.PassFilter(e.message.c_str()) &&
            !textFilter_.PassFilter(e.category.c_str()))
            continue;
        ImGui::PushStyleColor(ImGuiCol_Text, levelColor(e.level));
        ImGui::Text("%8.2f  %-5s  [%s] %s", e.timeSeconds, log::levelName(e.level), e.category.c_str(),
                    e.message.c_str());
        ImGui::PopStyleColor();
    }
    // Follow new messages only while the user has not scrolled away from the end.
    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

}  // namespace dmxviz::ui
