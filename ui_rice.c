/**
 * ============================================================================
 * SENKUVIM - ui_rice.c
 * Rick Sanchez Arch Linux Rice Aesthetic UI Renderer (Transparency Native)
 * ============================================================================
 *
 * Visual Components:
 * 1. Native Terminal Transparency:
 *    To seamlessly integrate with Arch Linux / Hyprland / Kitty wallpaper setups,
 *    this renderer emits ZERO canvas background color codes. All empty spaces,
 *    gutters, padding, and status bar gaps use standard terminal erase-to-EOL
 *    ('\x1b[0m\x1b[K'), allowing underlying desktop wallpaper to show through.
 *    Background color fills are restricted solely to the interiors of Waybar pills
 *    and active search/bracket highlights.
 *
 * 2. Dynamic Gutter with Relative Line Numbers:
 *    - In Normal mode: relative line numbers in Muted Slate Grey (#4A5568).
 *    - Active line is highlighted in bold Ice Blue (#A2D2FF) with its absolute line number.
 *    - On an empty buffer (with filename), line '1' is highlighted in Ice Blue at row 0.
 *    - Non-existent rows display '~' in Muted Slate Grey.
 *    - Unicode column partition: '│'.
 *
 * 3. Waybar-Inspired Rounded Pill Status Bar:
 *    - Floating pills with Nerd Font rounded caps: U+E0B6 and U+E0B4.
 *    - Left Pill (Mode): Dynamic Ice Blue (Normal), Portal Green (Insert), Amber (Command)
 *      with dark bold text (#090D16).
 *    - Middle Pill (File): Slate (#2A3441) with white text (#F4F6F8) and neon red dirty indicator (#EF476F).
 *    - Spacing Gap: Pure transparent spaces ('\x1b[0m ').
 *    - Right Pill: Aqua Teal (#7AE5C6) with dark bold text (#090D16).
 *
 * 4. Cyberpunk Startup Dashboard (RICK_BANNER):
 *    Centered row-by-row ASCII banner in Ice Blue and Aqua Teal with quick-start keys.
 */

#include "senkuvim.h"

/*
 * ============================================================================
 * RICK SANCHEZ / SENKUVIM PURE ASCII WELCOME BANNER
 * ============================================================================
 */
/* RICK_BANNER */
static const char *RICK_BANNER[] = {
    "  ____             _          __     ___             ",
    " / ___|  ___ _ __ | | ___   _ \\ \\   / (_)_ __ ___    ",
    " \\___ \\ / _ \\ '_ \\| |/ / | | | \\ \\ / /| | '_ ` _ \\   ",
    "  ___) |  __/ | | |   <| |_| |  \\ V / | | | | | | |  ",
    " |____/ \\___|_| |_|_|\\_\\__,_|    \\_/  |_|_| |_| |_|   ",
    "",
    "   * Rick Sanchez Arch Linux Rice Edition *    ",
    "         Modal & IDE Terminal Editor           ",
    "",
    "  [ i ]  Insert Mode     [ :w ]  Save File     ",
    "  [ u ]  Undo Action     [ :q ]  Quit          ",
    "  [ / ]  Live Search     [ dd ]  Delete Line   ",
    "  [ y ]  Yank Line       [ p  ]  Paste Below   "
};

#define RICK_BANNER_LINES (int)(sizeof(RICK_BANNER) / sizeof(RICK_BANNER[0]))

/* Gutter width helper: calculates digits needed for line numbers */
static int getGutterDigits(void) {
    if (E.numrows == 0) return 3;
    int digits = 0;
    int temp = E.numrows;
    while (temp > 0) {
        digits++;
        temp /= 10;
    }
    if (digits < 3) digits = 3;
    if (digits > 10) digits = 10;
    return digits;
}

/*
 * editorScroll() - Adjusts rowoff and coloff so cursor is visible on screen.
 * Strictly clamps coordinates to prevent buffer overruns or invalid states.
 */
