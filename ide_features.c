/**
 * ============================================================================
 * SENKUVIM - ide_features.c
 * Hybrid Neovim + VS Code Ergonomics, Smart Indentation, Auto-Pairs & Undo Stack
 * ============================================================================
 *
 * Feature Highlights:
 * 1. Smart Tab & Soft-Tab Unindent:
 *    - Tab inserts 4 visual spaces aligned to 4-space tab stops.
 *    - Backspace on leading indentation automatically unindents by up to 4 spaces
 *      in a single keypress, matching modern IDE behavior.
 *
 * 2. Auto-Indent with Scope Awareness:
 *    - Pressing Enter replicates the indentation of the current line.
 *    - If the line ends with an opening brace '{', an extra 4-space level
 *      is automatically added.
 *
 * 3. Intelligent Auto-Pairs:
 *    - Typing '(', '{', '[', '"', or '\'' inserts both opening and closing glyphs,
 *      positioning the cursor directly between them.
 *    - Typing a closing bracket when already adjacent to one simply steps over it.
 *    - Backspacing an empty pair deletes both glyphs simultaneously.
 *
 * 4. Transactional Undo/Redo Engine:
 *    - Operates as an immutable delta stack of user mutations.
 *    - Handles insert, delete, split, join, line deletions (dd), and pastes (p).
 *    - Replaying or reverting state guarantees cursor restoration and zero leaks.
 */

#include "senkuvim.h"

/* ============================================================================
 * UNDO / REDO TRANSACTION STACK IMPLEMENTATION
 * ============================================================================ */

/*
 * undoPush() - Allocates an undo record and pushes it onto E.undo_stack.
 * Note: Pushing a new user action purges the redo stack, as history has diverged.
 */
