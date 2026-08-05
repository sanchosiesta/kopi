#include "hotkey_macos.hpp"
#include "kopi.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// ============================================================================
// Forward declarations
// ============================================================================

static bool init_sdl(SDL_Window*& window, SDL_Renderer*& renderer);
static void cleanup_sdl(SDL_Window* window, SDL_Renderer* renderer);
static void render_ui(kopi::App& app, SDL_Window* window, bool& done, bool& visible);
static bool handle_vim_keys(kopi::App& app, SDL_Window* window, bool& visible);
static void draw_frame(SDL_Renderer* renderer);

// ============================================================================
// main
// ============================================================================

int main(int, char**) {
    // --- Init SDL3 + ImGui ---
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!init_sdl(window, renderer)) return 1;

    // --- Request Accessibility permission (shows system dialog if needed) ---
    kopi_request_accessibility();

    // --- Register global hotkey (Cmd+8) ---
    Uint32 hotkey_event = kopi_register_hotkey();

    // --- Create menu bar icon ---
    kopi_init_status_bar();

    // --- Our app ---
    kopi::App app;

    // Window created hidden (SDL_WINDOW_HIDDEN) — no need to hide here
    bool window_visible = false;

    // --- Main loop ---
    bool done = false;

    while (!done) {
        // Block waiting for events.  When visible we wake every 16ms to
        // render a frame; when hidden we wake every 250ms to poll clipboard.
        // This is much more power-efficient than PollEvent + SDL_Delay.
        SDL_WaitEventTimeout(nullptr, window_visible ? 16 : 250);

        // Process all SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Intercept Ctrl+J/K before ImGui — works even when InputText is focused
            if (window_visible &&
                event.type == SDL_EVENT_KEY_DOWN &&
                (event.key.mod & SDL_KMOD_CTRL)) {
                if (event.key.key == SDLK_J) {
                    app.move_selection(+1);
                    continue;  // don't pass to ImGui
                }
                if (event.key.key == SDLK_K) {
                    app.move_selection(-1);
                    continue;  // don't pass to ImGui
                }
            }

            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }

            // Global hotkey: toggle window visibility
            if (hotkey_event && event.type == hotkey_event) {
                window_visible = !window_visible;
                if (window_visible) {
                    kopi_show_window(window);
                } else {
                    kopi_hide_window(window);
                }
            }

            // Ctrl+Q quits (only when window is visible)
            if (window_visible &&
                event.type == SDL_EVENT_KEY_DOWN &&
                event.key.key == SDLK_Q &&
                (event.key.mod & SDL_KMOD_CTRL)) {
                done = true;
            }
        }

        // Always poll clipboard (even when hidden)
        app.poll_clipboard();

        // Only render when visible
        if (window_visible) {
            ImGui_ImplSDL3_NewFrame();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui::NewFrame();

            render_ui(app, window, done, window_visible);

            draw_frame(renderer);
        }
    }

    // --- Shutdown ---
    kopi_remove_status_item();
    kopi_unregister_hotkey();
    cleanup_sdl(window, renderer);
    return 0;
}

// ============================================================================
// Draw one ImGui frame
// ============================================================================