static void editorScroll(void) {
    E.rx = 0;
    if (E.numrows == 0 || E.row == NULL) {
        E.cx = 0;
        E.cy = 0;
        E.rx = 0;
        E.rowoff = 0;
        E.coloff = 0;
        return;
    }

    if (E.cy >= E.numrows) E.cy = E.numrows - 1;
    if (E.cy < 0) E.cy = 0;

    if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
    if (E.cx < 0) E.cx = 0;

    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

    /* Vertical scrolling */
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }

    /* Horizontal scrolling (takes gutter width into account) */
    int gutter_total = getGutterDigits() + 3; /* digits + spaces + separator */
    int avail_cols = E.screencols - gutter_total;
    if (avail_cols < 10) avail_cols = 10;

    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + avail_cols) {
        E.coloff = E.rx - avail_cols + 1;
    }
}

/*
 * editorDrawWelcomeBanner() - Renders clean, pure ASCII startup dashboard.
 * Centers the 13-line SenkuVim title, subtitles, and keybindings horizontally
 * and vertically with transparent background, and renders standard muted slate
 * tildes (~) on rows outside the banner.
 */
static void editorDrawWelcomeBanner(abuf *ab) {
    int start_y = (E.screenrows > RICK_BANNER_LINES) ? (E.screenrows - RICK_BANNER_LINES) / 2 : 0;
    if (start_y < 0) start_y = 0;

    for (int y = 0; y < E.screenrows; y++) {
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));

        /* Banner rows: render centered ASCII text */
        if (y >= start_y && y < start_y + RICK_BANNER_LINES) {
            int line_idx = y - start_y;
            const char *line = RICK_BANNER[line_idx];
            int len = (int)strlen(line);

            int padding = (E.screencols > len) ? (E.screencols - len) / 2 : 0;
            for (int p = 0; p < padding; p++) {
                abAppend(ab, " ", 1);
            }

            /* Set color scheme per banner section */
            if (line_idx < 5) {
                /* Spiky Hair Ice Blue */
                abAppend(ab, COLOR_FG_ICE_BLUE COLOR_BOLD, (int)strlen(COLOR_FG_ICE_BLUE COLOR_BOLD));
            } else if (line_idx < 8) {
                /* Rick Shirt Aqua Teal */
                abAppend(ab, COLOR_FG_AQUA_TEAL COLOR_BOLD, (int)strlen(COLOR_FG_AQUA_TEAL COLOR_BOLD));
            } else {
                /* Keybindings in Crisp White */
                abAppend(ab, COLOR_FG_WHITE, (int)strlen(COLOR_FG_WHITE));
            }

            int draw_len = (padding + len > E.screencols) ? (E.screencols - padding) : len;
            if (draw_len > 0) {
                abAppend(ab, line, draw_len);
            }
            abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
            abAppend(ab, "\x1b[K\r\n", 5);
        } else {
            /* Standard muted slate tilde (~) and clear line */
            abAppend(ab, COLOR_FG_MUTED "~" COLOR_RESET, (int)strlen(COLOR_FG_MUTED "~" COLOR_RESET));
            abAppend(ab, "\x1b[K\r\n", 5);
        }
    }
}

/*
 * editorDrawRows() - Renders editor text rows, gutter, and syntax styling.
 * Guaranteed 100% transparent on background: only text foreground is colored.
 */
