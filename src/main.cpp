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
static void render_ui(kopi::App& app, SDL_Window* window, bool& done);
static bool handle_vim_keys(kopi::App& app, SDL_Window* window);
static void draw_frame(SDL_Renderer* renderer);

// ============================================================================
// main
// ============================================================================

int main(int, char**) {
    // --- Init SDL3 + ImGui ---
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!init_sdl(window, renderer)) return 1;

    // --- Register global hotkey (Cmd+8) ---
    Uint32 hotkey_event = kopi_register_hotkey();

    // --- Our app ---
    kopi::App app;

    // Start hidden — user summons with Cmd+8
    bool window_visible = false;
    kopi_hide_window(window);

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

            render_ui(app, window, done);

            draw_frame(renderer);
        }
    }

    // --- Shutdown ---
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
    SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
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
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    ImGui::StyleColorsDark();
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

static bool handle_vim_keys(kopi::App& app, SDL_Window* window) {
    ImGuiIO& io = ImGui::GetIO();

    // ------------------------------------------------------------------
    // Keys that ALWAYS work, even in search mode
    // ------------------------------------------------------------------

    // Escape / Ctrl+[ : exit search mode, or hide window (normal mode)
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) &&
         ImGui::IsKeyDown(ImGuiKey_LeftCtrl))) {
        if (app.search_mode()) {
            app.set_search_mode(false);
            app.set_search_filter("");
            return true;
        }
        // Normal mode: Escape hides the window (like Cmd+W / vim's :q)
        kopi_hide_window(window);
        return true;
    }

    // ------------------------------------------------------------------
    // Keys that only work in normal mode (search inactive)
    // ------------------------------------------------------------------
    if (app.search_mode())
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

    // --- Enter / y / l: copy selected, hide, and paste into previous app ---
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) ||
        pressed(ImGuiKey_Y) || pressed(ImGuiKey_L)) {
        if (auto ri = app.real_index(app.selected()); ri != SIZE_MAX) {
            kopi_hide_window(window);
            kopi_copy_and_paste(app.entries()[ri].text.c_str());
        }
        return true;
    }

    return false;
}

// ============================================================================
// ImGui UI
// ============================================================================

static void render_ui(kopi::App& app, SDL_Window* window, bool& done) {
    handle_vim_keys(app, window);

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

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered.size()),
                  ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            size_t real_idx = filtered[static_cast<size_t>(i)];
            if (real_idx >= entries.size()) continue;
            const auto& e = entries[real_idx];

            // Selection highlight
            bool is_selected = (i == sel);
            if (is_selected) {
                ImGui::PushStyleColor(ImGuiCol_Header,
                                      ImVec4(0.25f, 0.45f, 0.75f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(0.30f, 0.50f, 0.80f, 0.65f));
            }

            // Format: [N] HH:MM:SS | first line of content (trimmed)
            auto tt = std::chrono::system_clock::to_time_t(e.timestamp);
            std::tm tm;
            localtime_r(&tt, &tm);
            char time_buf[32];
            std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm);

            std::string line = e.text;
            auto nl = line.find('\n');
            if (nl != std::string::npos)
                line = line.substr(0, nl) + " …";
            if (line.size() > 120)
                line = line.substr(0, 117) + "…";

            char label[512];
            std::snprintf(label, sizeof(label), "%3zu  %s  %s",
                          real_idx + 1, time_buf, line.c_str());

            ImGuiSelectableFlags sflags =
                ImGuiSelectableFlags_SpanAllColumns |
                ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(label, is_selected, sflags)) {
                app.set_selected(i);
            }

            // Double-click copies
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                app.copy_to_clipboard(real_idx);
            }

            if (is_selected) {
                ImGui::PopStyleColor(2);
                // Scroll to keep visible
                if (ImGui::IsKeyPressed(ImGuiKey_J) ||
                    ImGui::IsKeyPressed(ImGuiKey_K) ||
                    ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                    ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    // ImGui auto-scrolls for Selectable when keyboard-navigated
                }
            }
        }
    }
    clipper.End();

    // If list is empty
    if (filtered.empty()) {
        ImGui::TextUnformatted("  (empty — start copying!)");
    }

    ImGui::EndChild();

    // --- Status bar / keybindings hint ---
    {
        // Show preview of selected entry
        if (sel >= 0 && static_cast<size_t>(sel) < filtered.size()) {
            size_t real_idx = filtered[static_cast<size_t>(sel)];
            if (real_idx < entries.size()) {
                const auto& e = entries[real_idx];
                std::string preview = e.text;
                auto nl = preview.find('\n');
                if (nl != std::string::npos)
                    preview = preview.substr(0, nl) + " …";
                if (preview.size() > 150)
                    preview = preview.substr(0, 147) + "…";

                ImGui::TextDisabled("preview: %s", preview.c_str());
            }
        }

        // Keybinding hints
        ImGui::TextDisabled(
            "j/k:nav  /:search  Enter/y/l:copy  dd:del  gg/G:top/bot  "
            "Ctrl+D/U:page  Esc:hide  Ctrl+Q:quit");


    }

    ImGui::End();
    ImGui::PopStyleVar();
}
