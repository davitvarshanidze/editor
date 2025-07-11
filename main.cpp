#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

// List of fonts for menu bar
const char* font_list[] = {
    "/Library/Fonts/Arial.ttf",
    "/Library/Fonts/Courier New.ttf",
    "/Library/Fonts/Menlo.ttc"
};
const int font_list_size = sizeof(font_list) / sizeof(font_list[0]);

// Helper for timing
Uint32 get_ticks() {
    return SDL_GetTicks();
}

// Helper to reload font
TTF_Font* reload_font(const char* font_path, int font_size, TTF_Font* old_font) {
    if (old_font) TTF_CloseFont(old_font);
    return TTF_OpenFont(font_path, font_size);
}

struct Cursor {
    int row = 0;
    int col = 0;
    Cursor() = default;
    Cursor(int r, int c) : row(r), col(c) {}
    bool operator==(const Cursor& other) const { return row == other.row && col == other.col; }
    bool operator!=(const Cursor& other) const { return !(*this == other); }
    bool operator<(const Cursor& other) const { return row < other.row || (row == other.row && col < other.col); }
    bool operator>(const Cursor& other) const { return other < *this; }
};

struct EditorState {
    std::vector<std::string> lines;
    Cursor cursor;
    // Selection
    bool selecting = false;
    Cursor sel_anchor; // where selection started
    Cursor sel_active; // where selection ends (current cursor)
    bool has_selection() const { return selecting && sel_anchor != sel_active; }
    EditorState() : lines(1, ""), cursor(0, 0), selecting(false), sel_anchor(0, 0), sel_active(0, 0) {}
};

// Helper to get selection range (start, end)
void get_selection_bounds(const EditorState& ed, Cursor& start, Cursor& end) {
    if (ed.sel_anchor < ed.sel_active) {
        start = ed.sel_anchor;
        end = ed.sel_active;
    } else {
        start = ed.sel_active;
        end = ed.sel_anchor;
    }
}

// Helper to clamp cursor to valid position
void clamp_cursor(EditorState& ed) {
    if (ed.cursor.row < 0) ed.cursor.row = 0;
    if (ed.cursor.row >= (int)ed.lines.size()) ed.cursor.row = ed.lines.size() - 1;
    if (ed.cursor.col < 0) ed.cursor.col = 0;
    if (ed.cursor.col > (int)ed.lines[ed.cursor.row].size()) ed.cursor.col = ed.lines[ed.cursor.row].size();
}

// Helper to get selected text
std::string get_selected_text(const EditorState& ed) {
    if (!ed.has_selection()) return "";
    Cursor start, end;
    get_selection_bounds(ed, start, end);
    if (start.row == end.row) {
        return ed.lines[start.row].substr(start.col, end.col - start.col);
    }
    std::string result = ed.lines[start.row].substr(start.col) + "\n";
    for (int r = start.row + 1; r < end.row; ++r) {
        result += ed.lines[r] + "\n";
    }
    result += ed.lines[end.row].substr(0, end.col);
    return result;
}

// Helper to delete selected text
void delete_selection(EditorState& ed) {
    if (!ed.has_selection()) return;
    Cursor start, end;
    get_selection_bounds(ed, start, end);
    if (start.row == end.row) {
        ed.lines[start.row].erase(start.col, end.col - start.col);
    } else {
        ed.lines[start.row].erase(start.col);
        ed.lines[start.row] += ed.lines[end.row].substr(end.col);
        ed.lines.erase(ed.lines.begin() + start.row + 1, ed.lines.begin() + end.row + 1);
    }
    ed.cursor = start;
    ed.sel_anchor = ed.sel_active = start;
    ed.selecting = false;
}