void editorDrawRows(abuf *ab) {
    /* If buffer is empty and no file name given, show startup welcome banner */
    if (E.numrows == 0 && E.filename == NULL) {
        editorDrawWelcomeBanner(ab);
        return;
    }

    int digits = getGutterDigits();
    if (digits > 10) digits = 10;

    /* Find matching bracket to highlight */
    int match_row = -1, match_rx = -1;
    editorFindMatchingBracket(&match_row, &match_rx);

    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;

        /* Reset all background attributes for terminal transparency */
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));

        /* ====================================================================
         * 1. EMPTY BUFFER OR ROWS BEYOND BUFFER
         * ==================================================================== */
        if (filerow >= E.numrows) {
            char gutter[64];
            if (E.numrows == 0 && filerow == 0) {
                /* Empty buffer with filename: line 1 highlighted in Ice Blue */
                snprintf(gutter, sizeof(gutter), "%*d", digits, 1);
                abAppend(ab, COLOR_FG_ICE_BLUE COLOR_BOLD, (int)strlen(COLOR_FG_ICE_BLUE COLOR_BOLD));
                abAppend(ab, gutter, (int)strlen(gutter));
                abAppend(ab, COLOR_RESET " " COLOR_FG_MUTED "│" COLOR_RESET " ", (int)strlen(COLOR_RESET " " COLOR_FG_MUTED "│" COLOR_RESET " "));
            } else {
                /* Non-existent rows: muted tilde '~' */
                snprintf(gutter, sizeof(gutter), "%*s", digits, "~");
                abAppend(ab, COLOR_FG_MUTED, (int)strlen(COLOR_FG_MUTED));
                abAppend(ab, gutter, (int)strlen(gutter));
                abAppend(ab, COLOR_RESET " " COLOR_FG_MUTED "│" COLOR_RESET " ", (int)strlen(COLOR_RESET " " COLOR_FG_MUTED "│" COLOR_RESET " "));
            }
            abAppend(ab, "\x1b[K\r\n", 5);
            continue;
        }

        /* Safe dereference */
        if (E.row == NULL) {
            abAppend(ab, "\x1b[K\r\n", 5);
            continue;
        }

        erow *row = &E.row[filerow];

        /* ====================================================================
         * 2. DRAW GUTTER & LINE NUMBERS
         * ==================================================================== */
        char gutter[64];
        if (filerow == E.cy) {
            /* Active line: Bold Ice Blue with absolute line number */
            snprintf(gutter, sizeof(gutter), "%*d", digits, filerow + 1);
            abAppend(ab, COLOR_FG_ICE_BLUE COLOR_BOLD, (int)strlen(COLOR_FG_ICE_BLUE COLOR_BOLD));
            abAppend(ab, gutter, (int)strlen(gutter));
            abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
        } else {
            /* Inactive line: Muted slate grey */
            int line_num;
            if (E.mode == MODE_NORMAL) {
                /* Relative line numbers in Normal Mode */
                line_num = abs(filerow - E.cy);
            } else {
                /* Absolute line numbers in Insert/Command Mode */
                line_num = filerow + 1;
            }
            snprintf(gutter, sizeof(gutter), "%*d", digits, line_num);
            abAppend(ab, COLOR_FG_MUTED, (int)strlen(COLOR_FG_MUTED));
            abAppend(ab, gutter, (int)strlen(gutter));
            abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
        }

        /* Gutter separator column */
        abAppend(ab, " " COLOR_FG_MUTED "│" COLOR_RESET " ", (int)strlen(" " COLOR_FG_MUTED "│" COLOR_RESET " "));

        /* ====================================================================
         * 3. DRAW TEXT CHARACTERS & SYNTAX HIGHLIGHTS
         * ==================================================================== */
        int gutter_total = digits + 3;
        int avail_cols = E.screencols - gutter_total;
        if (avail_cols < 0) avail_cols = 0;

        int len = row->rsize - E.coloff;
        if (len < 0) len = 0;
        if (len > avail_cols) len = avail_cols;

        char *c = (row->render != NULL) ? &row->render[E.coloff] : NULL;
        unsigned char *hl = (row->hl != NULL) ? &row->hl[E.coloff] : NULL;
        const char *current_color = NULL;

        if (c != NULL && hl != NULL) {
            for (int j = 0; j < len; j++) {
                int current_rx = E.coloff + j;

                /* Check live search match */
                bool is_search_match = false;
                if (E.search_query != NULL && strlen(E.search_query) > 0) {
                    int qlen = (int)strlen(E.search_query);
                    if (current_rx >= 0 && current_rx + qlen <= row->rsize) {
                        if (strncasecmp(&row->render[current_rx], E.search_query, (size_t)qlen) == 0) {
                            is_search_match = true;
                        }
                    }
                    /* Check if inside match from earlier position */
                    for (int back = 1; back < qlen && current_rx - back >= 0; back++) {
                        if (strncasecmp(&row->render[current_rx - back], E.search_query, (size_t)qlen) == 0) {
                            is_search_match = true;
                            break;
                        }
                    }
                }

                /* Check bracket match */
                bool is_bracket_match = false;
                if ((filerow == E.cy && current_rx == E.rx &&
                     (c[j] == '(' || c[j] == ')' || c[j] == '[' || c[j] == ']' || c[j] == '{' || c[j] == '}')) ||
                    (filerow == match_row && current_rx == match_rx)) {
                    is_bracket_match = true;
                }

                if (is_search_match) {
                    abAppend(ab, COLOR_BG_WARNING COLOR_FG_DARK_TEXT COLOR_BOLD, (int)strlen(COLOR_BG_WARNING COLOR_FG_DARK_TEXT COLOR_BOLD));
                    abAppend(ab, &c[j], 1);
                    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
                    current_color = NULL;
                } else if (is_bracket_match) {
                    abAppend(ab, COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT COLOR_BOLD, (int)strlen(COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT COLOR_BOLD));
                    abAppend(ab, &c[j], 1);
                    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
                    current_color = NULL;
                } else if (iscntrl((unsigned char)c[j])) {
                    /* Display unprintable control characters as inverted symbol */
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    current_color = NULL;
                } else {
                    const char *color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        abAppend(ab, color, (int)strlen(color));
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
        }
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));

        /* Erase to end of line maintaining terminal transparency */
        abAppend(ab, "\x1b[K\r\n", 5);
    }
}