void undoPush(UndoType type, int cx, int cy, const char *data, size_t len) {
    /* Purge redo stack */
    UndoAction *curr = E.redo_stack;
    while (curr) {
        UndoAction *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    E.redo_stack = NULL;

    UndoAction *action = malloc(sizeof(UndoAction));
    if (action == NULL) die("malloc undoPush");

    action->type = type;
    action->cx = cx;
    action->cy = cy;
    action->len = len;

    if (data && len > 0) {
        action->data = malloc(len + 1);
        if (action->data == NULL) die("malloc undoPush data");
        memcpy(action->data, data, len);
        action->data[len] = '\0';
    } else {
        action->data = NULL;
    }

    action->next = E.undo_stack;
    E.undo_stack = action;
}

/*
 * undoFreeAll() - Frees all nodes in both undo and redo stacks.
 */
void undoFreeAll(void) {
    UndoAction *curr = E.undo_stack;
    while (curr) {
        UndoAction *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    E.undo_stack = NULL;

    curr = E.redo_stack;
    while (curr) {
        UndoAction *next = curr->next;
        free(curr->data);
        free(curr);
        curr = next;
    }
    E.redo_stack = NULL;
}

/*
 * editorUndo() - Reverts the most recent user modification.
 */
void editorUndo(void) {
    if (E.undo_stack == NULL) {
        editorSetStatusMessage("Already at oldest change");
        return;
    }

    /* Pop action from undo stack */
    UndoAction *act = E.undo_stack;
    E.undo_stack = act->next;

    /* Revert mutation based on its type */
    switch (act->type) {
        case UNDO_INSERT_CHAR:
            /* A char was inserted at (cx, cy): delete it */
            if (act->cy < E.numrows && E.row != NULL && act->cx < E.row[act->cy].size) {
                editorRowDelChar(&E.row[act->cy], act->cx);
            }
            E.cx = act->cx;
            E.cy = act->cy;
            break;

        case UNDO_DELETE_CHAR:
            /* A char was deleted at (cx, cy): re-insert it */
            if (act->cy < E.numrows && E.row != NULL && act->data != NULL) {
                editorRowInsertChar(&E.row[act->cy], act->cx, act->data[0]);
            }
            E.cx = act->cx;
            E.cy = act->cy;
            break;

        case UNDO_INSERT_STRING:
            /* A string was inserted at (cx, cy): delete `len` chars */
            if (act->cy < E.numrows && E.row != NULL) {
                for (size_t i = 0; i < act->len; i++) {
                    if (act->cx < E.row[act->cy].size) {
                        editorRowDelChar(&E.row[act->cy], act->cx);
                    }
                }
            }
            E.cx = act->cx;
            E.cy = act->cy;
            break;

        case UNDO_DELETE_STRING:
            /* A string was removed: re-insert the characters */
            if (act->cy < E.numrows && E.row != NULL && act->data != NULL) {
                for (size_t i = 0; i < act->len; i++) {
                    editorRowInsertChar(&E.row[act->cy], act->cx + (int)i, act->data[i]);
                }
            }
            E.cx = act->cx + (int)act->len;
            E.cy = act->cy;
            break;

        case UNDO_SPLIT_LINE:
            /* Enter was pressed: join act->cy + 1 back onto act->cy */
            if (act->cy < E.numrows && act->cy + 1 < E.numrows && E.row != NULL) {
                erow *next_row = &E.row[act->cy + 1];
                editorRowAppendString(&E.row[act->cy], next_row->chars, (size_t)next_row->size);
                editorDelRow(act->cy + 1);
            }
            E.cx = act->cx;
            E.cy = act->cy;
            break;

        case UNDO_JOIN_LINE:
            /* Two lines were joined: split act->cy at act->cx */
            if (act->cy < E.numrows && E.row != NULL) {
                erow *curr = &E.row[act->cy];
                int split_cx = (act->cx <= curr->size) ? act->cx : curr->size;
                editorInsertRow(act->cy + 1, &curr->chars[split_cx], (size_t)(curr->size - split_cx));
                curr = &E.row[act->cy];
                curr->size = split_cx;
                curr->chars[curr->size] = '\0';
                editorUpdateRow(curr);
            }
            E.cx = 0;
            E.cy = act->cy + 1;
            break;

        case UNDO_DELETE_LINE:
            /* Entire line was deleted (dd): re-insert row */
            editorInsertRow(act->cy, act->data ? act->data : "", act->len);
            E.cx = act->cx;
            E.cy = act->cy;
            break;

        case UNDO_PASTE_LINE:
            /* Line was pasted at act->cy + 1: remove it */
            if (act->cy + 1 < E.numrows && E.row != NULL) {
                editorDelRow(act->cy + 1);
            }
            E.cx = act->cx;
            E.cy = act->cy;
            break;
    }

    /* Clamp cursor to valid buffer boundaries */
    if (E.numrows > 0 && E.row != NULL) {
        if (E.cy >= E.numrows) E.cy = E.numrows - 1;
        if (E.cy < 0) E.cy = 0;
        if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
        if (E.cx < 0) E.cx = 0;
    } else {
        E.cy = 0;
        E.cx = 0;
    }

    /* Push reverted action onto redo stack */
    act->next = E.redo_stack;
    E.redo_stack = act;

    editorSetStatusMessage("Undo performed");
}

/*
 * editorRedo() - Replays the most recently undone modification.
 */
void editorRedo(void) {
    if (E.redo_stack == NULL) {
        editorSetStatusMessage("Already at newest change");
        return;
    }

    /* Pop action from redo stack */
    UndoAction *act = E.redo_stack;
    E.redo_stack = act->next;

    /* Re-apply modification */
    switch (act->type) {
        case UNDO_INSERT_CHAR:
            if (act->cy < E.numrows && E.row != NULL && act->data != NULL) {
                editorRowInsertChar(&E.row[act->cy], act->cx, act->data[0]);
                E.cx = act->cx + 1;
                E.cy = act->cy;
            }
            break;

        case UNDO_DELETE_CHAR:
            if (act->cy < E.numrows && E.row != NULL && act->cx < E.row[act->cy].size) {
                editorRowDelChar(&E.row[act->cy], act->cx);
                E.cx = act->cx;
                E.cy = act->cy;
            }
            break;

        case UNDO_INSERT_STRING:
            if (act->cy < E.numrows && E.row != NULL && act->data != NULL) {
                for (size_t i = 0; i < act->len; i++) {
                    editorRowInsertChar(&E.row[act->cy], act->cx + (int)i, act->data[i]);
                }
                E.cx = act->cx + (int)act->len;
                E.cy = act->cy;
            }
            break;

        case UNDO_DELETE_STRING:
            if (act->cy < E.numrows && E.row != NULL) {
                for (size_t i = 0; i < act->len; i++) {
                    if (act->cx < E.row[act->cy].size) {
                        editorRowDelChar(&E.row[act->cy], act->cx);
                    }
                }
                E.cx = act->cx;
                E.cy = act->cy;
            }
            break;

        case UNDO_SPLIT_LINE:
            E.cx = act->cx;
            E.cy = act->cy;
            editorInsertNewline();
            break;

        case UNDO_JOIN_LINE:
            if (act->cy + 1 < E.numrows && E.row != NULL) {
                erow *curr = &E.row[act->cy];
                erow *next = &E.row[act->cy + 1];
                editorRowAppendString(curr, next->chars, (size_t)next->size);
                editorDelRow(act->cy + 1);
                E.cx = act->cx;
                E.cy = act->cy;
            }
            break;

        case UNDO_DELETE_LINE:
            if (act->cy < E.numrows && E.row != NULL) {
                editorDelRow(act->cy);
                E.cx = act->cx;
                E.cy = act->cy;
            }
            break;

        case UNDO_PASTE_LINE:
            editorInsertRow(act->cy + 1, act->data ? act->data : "", act->len);
            E.cy = act->cy + 1;
            E.cx = 0;
            break;
    }

    /* Clamp cursor to valid buffer boundaries */
    if (E.numrows > 0 && E.row != NULL) {
        if (E.cy >= E.numrows) E.cy = E.numrows - 1;
        if (E.cy < 0) E.cy = 0;
        if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
        if (E.cx < 0) E.cx = 0;
    } else {
        E.cy = 0;
        E.cx = 0;
    }

    /* Push back onto undo stack */
    act->next = E.undo_stack;
    E.undo_stack = act;

    editorSetStatusMessage("Redo performed");
}

/* ============================================================================
 * SMART TAB & BACKSPACE ERGONOMICS
 * ============================================================================ */

/*
 * editorSmartTab() - Inserts spaces to reach the next 4-space tab stop.
 * Example:
 * If cx is 0 -> inserts 4 spaces
 * If cx is 2 -> inserts 2 spaces
 * If cx is 4 -> inserts 4 spaces
 */
void editorSmartTab(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }

    int spaces = SENKUVIM_TAB_STOP - (E.cx % SENKUVIM_TAB_STOP);
    char tab_spaces[SENKUVIM_TAB_STOP + 1];
    for (int i = 0; i < spaces; i++) {
        tab_spaces[i] = ' ';
    }
    tab_spaces[spaces] = '\0';

    /* Push undo entry for the inserted tab spaces */
    undoPush(UNDO_INSERT_STRING, E.cx, E.cy, tab_spaces, (size_t)spaces);

    for (int i = 0; i < spaces; i++) {
        editorRowInsertChar(&E.row[E.cy], E.cx, ' ');
        E.cx++;
    }
}

/*
 * editorSmartBackspace() - Smart unindent and auto-pair deletion.
 * 1. If between an empty pair (e.g. () or {}), removes both brackets simultaneously.
 * 2. If in leading whitespace, deletes up to 4 spaces at once (soft-tab unindent).
 * 3. Otherwise, performs standard single-character deletion.
 */
void editorSmartBackspace(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];

    /* Case 1: Cursor is between an auto-pair */
    if (E.cx > 0 && E.cx < row->size) {
        char left = row->chars[E.cx - 1];
        char right = row->chars[E.cx];
        if ((left == '(' && right == ')') ||
            (left == '[' && right == ']') ||
            (left == '{' && right == '}') ||
            (left == '"' && right == '"') ||
            (left == '\'' && right == '\'')) {
            /* Record undo for right char, then left char */
            char pair[2] = { left, right };
            undoPush(UNDO_DELETE_STRING, E.cx - 1, E.cy, pair, 2);

            editorRowDelChar(row, E.cx);     /* Delete closing bracket */
            editorRowDelChar(row, E.cx - 1); /* Delete opening bracket */
            E.cx--;
            return;
        }
    }

    /* Case 2: Soft-tab unindent on leading whitespace */
    if (E.cx > 0) {
        bool only_spaces = true;
        for (int i = 0; i < E.cx; i++) {
            if (row->chars[i] != ' ') {
                only_spaces = false;
                break;
            }
        }

        if (only_spaces) {
            int rem = E.cx % SENKUVIM_TAB_STOP;
            int count = (rem == 0) ? SENKUVIM_TAB_STOP : rem;
            if (count > E.cx) count = E.cx;

            char spaces[SENKUVIM_TAB_STOP + 1];
            for (int i = 0; i < count; i++) spaces[i] = ' ';
            spaces[count] = '\0';

            undoPush(UNDO_DELETE_STRING, E.cx - count, E.cy, spaces, (size_t)count);

            for (int i = 0; i < count; i++) {
                editorRowDelChar(row, E.cx - 1);
                E.cx--;
            }
            return;
        }
    }

    /* Case 3: Line join at start of line */
    if (E.cx == 0 && E.cy > 0) {
        int prev_len = E.row[E.cy - 1].size;
        undoPush(UNDO_JOIN_LINE, prev_len, E.cy - 1, NULL, 0);
        editorDelChar();
        return;
    }

    /* Case 4: Standard single character backspace */
    char deleted_char = row->chars[E.cx - 1];
    undoPush(UNDO_DELETE_CHAR, E.cx - 1, E.cy, &deleted_char, 1);
    editorDelChar();
}

