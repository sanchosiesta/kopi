#include "kopi.hpp"
#include "hotkey.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <unordered_set>

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

static std::vector<uint8_t> base64_decode_bytes(std::string_view in) {
    std::vector<uint8_t> out;
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
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
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

        // Tokens: timestamp, mime_count, [type, b64_data]*, text_b64
        std::vector<std::string_view> tokens;
        size_t pos = 0;
        while (pos < line.size()) {
            auto tab = line.find('\t', pos);
            if (tab == std::string::npos) {
                tokens.push_back(std::string_view(line).substr(pos));
                break;
            }
            tokens.push_back(std::string_view(line).substr(pos, tab - pos));
            pos = tab + 1;
        }
        if (tokens.size() < 2) continue;

        Entry e;
        int64_t unix_sec = std::stoll(std::string(tokens[0]));
        e.timestamp = std::chrono::system_clock::from_time_t(
            static_cast<std::time_t>(unix_sec));

        // Detect format: token[1] is a MIME count (new) or base64 text (old)
        int mime_count = 0;
        bool is_new_format = false;
        try {
            mime_count = std::stoi(std::string(tokens[1]));
            is_new_format = true;
        } catch (...) {}

        if (is_new_format) {
            size_t idx = 2;
            for (int i = 0; i < mime_count && idx + 1 < tokens.size(); ++i) {
                MimeData m;
                m.type = tokens[idx++];
                m.bytes = base64_decode_bytes(tokens[idx++]);
                e.mime_data.push_back(std::move(m));
            }
            if (idx < tokens.size())
                e.text = base64_decode(tokens[idx]);
        } else {
            e.text = base64_decode(tokens[1]);
        }

        if (!e.text.empty() || !e.mime_data.empty()) {
            entries_.push_back(std::move(e));
            if (entries_.size() >= kMaxHistory) break;
        }
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
            << e.mime_data.size();
        for (auto& m : e.mime_data) {
            out << '\t' << m.type
                << '\t' << base64_encode(std::string_view(
                    reinterpret_cast<const char*>(m.bytes.data()),
                    m.bytes.size()));
        }
        out << '\t' << base64_encode(e.text) << '\n';
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

static bool mime_data_equal(const std::vector<MimeData>& a,
                            const std::vector<MimeData>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].bytes != b[i].bytes) return false;
    }
    return true;
}

bool App::poll_clipboard() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ < kPollInterval) return false;
    last_poll_ = now;

    // Fast O(1) integer check — avoids memory allocation when idle
    int64_t current_count = kopi_get_clipboard_change_count();
    if (current_count == last_change_count_) return false;
    last_change_count_ = current_count;

    // Change confirmed — now pull the string
    std::string current = system_clipboard();

    // Capture MIME data if enabled. This must happen *before* we bail out
    // on an empty plain-text string, since rich content (images, RTF,
    // HTML, etc.) very often has no plain-text pasteboard flavor at all —
    // e.g. a screenshot or an image copied from Preview/Finder.
    std::vector<MimeData> mime;
    if (mime_capture_) {
        std::vector<std::string> types;
        std::vector<std::vector<uint8_t>> data;
        int n = kopi_capture_mime(types, data);
        mime.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            mime.push_back({std::move(types[i]), std::move(data[i])});
        }
    }

    // Nothing usable on the clipboard at all.
    if (current.empty() && mime.empty()) return false;

    // Ignore whitespace-only text blobs, but only when there's no rich
    // content riding along with them — an image/HTML/RTF copy that has
    // empty or whitespace-only plain text should still be captured.
    bool whitespace_only_text = !current.empty() &&
        std::all_of(current.begin(), current.end(),
                    [](unsigned char c) { return std::isspace(c); });
    if (mime.empty() && whitespace_only_text) return false;

    // Skip if this is exactly the same content (text + MIME payloads) as
    // what we already captured last time.
    if (current == last_clipboard_ && mime_data_equal(mime, last_mime_))
        return false;

    Entry e;
    e.text = current;
    e.mime_data = mime;
    e.timestamp = std::chrono::system_clock::now();

    last_mime_ = std::move(mime);
    add_entry(std::move(e));
    return true;
}

void App::copy_to_clipboard(size_t index) {
    if (index >= entries_.size()) return;
    const auto& e = entries_[index];

    if (!e.mime_data.empty()) {
        // Restore all MIME types for rich paste
        std::vector<std::string> types;
        std::vector<std::vector<uint8_t>> data;
        for (auto& m : e.mime_data) {
            types.push_back(m.type);
            data.push_back(m.bytes);
        }
        kopi_restore_mime(types, data);
    } else {
        SDL_SetClipboardText(e.text.c_str());
    }
}

// ============================================================================
// History management
// ============================================================================

void App::add_entry(Entry e) {
    // Remove any existing identical entries (dedup by text *and* MIME
    // payload). Comparing text alone would wrongly collapse distinct
    // image/rich-content entries that happen to share the same, often
    // empty, plain-text representation.
    std::erase_if(entries_, [&](const Entry& ex) {
        return ex.text == e.text && mime_data_equal(ex.mime_data, e.mime_data);
    });

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

// ============================================================================
// Multi-select
// ============================================================================

bool App::is_selected(size_t filtered_pos) const {
    return selected_indices_.contains(filtered_pos);
}

bool App::toggle_selection(size_t filtered_pos) {
    if (filtered_pos >= filtered_indices_.size()) return false;
    auto [it, inserted] = selected_indices_.insert(filtered_pos);
    if (!inserted) {
        selected_indices_.erase(it);
        return false;
    }
    return true;
}

void App::clear_selection() {
    selected_indices_.clear();
}

size_t App::selection_count() const {
    return selected_indices_.size();
}

std::string App::merge_selected() const {
    if (selected_indices_.empty()) return {};

    // Collect selected entries in display order
    std::vector<size_t> ordered(selected_indices_.begin(), selected_indices_.end());
    std::sort(ordered.begin(), ordered.end());

    std::string result;
    for (auto fp : ordered) {
        size_t ri = real_index(fp);
        if (ri == SIZE_MAX) continue;
        if (!result.empty()) result += '\n';
        result += entries_[ri].text;
    }
    return result;
}

} // namespace kopi
