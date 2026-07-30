#include "kopi.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace kopi {

// ============================================================================
// Utility: simple base64 encode / decode
// ============================================================================

static const char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(std::string_view in) {
    std::string out;
    out.reserve(4 * ((in.size() + 2) / 3));
    int val = 0;
    int bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kBase64[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6)
        out.push_back(kBase64[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

static std::string base64_decode(std::string_view in) {
    std::string out;
    out.reserve(3 * in.size() / 4);
    int val = 0;
    int bits = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        auto p = std::find(std::begin(kBase64), std::end(kBase64), c);
        if (p == std::end(kBase64)) continue;
        val = (val << 6) + static_cast<int>(p - std::begin(kBase64));
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ============================================================================
// Persistence: store in ~/.local/share/kopi/history.dat
// Format: one entry per line:  timestamp<TAB>base64_content
// ============================================================================

static fs::path data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "kopi";
    const char* home = std::getenv("HOME");
    if (home && *home) return fs::path(home) / ".local/share/kopi";
    return fs::path(".") / "kopi_data";
}

App::App() : history_path_(data_dir() / "history.dat") {
    load();
    // Seed last poll time so we poll immediately on first frame.
    last_poll_ = std::chrono::steady_clock::now() - kPollInterval;
}

App::~App() { save(); }

void App::load() {
    entries_.clear();
    if (!fs::exists(history_path_)) return;

    std::ifstream in(history_path_);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // Split on first TAB
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;

        std::string ts_str = line.substr(0, tab);
        std::string content = base64_decode(line.substr(tab + 1));

        Entry e;
        e.text = std::move(content);
        // Timestamp stored as Unix time integer (seconds)
        int64_t unix_sec = std::stoll(ts_str);
        e.timestamp = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(unix_sec));
        entries_.push_back(std::move(e));
        if (entries_.size() >= kMaxHistory) break;
    }
    recalc_filtered();
    if (!filtered_indices_.empty()) selected_ = 0;
}

void App::save() {
    fs::create_directories(history_path_.parent_path());
    std::ofstream out(history_path_);
    for (auto& e : entries_) {
        auto unix_sec = std::chrono::system_clock::to_time_t(e.timestamp);
        out << static_cast<int64_t>(unix_sec) << '\t'
            << base64_encode(e.text) << '\n';
    }
}

// ============================================================================
// Clipboard
// ============================================================================

std::string App::system_clipboard() {
    char* raw = SDL_GetClipboardText();
    if (!raw) return {};
    std::string s(raw);
    SDL_free(raw);
    return s;
}

bool App::poll_clipboard() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ < kPollInterval) return false;
    last_poll_ = now;

    std::string current = system_clipboard();
    if (current.empty() || current == last_clipboard_) return false;

    add_entry(std::move(current));
    return true;
}

void App::copy_to_clipboard(size_t index) {
    if (index >= entries_.size()) return;
    SDL_SetClipboardText(entries_[index].text.c_str());
}

// ============================================================================
// History management
// ============================================================================

void App::add_entry(std::string text) {
    // Remove any existing identical entries (full dedup).
    std::erase_if(entries_, [&](const Entry& e) { return e.text == text; });

    // Add at front.
    Entry e;
    e.text      = text;
    e.timestamp = std::chrono::system_clock::now();
    entries_.push_front(std::move(e));
    last_clipboard_ = entries_.front().text; // copy

    // Trim.
    while (entries_.size() > kMaxHistory)
        entries_.pop_back();

    recalc_filtered();
    if (!filtered_indices_.empty()) selected_ = 0;
    save();
}

void App::delete_entry(size_t index) {
    if (index >= entries_.size()) return;
    entries_.erase(entries_.begin() + static_cast<long>(index));
    recalc_filtered();
    if (selected_ >= static_cast<int>(filtered_indices_.size()))
        selected_ = static_cast<int>(filtered_indices_.size()) - 1;
    if (selected_ < 0 && !filtered_indices_.empty()) selected_ = 0;
    save();
}

// ============================================================================
// Selection
// ============================================================================

void App::set_selected(int s) {
    if (filtered_indices_.empty()) {
        selected_ = -1;
        return;
    }
    selected_ = std::clamp(s, 0, static_cast<int>(filtered_indices_.size()) - 1);
}

void App::move_selection(int delta) {
    set_selected(selected_ + delta);
}

void App::select_first() { set_selected(0); }
void App::select_last() {
    set_selected(static_cast<int>(filtered_indices_.size()) - 1);
}

void App::page_down() {
    set_selected(selected_ + 15);
}

void App::page_up() {
    set_selected(selected_ - 15);
}

// ============================================================================
// Filtering
// ============================================================================

void App::recalc_filtered() {
    filtered_indices_.clear();
    if (search_filter_.empty()) {
        filtered_indices_.reserve(entries_.size());
        for (size_t i = 0; i < entries_.size(); ++i)
            filtered_indices_.push_back(i);
        return;
    }
    // Case-insensitive substring match.
    auto lower_filter = search_filter_;
    std::transform(lower_filter.begin(), lower_filter.end(),
                   lower_filter.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto lower_text = entries_[i].text;
        std::transform(lower_text.begin(), lower_text.end(),
                       lower_text.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_text.find(lower_filter) != std::string::npos)
            filtered_indices_.push_back(i);
    }
}

size_t App::real_index(size_t filtered_pos) const {
    if (filtered_pos >= filtered_indices_.size()) return SIZE_MAX;
    return filtered_indices_[filtered_pos];
}

} // namespace kopi