/*
 * editorDrawStatusBar() - Renders Waybar-inspired rounded pill status bar.
 * Restricts background color fills strictly to the pills themselves.
 * The gap between pills is 100% transparent.
 */
void editorDrawStatusBar(abuf *ab) {
    /* Clear styling for transparent bar background */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));

    /* --- Left Pill: Mode Badge --- */
    const char *mode_str = " NORMAL ";
    const char *mode_fg = COLOR_FG_ICE_BLUE;
    const char *mode_bg = COLOR_BG_ICE_BLUE;

    if (E.mode == MODE_INSERT) {
        mode_str = " INSERT ";
        mode_fg = COLOR_FG_PORTAL_GREEN;
        mode_bg = COLOR_BG_PORTAL_GREEN;
    } else if (E.mode == MODE_COMMAND) {
        mode_str = " COMMAND ";
        mode_fg = COLOR_FG_WARNING;
        mode_bg = COLOR_BG_WARNING;
    }

#if USE_NERD_FONT
    /* Left rounded cap on transparent canvas */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    abAppend(ab, mode_fg, (int)strlen(mode_fg));
    abAppend(ab, NERD_PILL_LEFT, (int)strlen(NERD_PILL_LEFT));

    /* Pill body */
    abAppend(ab, mode_bg, (int)strlen(mode_bg));
    abAppend(ab, COLOR_FG_DARK_TEXT COLOR_BOLD, (int)strlen(COLOR_FG_DARK_TEXT COLOR_BOLD));
    abAppend(ab, mode_str, (int)strlen(mode_str));

    /* Right rounded cap */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    abAppend(ab, mode_fg, (int)strlen(mode_fg));
    abAppend(ab, NERD_PILL_RIGHT, (int)strlen(NERD_PILL_RIGHT));
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#else
    abAppend(ab, mode_bg COLOR_FG_DARK_TEXT COLOR_BOLD, (int)strlen(mode_bg COLOR_FG_DARK_TEXT COLOR_BOLD));
    abAppend(ab, mode_str, (int)strlen(mode_str));
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#endif

    abAppend(ab, " ", 1);

    /* --- Middle Pill: Filename & Dirty Indicator (Slate #2A3441) --- */
    char file_pill[128];
    const char *fname = E.filename ? E.filename : "[No Name]";
    int flen = snprintf(file_pill, sizeof(file_pill), " %s%s ", fname, E.dirty ? " [+MODIFIED]" : "");