static void draw_frame(SDL_Renderer* renderer) {
    ImGui::Render();
    SDL_SetRenderScale(renderer,
                       ImGui::GetIO().DisplayFramebufferScale.x,
                       ImGui::GetIO().DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 240, 240, 245, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

// ============================================================================
// SDL3 + ImGui init
// ============================================================================

static bool init_sdl(SDL_Window*& window, SDL_Renderer*& renderer) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init error: %s", SDL_GetError());
        return false;
    }

    // Create window
    window = SDL_CreateWindow("kopi — clipboard manager",
                              900, 600,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Log("SDL_CreateWindow error: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer error: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Cap to display refresh rate (60 Hz typically) — don't burn CPU
    SDL_SetRenderVSync(renderer, 1);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't persist imgui state

    // Style
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;

    // Load a good monospace font at a readable size
    io.Fonts->AddFontFromFileTTF(
        "/System/Library/Fonts/Menlo.ttc", 16.0f);
    // Fallback if Menlo isn't found
    if (io.Fonts->Fonts.empty()) {
        io.Fonts->AddFontDefault();
    }

    // Init backends
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
        SDL_Log("ImGui_ImplSDL3_InitForSDLRenderer error");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
        SDL_Log("ImGui_ImplSDLRenderer3_Init error");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    return true;
}

static void cleanup_sdl(SDL_Window* window, SDL_Renderer* renderer) {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// ============================================================================
// Vim key handling
// ============================================================================

// When search mode is active, only Escape and Ctrl+Q are handled here;
// everything else goes to the search InputText.
// When search mode is inactive, all vim navigation keys are active.

static bool handle_vim_keys(kopi::App& app, SDL_Window* window, bool& visible) {
    ImGuiIO& io = ImGui::GetIO();

    // ------------------------------------------------------------------
    // Keys that ALWAYS work, even in search mode
    // ------------------------------------------------------------------

    // --- Escape / Ctrl+[ ---
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) &&
         ImGui::IsKeyDown(ImGuiKey_LeftCtrl))) {
        if (app.search_input_focused()) {
            // Exit search input → focus returns to list, filter stays
            app.set_search_mode(false);
            return true;
        }
        // Normal mode: hide window
        kopi_hide_window(window);
        visible = false;
        return true;
    }

    // ------------------------------------------------------------------
    // Keys that only work when search input is NOT focused
    // ------------------------------------------------------------------
    if (app.search_input_focused())
        return false;

    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k); };

    // --- Navigation ---
    if (pressed(ImGuiKey_J) || pressed(ImGuiKey_DownArrow)) {
        app.move_selection(+1);
        return true;
    }
    if (pressed(ImGuiKey_K) || pressed(ImGuiKey_UpArrow)) {
        app.move_selection(-1);
        return true;
    }

    // --- Page up / down ---
    if (pressed(ImGuiKey_D) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
        app.page_down();
        return true;
    }
    if (pressed(ImGuiKey_U) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) {
        app.page_up();
        return true;
    }

    // --- G: go to bottom ---
    if (pressed(ImGuiKey_G) && ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
        app.select_last();
        return true;
    }

    // --- gg: double-tap g to go to top ---
    {
        static bool waiting_gg = false;
        static auto  last_g    = std::chrono::steady_clock::now();
        if (pressed(ImGuiKey_G)) {
            auto now = std::chrono::steady_clock::now();
            if (waiting_gg && now - last_g < std::chrono::milliseconds(500)) {
                app.select_first();
                waiting_gg = false;
                return true;
            }
            waiting_gg = true;
            last_g = now;
            return true;
        }
        // Any other navigation key resets the gg prefix
        if (waiting_gg && (pressed(ImGuiKey_J) || pressed(ImGuiKey_K) ||
                           pressed(ImGuiKey_Escape))) {
            waiting_gg = false;
        }
    }

    // --- dd: double-tap d to delete ---
    {
        static bool waiting_dd = false;
        static auto  last_d    = std::chrono::steady_clock::now();
        if (pressed(ImGuiKey_D)) {
            auto now = std::chrono::steady_clock::now();
            if (waiting_dd && now - last_d < std::chrono::milliseconds(500)) {
                if (auto ri = app.real_index(app.selected()); ri != SIZE_MAX)
                    app.delete_entry(ri);
                waiting_dd = false;
                return true;
            }
            waiting_dd = true;
            last_d = now;
            return true;
        }
        if (waiting_dd && (pressed(ImGuiKey_J) || pressed(ImGuiKey_K) ||
                           pressed(ImGuiKey_Escape))) {
            waiting_dd = false;
        }
    }

    // --- /  enter search mode ---
    if (pressed(ImGuiKey_Slash)) {
        app.set_search_mode(true);
        app.set_search_filter("");
        return true;
    }

    // --- z prefix: track for zz / zt / zb ---
    {
        static auto last_z_time = std::chrono::steady_clock::now();
        if (pressed(ImGuiKey_Z)) {
            auto now = std::chrono::steady_clock::now();
            if (app.last_key_was_z() &&
                now - last_z_time < std::chrono::milliseconds(500)) {
                app.set_scroll_pos(kopi::App::ScrollPos::Center);
                app.set_last_key_z(false);
                return true;
            }
            app.set_last_key_z(true);
            last_z_time = now;
            return true;
        }
        if (pressed(ImGuiKey_T) && app.last_key_was_z()) {
            app.set_scroll_pos(kopi::App::ScrollPos::Top);
            app.set_last_key_z(false);
            return true;
        }
        if (pressed(ImGuiKey_B) && app.last_key_was_z()) {
            app.set_scroll_pos(kopi::App::ScrollPos::Bottom);
            app.set_last_key_z(false);
            return true;
        }
        // Any other key resets the z prefix
        if (app.last_key_was_z() &&
            (pressed(ImGuiKey_J) || pressed(ImGuiKey_K) ||
             pressed(ImGuiKey_Escape) || pressed(ImGuiKey_Slash) ||
             pressed(ImGuiKey_Enter) || pressed(ImGuiKey_Y) ||
             pressed(ImGuiKey_L) || pressed(ImGuiKey_Space) ||
             pressed(ImGuiKey_D) || pressed(ImGuiKey_G))) {
            app.set_last_key_z(false);
        }
    }

    // --- Space: toggle selection on current item ---
    if (pressed(ImGuiKey_Space)) {
        app.toggle_selection(app.selected());
        return true;
    }

    // --- Enter / y / l: paste selected items, or single item ---
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) ||
        pressed(ImGuiKey_Y) || pressed(ImGuiKey_L)) {
        if (app.selection_count() > 0) {
            std::string merged = app.merge_selected();
            if (!merged.empty()) {
                app.clear_selection();
                kopi_hide_window(window);
                visible = false;
                kopi_copy_and_paste(merged.c_str());
            }
        } else if (auto ri = app.real_index(app.selected()); ri != SIZE_MAX) {
            // Restore MIME data (or plain text) to clipboard, then paste
            app.copy_to_clipboard(ri);
            kopi_hide_window(window);
            visible = false;
            kopi_paste();
        }
        return true;
    }

    return false;
}

