#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace kopi {

/// A single MIME-typed blob from the clipboard.
struct MimeData {
    std::string type;              // UTI, e.g. "public.utf8-plain-text"
    std::vector<uint8_t> bytes;
};

/// A single clipboard entry.
struct Entry {
    std::string text;                  // plain-text content (always available)
    std::vector<MimeData> mime_data;   // all MIME types (only when MIME capture is on)
    std::chrono::system_clock::time_point timestamp;
};

/// How many entries we keep in memory (and on disk).
inline constexpr size_t kMaxHistory = 500;

/// Poll interval for clipboard checking.
inline constexpr auto kPollInterval = std::chrono::milliseconds(250);

/// Application state for the clipboard manager.
class App {
public:
    App();
    ~App();

    /// Load persisted history from disk.
    void load();
    /// Save current history to disk.
    void save();

    // --- clipboard ---
    /// Poll clipboard; returns true if something new was captured.
    bool poll_clipboard();
    /// Copy an entry back to the system clipboard.
    void copy_to_clipboard(size_t index);
    /// Get the text currently on the system clipboard.
    static std::string system_clipboard();

    // --- MIME capture ---
    bool mime_capture() const { return mime_capture_; }
    void set_mime_capture(bool on) { mime_capture_ = on; }

    // --- history ---
    const std::deque<Entry>& entries() const { return entries_; }
    void delete_entry(size_t index);

    // --- selection ---
    int selected() const { return selected_; }
    void set_selected(int s);
    void move_selection(int delta);   // +1 or -1
    void select_first();
    void select_last();
    void page_down();
    void page_up();

    // --- search ---
    const std::string& search_filter() const { return search_filter_; }
    void set_search_filter(std::string f) { search_filter_ = std::move(f); recalc_filtered(); }
    bool search_mode() const { return search_mode_; }
    void set_search_mode(bool m) { search_mode_ = m; if (!m) search_input_focused_ = false; }
    bool search_input_focused() const { return search_input_focused_; }
    void set_search_input_focused(bool f) { search_input_focused_ = f; }

    /// Returns the indices of entries that pass the current filter.
    const std::vector<size_t>& filtered_indices() const { return filtered_indices_; }
    size_t filtered_count() const { return filtered_indices_.size(); }
    /// Map filtered-list position → real entry index, or SIZE_MAX.
    size_t real_index(size_t filtered_pos) const;

    // --- multi-select (indices into filtered_indices_) ---
    bool is_selected(size_t filtered_pos) const;
    bool toggle_selection(size_t filtered_pos);
    void clear_selection();
    size_t selection_count() const;
    std::string merge_selected() const;  // concatenate selected entries

private:
    void recalc_filtered();
    void add_entry(Entry e);

    std::deque<Entry> entries_;
    int selected_ = -1;              // index into filtered_indices_
    std::string search_filter_;
    bool search_mode_ = false;
    bool search_input_focused_ = false;
    std::vector<size_t> filtered_indices_;  // indices into entries_
    std::unordered_set<size_t> selected_indices_;  // indices into filtered_indices_

    std::string last_clipboard_;
    std::chrono::steady_clock::time_point last_poll_;
    int64_t last_change_count_ = 0;
    bool mime_capture_ = false;

    std::filesystem::path history_path_;
};

} // namespace kopi