#if USE_NERD_FONT
    /* Left rounded cap in Slate */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    abAppend(ab, COLOR_FG_SLATE, (int)strlen(COLOR_FG_SLATE));
    abAppend(ab, NERD_PILL_LEFT, (int)strlen(NERD_PILL_LEFT));

    /* Slate pill body */
    abAppend(ab, COLOR_BG_SLATE COLOR_FG_WHITE COLOR_BOLD, (int)strlen(COLOR_BG_SLATE COLOR_FG_WHITE COLOR_BOLD));
    if (E.dirty) {
        char base_pill[96];
        snprintf(base_pill, sizeof(base_pill), " %s ", fname);
        abAppend(ab, base_pill, (int)strlen(base_pill));
        abAppend(ab, COLOR_FG_ERROR COLOR_BOLD, (int)strlen(COLOR_FG_ERROR COLOR_BOLD));
        abAppend(ab, "[+MODIFIED] ", 12);
    } else {
        abAppend(ab, file_pill, flen);
    }

    /* Right rounded cap in Slate */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    abAppend(ab, COLOR_FG_SLATE, (int)strlen(COLOR_FG_SLATE));
    abAppend(ab, NERD_PILL_RIGHT, (int)strlen(NERD_PILL_RIGHT));
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#else
    abAppend(ab, COLOR_BG_SLATE COLOR_FG_WHITE, (int)strlen(COLOR_BG_SLATE COLOR_FG_WHITE));
    abAppend(ab, file_pill, flen);
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#endif

    /* Calculate visible width consumed on the left */
    int left_len = (int)strlen(mode_str) + (USE_NERD_FONT ? 2 : 0) + 1 + flen + (USE_NERD_FONT ? 2 : 0);

    /* --- Right Pill: Filetype, Position, Tab Info (Aqua Teal #7AE5C6) --- */
    char right_pill[128];
    const char *ft = E.syntax ? E.syntax->filetype : "No FT";
    int rlen = snprintf(right_pill, sizeof(right_pill),
                        " %s | C17  LN %d : COL %d  TAB: 4 SP ",
                        ft, E.cy + 1, E.cx + 1);

    int total_right = rlen + (USE_NERD_FONT ? 2 : 0);
    int spacing = E.screencols - left_len - total_right;

    /* Fill gap between pills with transparent spaces */
    abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    for (int s = 0; s < spacing; s++) {
        abAppend(ab, " ", 1);
    }

    /* Render Right Pill */
    if (spacing >= 0 && E.screencols >= left_len + total_right) {
#if USE_NERD_FONT
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
        abAppend(ab, COLOR_FG_AQUA_TEAL, (int)strlen(COLOR_FG_AQUA_TEAL));
        abAppend(ab, NERD_PILL_LEFT, (int)strlen(NERD_PILL_LEFT));

        abAppend(ab, COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT COLOR_BOLD, (int)strlen(COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT COLOR_BOLD));
        abAppend(ab, right_pill, rlen);

        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
        abAppend(ab, COLOR_FG_AQUA_TEAL, (int)strlen(COLOR_FG_AQUA_TEAL));
        abAppend(ab, NERD_PILL_RIGHT, (int)strlen(NERD_PILL_RIGHT));
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#else
        abAppend(ab, COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT, (int)strlen(COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT));
        abAppend(ab, right_pill, rlen);
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
#endif
    }

    abAppend(ab, "\x1b[K\r\n", 5);
}

/*
 * editorDrawMessageBar() - Renders message bar or command prompt on bottom line.
 * Transparent canvas with crisp foreground text.
 */