/* ============================================================================
 * AUTO-INDENT ON ENTER
 * ============================================================================ */

/*
 * editorAutoIndentNewline() - Handles Enter in Insert mode.
 * Copies leading whitespace from current row, plus +4 spaces if row ends with '{'.
 * If Enter is pressed between '{' and '}', expands into a 3-line indented block
 * with matching closing brace (canonical modern IDE behavior).
 */
void editorAutoIndentNewline(void) {
    if (E.numrows == 0 || E.row == NULL) {
        editorInsertRow(0, "", 0);
        E.cy = 0;
        E.cx = 0;
        return;
    }
    if (E.cy >= E.numrows) {
        editorInsertNewline();
        return;
    }

    erow *row = &E.row[E.cy];

    /* Calculate leading whitespace */
    int indent_len = 0;
    while (indent_len < row->size && (row->chars[indent_len] == ' ' || row->chars[indent_len] == '\t')) {
        indent_len++;
    }

    /* Save leading whitespace prefix into a temporary buffer before any row reallocation */
    char *indent_prefix = malloc((size_t)(indent_len + 1));
    if (indent_prefix == NULL) die("malloc indent_prefix");
    if (indent_len > 0) {
        memcpy(indent_prefix, row->chars, (size_t)indent_len);
    }
    indent_prefix[indent_len] = '\0';

    /* Check if line before cursor ends with '{' or ':' (ignoring whitespace) */
    bool add_extra_indent = false;
    int p = E.cx - 1;
    while (p >= 0 && (row->chars[p] == ' ' || row->chars[p] == '\t')) {
        p--;
    }
    if (p >= 0 && (row->chars[p] == '{' || row->chars[p] == ':')) {
        add_extra_indent = true;
    }

    /* Check if cursor is directly between auto-paired braces: '{' and '}' */
    bool between_braces = (p >= 0 && row->chars[p] == '{' && E.cx < row->size && row->chars[E.cx] == '}');

    if (between_braces) {
        /* Remove the '}' from the current line */
        editorRowDelChar(&E.row[E.cy], E.cx);

        /* Split current line after '{' */
        undoPush(UNDO_SPLIT_LINE, E.cx, E.cy, NULL, 0);
        editorInsertNewline();

        /* Indent the middle active line by +4 spaces */
        int child_indent = indent_len + SENKUVIM_TAB_STOP;
        char *child_buf = malloc((size_t)(child_indent + 1));
        if (child_buf == NULL) die("malloc editorAutoIndentNewline child_buf");

        if (indent_len > 0) {
            memcpy(child_buf, indent_prefix, (size_t)indent_len);
        }
        for (int i = 0; i < SENKUVIM_TAB_STOP; i++) {
            child_buf[indent_len + i] = ' ';
        }
        child_buf[child_indent] = '\0';

        undoPush(UNDO_INSERT_STRING, 0, E.cy, child_buf, (size_t)child_indent);
        for (int i = 0; i < child_indent; i++) {
            editorRowInsertChar(&E.row[E.cy], i, child_buf[i]);
            E.cx++;
        }
        free(child_buf);

        /* Place the matching '}' on a third line aligned with parent indentation */
        char *parent_close_buf = malloc((size_t)(indent_len + 2));
        if (parent_close_buf == NULL) die("malloc editorAutoIndentNewline close_buf");

        if (indent_len > 0) {
            memcpy(parent_close_buf, indent_prefix, (size_t)indent_len);
        }
        parent_close_buf[indent_len] = '}';
        parent_close_buf[indent_len + 1] = '\0';

        editorInsertRow(E.cy + 1, parent_close_buf, (size_t)(indent_len + 1));
        free(parent_close_buf);
        free(indent_prefix);
        return;
    }

    /* Split line at cursor */
    undoPush(UNDO_SPLIT_LINE, E.cx, E.cy, NULL, 0);
    editorInsertNewline();

    /* Build indentation string */
    int total_indent = indent_len + (add_extra_indent ? SENKUVIM_TAB_STOP : 0);
    if (total_indent > 0) {
        char *indent_buf = malloc((size_t)(total_indent + 1));
        if (indent_buf == NULL) die("malloc editorAutoIndentNewline");

        /* Copy parent indentation */
        if (indent_len > 0) {
            memcpy(indent_buf, indent_prefix, (size_t)indent_len);
        }
        /* Append 4 spaces if scope opened */
        if (add_extra_indent) {
            for (int i = 0; i < SENKUVIM_TAB_STOP; i++) {
                indent_buf[indent_len + i] = ' ';
            }
        }
        indent_buf[total_indent] = '\0';

        /* Push undo for indentation string */
        undoPush(UNDO_INSERT_STRING, 0, E.cy, indent_buf, (size_t)total_indent);

        for (int i = 0; i < total_indent; i++) {
            editorRowInsertChar(&E.row[E.cy], i, indent_buf[i]);
            E.cx++;
        }

        free(indent_buf);
    }
    free(indent_prefix);
}