// Helper to insert text at cursor
void insert_text(EditorState& ed, const std::string& text) {
    size_t pos = 0, next;
    while ((next = text.find('\n', pos)) != std::string::npos) {
        std::string part = text.substr(pos, next - pos);
        ed.lines[ed.cursor.row].insert(ed.cursor.col, part);
        std::string new_line = ed.lines[ed.cursor.row].substr(ed.cursor.col + part.size());
        ed.lines[ed.cursor.row] = ed.lines[ed.cursor.row].substr(0, ed.cursor.col + part.size());
        ed.lines.insert(ed.lines.begin() + ed.cursor.row + 1, new_line);
        ed.cursor.row++;
        ed.cursor.col = 0;
        pos = next + 1;
    }
    ed.lines[ed.cursor.row].insert(ed.cursor.col, text.substr(pos));
    ed.cursor.col += text.size() - pos;
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    if (TTF_Init() != 0) {
        std::cout << "TTF_Init Error: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    int win_w = 800, win_h = 600;
    SDL_Window* window = SDL_CreateWindow("Simple SDL2 Text Editor", 100, 100, win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cout << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cout << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Font settings
    int font_size = 11;
    int default_font_size = 11;
    int font_index = 0;
    const char* font_path = font_list[font_index];
    TTF_Font* font = TTF_OpenFont(font_path, font_size);
    if (!font) {
        std::cout << "TTF_OpenFont Error: " << TTF_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    EditorState editors[2];
    int focused_editor = 0; // 0 = left, 1 = right
    bool quit = false;
    SDL_StartTextInput();

    // Cursor always visible
    bool cursor_visible = true;

    // File open/search state
    bool file_open_mode = false;
    bool ctrl_x_pressed = false;
    std::string file_search_input;
    std::vector<std::string> dir_entries;
    std::string current_dir = ".";
    int dir_selected = 0;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (file_open_mode) {
                // Handle file search input
                if (e.type == SDL_TEXTINPUT) {
                    file_search_input += e.text.text;
                } else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        file_open_mode = false;
                        file_search_input.clear();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (!file_search_input.empty()) file_search_input.pop_back();
                    }
                    // TODO: Add navigation, enter, directory listing, etc.
                }
                continue;
            } else if (e.type == SDL_TEXTINPUT) {
                EditorState& ed = editors[focused_editor];
                if (ed.has_selection()) delete_selection(ed);
                ed.lines[ed.cursor.row].insert(ed.cursor.col, e.text.text);
                ed.cursor.col += strlen(e.text.text);
                ed.sel_anchor = ed.sel_active = ed.cursor;
                ed.selecting = false;
            } else if (e.type == SDL_KEYDOWN) {
                EditorState& ed = editors[focused_editor];
                bool cmd = (e.key.keysym.mod & KMOD_GUI) != 0;
                bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
                bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                if (cmd) {
                    if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_KP_PLUS) { // Cmd+
                        font_size++;
                        font = reload_font(font_path, font_size, font);
                    } else if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS) { // Cmd-
                        if (font_size > 6) {
                            font_size--;
                            font = reload_font(font_path, font_size, font);
                        }
                    } else if (e.key.keysym.sym == SDLK_0) { // Cmd0
                        font_size = default_font_size;
                        font = reload_font(font_path, font_size, font);
                    } else if (e.key.keysym.sym == SDLK_1) { // Cmd1
                        focused_editor = 0;
                    } else if (e.key.keysym.sym == SDLK_2) { // Cmd2
                        focused_editor = 1;
                    } else if (e.key.keysym.sym == SDLK_a) { // Cmd+A (copy all)
                        ed.sel_anchor = Cursor(0, 0);
                        ed.sel_active = Cursor(ed.lines.size() - 1, ed.lines.back().size());
                        ed.cursor = ed.sel_active;
                        ed.selecting = true;
                        std::string sel = get_selected_text(ed);
                        SDL_SetClipboardText(sel.c_str());
                    } else if (e.key.keysym.sym == SDLK_c) { // Cmd+C
                        if (ed.has_selection()) {
                            std::string sel = get_selected_text(ed);
                            SDL_SetClipboardText(sel.c_str());
                        }
                    } else if (e.key.keysym.sym == SDLK_v) { // Cmd+V
                        if (SDL_HasClipboardText()) {
                            if (ed.has_selection()) delete_selection(ed);
                            char* clip = SDL_GetClipboardText();
                            if (clip) {
                                insert_text(ed, clip);
                                SDL_free(clip);
                            }
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                        }
                    } else if (e.key.keysym.sym == SDLK_LEFT) { // Cmd+Left
                        ed.cursor.col = 0;
                        if (shift) {
                            ed.sel_active = ed.cursor;
                            ed.selecting = true;
                        } else {
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                        }
                    } else if (e.key.keysym.sym == SDLK_RIGHT) { // Cmd+Right
                        ed.cursor.col = ed.lines[ed.cursor.row].size();
                        if (shift) {
                            ed.sel_active = ed.cursor;
                            ed.selecting = true;
                        } else {
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                        }
                    }
                } else if (ctrl) {
                    // Ctrl+X, then F to open file search
                    if (e.key.keysym.sym == SDLK_x) {
                        ctrl_x_pressed = true;
                    } else if (ctrl_x_pressed && e.key.keysym.sym == SDLK_f) {
                        file_open_mode = true;
                        ctrl_x_pressed = false;
                        file_search_input.clear();
                        // TODO: Populate dir_entries with current directory
                    } else {
                        ctrl_x_pressed = false;
                    }
                    // Ctrl+A/E/F/B/D navigation and word delete
                    switch (e.key.keysym.sym) {
                        case SDLK_a: // Ctrl+A (start of line)
                            ed.cursor.col = 0;
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        case SDLK_e: // Ctrl+E (end of line)
                            ed.cursor.col = ed.lines[ed.cursor.row].size();
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        case SDLK_f: { // Ctrl+F (forward word)
                            int len = ed.lines[ed.cursor.row].size();
                            int i = ed.cursor.col;
                            while (i < len && isalnum(ed.lines[ed.cursor.row][i])) ++i;
                            while (i < len && !isalnum(ed.lines[ed.cursor.row][i])) ++i;
                            ed.cursor.col = i;
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        }
                        case SDLK_b: { // Ctrl+B (backward word)
                            int i = ed.cursor.col;
                            while (i > 0 && !isalnum(ed.lines[ed.cursor.row][i-1])) --i;
                            while (i > 0 && isalnum(ed.lines[ed.cursor.row][i-1])) --i;
                            ed.cursor.col = i;
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        }
                        case SDLK_d: { // Ctrl+D (delete char in front)
                            int len = ed.lines[ed.cursor.row].size();
                            if (ed.cursor.col < len) {
                                ed.lines[ed.cursor.row].erase(ed.cursor.col, 1);
                            } else if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                ed.lines[ed.cursor.row] += ed.lines[ed.cursor.row + 1];
                                ed.lines.erase(ed.lines.begin() + ed.cursor.row + 1);
                            }
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        }
                    }
                } else {
                    switch (e.key.keysym.sym) {
                        case SDLK_BACKSPACE:
                            if (ed.has_selection()) {
                                delete_selection(ed);
                            } else if (ed.cursor.col > 0) {
                                ed.lines[ed.cursor.row].erase(ed.cursor.col - 1, 1);
                                ed.cursor.col--;
                            } else if (ed.cursor.row > 0) {
                                ed.cursor.col = ed.lines[ed.cursor.row - 1].size();
                                ed.lines[ed.cursor.row - 1] += ed.lines[ed.cursor.row];
                                ed.lines.erase(ed.lines.begin() + ed.cursor.row);
                                ed.cursor.row--;
                            }
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        case SDLK_DELETE:
                            if (ed.has_selection()) {
                                delete_selection(ed);
                            } else if (ed.cursor.col < (int)ed.lines[ed.cursor.row].size()) {
                                ed.lines[ed.cursor.row].erase(ed.cursor.col, 1);
                            } else if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                ed.lines[ed.cursor.row] += ed.lines[ed.cursor.row + 1];
                                ed.lines.erase(ed.lines.begin() + ed.cursor.row + 1);
                            }
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER: {
                            if (ed.has_selection()) delete_selection(ed);
                            std::string new_line = ed.lines[ed.cursor.row].substr(ed.cursor.col);
                            ed.lines[ed.cursor.row] = ed.lines[ed.cursor.row].substr(0, ed.cursor.col);
                            ed.lines.insert(ed.lines.begin() + ed.cursor.row + 1, new_line);
                            ed.cursor.row++;
                            ed.cursor.col = 0;
                            ed.sel_anchor = ed.sel_active = ed.cursor;
                            ed.selecting = false;
                            break;
                        }
                        case SDLK_LEFT:
                            if (shift) {
                                if (ed.cursor.col > 0) {
                                    ed.cursor.col--;
                                } else if (ed.cursor.row > 0) {
                                    ed.cursor.row--;
                                    ed.cursor.col = ed.lines[ed.cursor.row].size();
                                }
                                ed.sel_active = ed.cursor;
                                ed.selecting = true;
                            } else {
                                if (ed.cursor.col > 0) {
                                    ed.cursor.col--;
                                } else if (ed.cursor.row > 0) {
                                    ed.cursor.row--;
                                    ed.cursor.col = ed.lines[ed.cursor.row].size();
                                }
                                ed.sel_anchor = ed.sel_active = ed.cursor;
                                ed.selecting = false;
                            }
                            break;
                        case SDLK_RIGHT:
                            if (shift) {
                                if (ed.cursor.col < (int)ed.lines[ed.cursor.row].size()) {
                                    ed.cursor.col++;
                                } else if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                    ed.cursor.row++;
                                    ed.cursor.col = 0;
                                }
                                ed.sel_active = ed.cursor;
                                ed.selecting = true;
                            } else {
                                if (ed.cursor.col < (int)ed.lines[ed.cursor.row].size()) {
                                    ed.cursor.col++;
                                } else if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                    ed.cursor.row++;
                                    ed.cursor.col = 0;
                                }
                                ed.sel_anchor = ed.sel_active = ed.cursor;
                                ed.selecting = false;
                            }
                            break;
                        case SDLK_UP:
                            if (shift) {
                                if (ed.cursor.row > 0) {
                                    ed.cursor.row--;
                                    ed.cursor.col = std::min(ed.cursor.col, (int)ed.lines[ed.cursor.row].size());
                                }
                                ed.sel_active = ed.cursor;
                                ed.selecting = true;
                            } else {
                                if (ed.cursor.row > 0) {
                                    ed.cursor.row--;
                                    ed.cursor.col = std::min(ed.cursor.col, (int)ed.lines[ed.cursor.row].size());
                                }
                                ed.sel_anchor = ed.sel_active = ed.cursor;
                                ed.selecting = false;
                            }
                            break;
                        case SDLK_DOWN:
                            if (shift) {
                                if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                    ed.cursor.row++;
                                    ed.cursor.col = std::min(ed.cursor.col, (int)ed.lines[ed.cursor.row].size());
                                }
                                ed.sel_active = ed.cursor;
                                ed.selecting = true;
                            } else {
                                if (ed.cursor.row + 1 < (int)ed.lines.size()) {
                                    ed.cursor.row++;
                                    ed.cursor.col = std::min(ed.cursor.col, (int)ed.lines[ed.cursor.row].size());
                                }
                                ed.sel_anchor = ed.sel_active = ed.cursor;
                                ed.selecting = false;
                            }
                            break;
                        case SDLK_F1:
                            // Removed font menu
                            break;
                        case SDLK_TAB:
                            if (shift) {
                                // Shift+Tab (unindent)
                                if (ed.has_selection()) {
                                    Cursor start, end;
                                    get_selection_bounds(ed, start, end);
                                    for (int r = start.row; r <= end.row; ++r) {
                                        if (ed.lines[r].substr(0, 4) == "    ") {
                                            ed.lines[r].erase(0, 4);
                                        }
                                    }
                                } else {
                                    if (ed.lines[ed.cursor.row].substr(0, 4) == "    ") {
                                        ed.lines[ed.cursor.row].erase(0, 4);
                                        if (ed.cursor.col >= 4) ed.cursor.col -= 4;
                                        else ed.cursor.col = 0;
                                    }
                                }
                            } else {
                                // Tab (indent)
                                if (ed.has_selection()) {
                                    Cursor start, end;
                                    get_selection_bounds(ed, start, end);
                                    for (int r = start.row; r <= end.row; ++r) {
                                        ed.lines[r].insert(0, "    ");
                                    }
                                    ed.cursor.col += 4;
                                    ed.sel_anchor.col += 4;
                                    ed.sel_active.col += 4;
                                } else {
                                    ed.lines[ed.cursor.row].insert(ed.cursor.col, "    ");
                                    ed.cursor.col += 4;
                                }
                            }
                            break;
                        default:
                            // No auto-pairing: just insert the character as normal (handled by SDL_TEXTINPUT)
                            break;
                    }
                }
                clamp_cursor(ed);
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                // Menu bar click
                // Removed menu bar click handling
                // Font menu selection
                // Removed font menu selection handling
                // Editor pane click
                int pane = (mx < win_w / 2) ? 0 : 1;
                if (my > 30) {
                    focused_editor = pane;
                    EditorState& ed = editors[pane];
                    int x_offset = (pane == 0) ? 0 : win_w / 2;
                    int y = 40;
                    int line_height = TTF_FontHeight(font);
                    int row = (my - y) / line_height;
                    if (row < 0) row = 0;
                    if (row >= (int)ed.lines.size()) row = ed.lines.size() - 1;
                    int col = 0;
                    int tx = mx - (x_offset + 50);
                    for (size_t i = 0; i <= ed.lines[row].size(); ++i) {
                        int w = 0;
                        std::string sub = ed.lines[row].substr(0, i);
                        TTF_SizeText(font, sub.c_str(), &w, NULL);
                        if (w > tx) break;
                        col = i;
                    }
                    ed.cursor = Cursor(row, col);
                    ed.sel_anchor = ed.sel_active = ed.cursor;
                    ed.selecting = true;
                    // Removed mouse_selecting and mouse_sel_start
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                // Removed mouse_selecting
            } else if (e.type == SDL_MOUSEMOTION && false) { // Removed mouse_selecting
                int mx = e.motion.x, my = e.motion.y;
                int pane = (mx < win_w / 2) ? 0 : 1;
                if (pane != focused_editor) continue;
                EditorState& ed = editors[pane];
                int x_offset = (pane == 0) ? 0 : win_w / 2;
                int y = 40;
                int line_height = TTF_FontHeight(font);
                int row = (my - y) / line_height;
                if (row < 0) row = 0;
                if (row >= (int)ed.lines.size()) row = ed.lines.size() - 1;
                int col = 0;
                int tx = mx - (x_offset + 50);
                for (size_t i = 0; i <= ed.lines[row].size(); ++i) {
                    int w = 0;
                    std::string sub = ed.lines[row].substr(0, i);
                    TTF_SizeText(font, sub.c_str(), &w, NULL);
                    if (w > tx) break;
                    col = i;
                }
                ed.cursor = Cursor(row, col);
                ed.sel_active = ed.cursor;
                ed.selecting = true;
            }
        }

        // Cursor always visible (no blinking)
        cursor_visible = true;

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // Draw file open/search prompt if active
        if (file_open_mode) {
            SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
            SDL_Rect bar = {0, 0, win_w, 30};
            SDL_RenderFillRect(renderer, &bar);
            SDL_Color promptColor = {255, 255, 255, 255};
            std::string prompt = "Open file: " + file_search_input;
            SDL_Surface* promptSurf = TTF_RenderText_Blended(font, prompt.c_str(), promptColor);
            if (promptSurf) {
                SDL_Texture* promptTex = SDL_CreateTextureFromSurface(renderer, promptSurf);
                SDL_Rect promptRect = {10, 5, promptSurf->w, promptSurf->h};
                SDL_RenderCopy(renderer, promptTex, NULL, &promptRect);
                SDL_DestroyTexture(promptTex);
                SDL_FreeSurface(promptSurf);
            }
            // TODO: Render directory entries below prompt
        }

        // Responsive layout: get window size
        SDL_GetWindowSize(window, &win_w, &win_h);

        // Draw vertical divider
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_Rect divider = {win_w / 2 - 1, 30, 2, win_h - 30};
        SDL_RenderFillRect(renderer, &divider);

        // Draw both editor panes
        for (int pane = 0; pane < 2; ++pane) {
            EditorState& ed = editors[pane];
            SDL_Color textColor = {255, 255, 255, 255};
            SDL_Color lineNumColor = {180, 180, 180, 255};
            int x_offset = (pane == 0) ? 0 : win_w / 2;
            int y = 40;
            if (file_open_mode) y += 30; // shift down for file search bar
            int line_height = TTF_FontHeight(font);
            // Draw selection highlight
            if (ed.has_selection()) {
                Cursor sel_start, sel_end;
                get_selection_bounds(ed, sel_start, sel_end);
                for (int r = sel_start.row; r <= sel_end.row; ++r) {
                    int sel_col_start = (r == sel_start.row) ? sel_start.col : 0;
                    int sel_col_end = (r == sel_end.row) ? sel_end.col : ed.lines[r].size();
                    if (sel_col_start == sel_col_end) continue;
                    std::string before = ed.lines[r].substr(0, sel_col_start);
                    std::string selected = ed.lines[r].substr(sel_col_start, sel_col_end - sel_col_start);
                    int bx = x_offset + 50;
                    if (!before.empty()) {
                        int w = 0;
                        TTF_SizeText(font, before.c_str(), &w, NULL);
                        bx += w;
                    }
                    int sel_w = 0;
                    if (!selected.empty()) {
                        TTF_SizeText(font, selected.c_str(), &sel_w, NULL);
                    }
                    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
                    SDL_Rect sel_rect = {bx, y + (r - sel_start.row) * line_height, sel_w, line_height};
                    SDL_RenderFillRect(renderer, &sel_rect);
                }
            }
            y = 40;
            for (size_t i = 0; i < ed.lines.size(); ++i) {
                // Line number
                std::string num = std::to_string(i + 1);
                SDL_Surface* numSurf = TTF_RenderText_Blended(font, num.c_str(), lineNumColor);
                if (numSurf) {
                    SDL_Texture* numTex = SDL_CreateTextureFromSurface(renderer, numSurf);
                    SDL_Rect numRect = {x_offset + 10, y, numSurf->w, numSurf->h};
                    SDL_RenderCopy(renderer, numTex, NULL, &numRect);
                    SDL_DestroyTexture(numTex);
                    SDL_FreeSurface(numSurf);
                }
                // Text
                SDL_Surface* textSurface = TTF_RenderText_Blended(font, ed.lines[i].c_str(), textColor);
                if (textSurface) {
                    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    SDL_Rect dstRect = {x_offset + 50, y, textSurface->w, textSurface->h};
                    SDL_RenderCopy(renderer, textTexture, NULL, &dstRect);
                    SDL_DestroyTexture(textTexture);
                    SDL_FreeSurface(textSurface);
                }
                y += line_height;
            }
            // Draw blinking cursor for focused editor
            if (pane == focused_editor && cursor_visible) {
                int cursor_x = x_offset + 50;
                int cursor_y = 40 + ed.cursor.row * line_height;
                std::string before_cursor = ed.lines[ed.cursor.row].substr(0, ed.cursor.col);
                if (!before_cursor.empty()) {
                    int w = 0;
                    TTF_SizeText(font, before_cursor.c_str(), &w, NULL);
                    cursor_x += w;
                }
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_Rect cursor_rect = {cursor_x, cursor_y, 2, line_height};
                SDL_RenderFillRect(renderer, &cursor_rect);
            }
        }

        SDL_RenderPresent(renderer);
    }
    SDL_StopTextInput();
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}