void editorDrawMessageBar(abuf *ab) {
    abAppend(ab, COLOR_RESET "\x1b[K", (int)strlen(COLOR_RESET "\x1b[K"));

    if (E.mode == MODE_COMMAND) {
        /* Render Command Mode Prompt ':' */
        abAppend(ab, COLOR_FG_WARNING COLOR_BOLD ":" COLOR_RESET COLOR_FG_WHITE, (int)strlen(COLOR_FG_WARNING COLOR_BOLD ":" COLOR_RESET COLOR_FG_WHITE));
        abAppend(ab, E.command_buf, E.command_len);
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    } else if (E.search_query != NULL && E.mode == MODE_NORMAL && E.search_last_match != -1) {
        /* Render Search Prompt '/' */
        abAppend(ab, COLOR_FG_AQUA_TEAL COLOR_BOLD "/" COLOR_RESET COLOR_FG_WHITE, (int)strlen(COLOR_FG_AQUA_TEAL COLOR_BOLD "/" COLOR_RESET COLOR_FG_WHITE));
        abAppend(ab, E.search_query, (int)strlen(E.search_query));
        abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
    } else {
        /* Render Temporary Status Notification */
        int msglen = (int)strlen(E.statusmsg);
        if (msglen > E.screencols) msglen = E.screencols;
        if (msglen > 0 && time(NULL) - E.statusmsg_time < 5) {
            abAppend(ab, COLOR_FG_ICE_BLUE, (int)strlen(COLOR_FG_ICE_BLUE));
            abAppend(ab, E.statusmsg, msglen);
            abAppend(ab, COLOR_RESET, (int)strlen(COLOR_RESET));
        }
    }
}

/*
 * editorRefreshScreen() - Full double-buffered frame redraw.
 * Flushes the append buffer to stdout in a single write() call.
 */
void editorRefreshScreen(void) {
    editorScroll();

    abuf ab = ABUF_INIT;

    /* Hide cursor during frame composition */
    abAppend(&ab, "\x1b[?25l", 6);
    /* Place cursor at screen origin (1, 1) */
    abAppend(&ab, "\x1b[H", 3);

    /* Render components */
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    /* Determine cursor placement */
    if (E.mode == MODE_COMMAND) {
        /* Place cursor on command line */
        char cursor_buf[32];
        snprintf(cursor_buf, sizeof(cursor_buf), "\x1b[%d;%dH", E.screenrows + 2, E.command_len + 2);
        abAppend(&ab, cursor_buf, (int)strlen(cursor_buf));
    } else if (E.numrows == 0 && E.filename == NULL) {
        /* In welcome banner mode: place cursor at top-left */
        abAppend(&ab, "\x1b[1;1H", 6);
    } else {
        /* Place cursor in code text area */
        int digits = getGutterDigits();
        int gutter_total = digits + 3;
        int screen_cursor_x = (E.rx - E.coloff) + gutter_total + 1;
        int screen_cursor_y = (E.cy - E.rowoff) + 1;

        if (screen_cursor_x < gutter_total + 1) screen_cursor_x = gutter_total + 1;
        if (screen_cursor_x > E.screencols) screen_cursor_x = E.screencols;
        if (screen_cursor_y < 1) screen_cursor_y = 1;
        if (screen_cursor_y > E.screenrows) screen_cursor_y = E.screenrows;

        char cursor_buf[32];
        snprintf(cursor_buf, sizeof(cursor_buf), "\x1b[%d;%dH", screen_cursor_y, screen_cursor_x);
        abAppend(&ab, cursor_buf, (int)strlen(cursor_buf));
    }

    /* Restore cursor visibility */
    abAppend(&ab, "\x1b[?25h", 6);

    /* Flush frame to terminal using looping writeAll */
    editorWriteAll(ab.b, (size_t)ab.len);
    abFree(&ab);
}

/*
 * editorSetStatusMessage() - Variadic printf-style notification setter.
 */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}