/* ============================================================================
 * AUTO-PAIRS ENGINE
 * ============================================================================ */

/*
 * editorHandleAutoPairs() - Intercepts typed delimiters.
 * Returns true if the key was handled, false otherwise.
 */
bool editorHandleAutoPairs(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }

    erow *row = &E.row[E.cy];

    /* Over-type closing bracket if cursor is already directly before it */
    if (E.cx < row->size) {
        char next_ch = row->chars[E.cx];
        if ((c == ')' && next_ch == ')') ||
            (c == ']' && next_ch == ']') ||
            (c == '}' && next_ch == '}') ||
            (c == '"' && next_ch == '"') ||
            (c == '\'' && next_ch == '\'')) {
            E.cx++;
            return true;
        }
    }

    /* Auto-insert pair */
    char open_ch = 0;
    char close_ch = 0;

    switch (c) {
        case '(': open_ch = '('; close_ch = ')'; break;
        case '[': open_ch = '['; close_ch = ']'; break;
        case '{': open_ch = '{'; close_ch = '}'; break;
        case '"': open_ch = '"'; close_ch = '"'; break;
        case '\'': open_ch = '\''; close_ch = '\''; break;
        default: return false;
    }

    char pair[3] = { open_ch, close_ch, '\0' };
    undoPush(UNDO_INSERT_STRING, E.cx, E.cy, pair, 2);

    editorRowInsertChar(row, E.cx, open_ch);
    editorRowInsertChar(row, E.cx + 1, close_ch);

    /* Place cursor inside the pair */
    E.cx++;
    return true;
}