// ============================================================================
// ImGui UI
// ============================================================================

static void render_ui(kopi::App& app, SDL_Window* window, bool& done, bool& visible) {
    handle_vim_keys(app, window, visible);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::Begin("##kopi_main", nullptr, wflags);

    // --- Menu bar ---
    if (ImGui::BeginMenuBar()) {
        ImGui::TextUnformatted("kopi");
        ImGui::Separator();
        ImGui::Text("entries: %zu", app.filtered_count());
        ImGui::EndMenuBar();
    }

    // --- Search bar ---
    {
        char buf[256];
        std::strncpy(buf, app.search_filter().c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        ImGui::SetNextItemWidth(-1.0f);
        if (app.search_mode()) {
            ImGui::SetKeyboardFocusHere(0);
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;
        if (ImGui::InputTextWithHint("##search", "/ search …", buf,
                                     sizeof(buf), flags)) {
            app.set_search_filter(buf);
        }
        app.set_search_input_focused(ImGui::IsItemActive());

        // Enter pressed while search is active → confirm, exit, select first
        if (app.search_mode() && ImGui::IsItemFocused() &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            app.set_search_mode(false);
            app.set_selected(0);
        }

        if (app.search_mode() && !ImGui::IsItemActive()) {
            // User clicked away — exit search mode but keep filter
            app.set_search_mode(false);
        }

        ImGui::SameLine();
        if (ImGui::Button("clear")) {
            app.set_search_mode(false);
            app.set_search_filter("");
        }
    }

    ImGui::Separator();

    // --- Entry list ---
    const auto& entries = app.entries();
    const auto& filtered = app.filtered_indices();
    int sel = app.selected();

    // Compute visible height
    float avail_h = ImGui::GetContentRegionAvail().y;
    float footer_h = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
    float list_h = avail_h - footer_h;
    if (list_h < 100.0f) list_h = 100.0f;

    ImGui::BeginChild("##entry_list", ImVec2(0, list_h),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);

    float line_h = ImGui::GetTextLineHeightWithSpacing();
    constexpr int kMaxLines = 10;
    constexpr float kPad = 4.0f;

    // Pre-compute line counts and heights for each visible item
    struct ItemInfo {
        int lines;
        float height;
        std::string preview;  // truncated text
    };
    std::vector<ItemInfo> infos(filtered.size());
    float total_h = 0;
    for (size_t i = 0; i < filtered.size(); ++i) {
        size_t ri = filtered[i];
        const auto& e = entries[ri];

        // Count preview lines (max kMaxLines), +1 for the header line
        int lc = 1;
        for (size_t pos = 0; pos < e.text.size(); ) {
            auto nl = e.text.find('\n', pos);
            if (nl == std::string::npos || lc >= kMaxLines) break;
            lc++;
            pos = nl + 1;
        }
        int total_lines = std::min(lc + 1, kMaxLines + 1);  // header + preview

        // Truncate preview to kMaxLines
        std::string prev = e.text;
        int seen = 0;
        size_t cut = 0;
        while (seen < kMaxLines && cut < prev.size()) {
            auto nl = prev.find('\n', cut);
            if (nl == std::string::npos) { seen++; break; }
            cut = nl + 1;
            seen++;
        }
        if (cut > 0 && cut < prev.size()) prev = prev.substr(0, cut) + "…";
        if (prev.size() > 200) prev = prev.substr(0, 197) + "…";

        float h = total_lines * line_h + kPad;
        infos[i] = {total_lines, h, std::move(prev)};
        total_h += h;
    }

    // Handle zz/zt/zb scroll requests
    if (sel >= 0 && sel < static_cast<int>(infos.size())) {
        float sel_top = 0;
        for (int i = 0; i < sel; ++i) sel_top += infos[i].height;
        float sel_h = infos[sel].height;
        float win_h = ImGui::GetWindowHeight();
        float scroll_y = ImGui::GetScrollY();

        switch (app.scroll_pos()) {
        case kopi::App::ScrollPos::Center:
            ImGui::SetScrollY(sel_top - (win_h - sel_h) * 0.5f);
            break;
        case kopi::App::ScrollPos::Top:
            ImGui::SetScrollY(sel_top);
            break;
        case kopi::App::ScrollPos::Bottom:
            ImGui::SetScrollY(sel_top + sel_h - win_h);
            break;
        default:
            // Auto-scroll to keep selected item visible
            if (sel_top < scroll_y)
                ImGui::SetScrollY(sel_top);
            else if (sel_top + sel_h > scroll_y + win_h)
                ImGui::SetScrollY(sel_top + sel_h - win_h);
            break;
        }
        app.set_scroll_pos(kopi::App::ScrollPos::None);
    }

    // Manual virtual list with variable heights
    float scroll_y = ImGui::GetScrollY();
    float win_h = ImGui::GetWindowHeight();
    float cursor_y = 0;

    // We don't use clipper.Step() — handle visibility manually

    for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
        float item_h = infos[i].height;

        // Only render if visible
        if (cursor_y + item_h > scroll_y && cursor_y < scroll_y + win_h) {
            size_t real_idx = filtered[static_cast<size_t>(i)];
            const auto& e = entries[real_idx];

            ImGui::SetCursorPosY(cursor_y);

            bool is_current = (i == sel);
            bool is_picked = app.is_selected(i);

            if (is_current) {
                ImGui::PushStyleColor(ImGuiCol_Header,
                                      ImVec4(0.40f, 0.60f, 0.90f, 0.50f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(0.45f, 0.65f, 0.95f, 0.60f));
            } else if (is_picked) {
                ImGui::PushStyleColor(ImGuiCol_Header,
                                      ImVec4(0.30f, 0.70f, 0.40f, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(0.35f, 0.75f, 0.45f, 0.55f));
            }

            auto tt = std::chrono::system_clock::to_time_t(e.timestamp);
            std::tm tm;
            localtime_r(&tt, &tm);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm);

            // MIME type badges
            std::string badges;
            if (!e.mime_data.empty()) {
                bool has_html = false, has_rtf = false, has_img = false;
                for (auto& m : e.mime_data) {
                    if (m.type.find("html") != std::string::npos) has_html = true;
                    else if (m.type.find("rtf") != std::string::npos) has_rtf = true;
                    else if (m.type.find("image") != std::string::npos) has_img = true;
                }
                if (has_html) badges += "[H]";
                if (has_rtf)  badges += "[R]";
                if (has_img)  badges += "[I]";
            }

            char label[1024];
            std::snprintf(label, sizeof(label), "%s %3zu  %s %s\n%s",
                          is_picked ? "[✓]" : "[ ]",
                          real_idx + 1, time_buf, badges.c_str(),
                          infos[i].preview.c_str());

            ImGuiSelectableFlags sflags =
                ImGuiSelectableFlags_SpanAllColumns |
                ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(label, is_current, sflags,
                                  ImVec2(0, item_h))) {
                app.set_selected(i);
            }

            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                app.copy_to_clipboard(real_idx);
            }

            if (is_current || is_picked) {
                ImGui::PopStyleColor(2);
            }
        }

        cursor_y += item_h;
    }

    // Set total scrollable content height
    ImGui::SetCursorPosY(total_h);

    // If list is empty
    if (filtered.empty()) {
        ImGui::TextUnformatted("  (empty — start copying!)");
    }

    ImGui::EndChild();

    // --- Status bar / keybindings hint ---
    {
        // Selection count
        if (app.selection_count() > 0) {
            ImGui::TextDisabled("%zu item(s) selected — Enter to merge & paste",
                                app.selection_count());
        }

        // MIME capture toggle
        bool mime = app.mime_capture();
        if (ImGui::Checkbox("Capture rich content (HTML/images/etc.)", &mime)) {
            app.set_mime_capture(mime);
        }

        // Keybinding hints
        ImGui::TextDisabled(
            "j/k or Ctrl+j/k:nav  /:search  Space:select  Enter/y/l:copy  "
            "dd:del  gg/G:top/bot  Ctrl+D/U:page  Esc:list→hide  Ctrl+Q:quit");


    }

    ImGui::End();
    ImGui::PopStyleVar();
}