/* ============================================================================
 * VIM LINE CLIPBOARD ACTIONS (dd / yy / p)
 * ============================================================================ */

/*
 * editorCopyCurrentLine() - Vim 'yy' yank operation.
 */
void editorCopyCurrentLine(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) {
        editorSetStatusMessage("No line to copy");
        return;
    }

    erow *row = &E.row[E.cy];
    free(E.clipboard);
    E.clipboard = malloc((size_t)(row->size + 1));
    if (E.clipboard == NULL) die("malloc editorCopyCurrentLine");

    memcpy(E.clipboard, row->chars, (size_t)row->size);
    E.clipboard[row->size] = '\0';
    E.clipboard_len = (size_t)row->size;

    editorSetStatusMessage("1 line yanked");
}

/*
 * editorDeleteCurrentLine() - Vim 'dd' delete line operation.
 */
void editorDeleteCurrentLine(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) {
        editorSetStatusMessage("No line to delete");
        return;
    }

    /* Copy into clipboard first */
    editorCopyCurrentLine();

    /* Push delete line undo action */
    erow *row = &E.row[E.cy];
    undoPush(UNDO_DELETE_LINE, E.cx, E.cy, row->chars, (size_t)row->size);

    editorDelRow(E.cy);

    if (E.cy >= E.numrows && E.numrows > 0) {
        E.cy = E.numrows - 1;
    }
    if (E.numrows > 0 && E.row != NULL) {
        if (E.cx > E.row[E.cy].size) {
            E.cx = E.row[E.cy].size;
        }
    } else {
        E.cx = 0;
        E.cy = 0;
    }

    editorSetStatusMessage("1 line deleted");
}

/*
 * editorPasteBelow() - Vim 'p' paste line below current row.
 */
void editorPasteBelow(void) {
    if (E.clipboard == NULL) {
        editorSetStatusMessage("Clipboard is empty");
        return;
    }

    int paste_at = (E.numrows == 0 || E.row == NULL) ? 0 : E.cy + 1;
    editorInsertRow(paste_at, E.clipboard, E.clipboard_len);

    undoPush(UNDO_PASTE_LINE, E.cx, E.cy, E.clipboard, E.clipboard_len);

    E.cy = paste_at;
    E.cx = 0;

    editorSetStatusMessage("1 line pasted");
}
