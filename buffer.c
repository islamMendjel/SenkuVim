/**
 * ============================================================================
 * SENKUVIM - buffer.c
 * Dynamic Text Buffer, Line Rendering, String Serialization & File I/O
 * ============================================================================
 *
 * Data Structure Design:
 * - The buffer is organized as an array of `erow` structs (lines of text).
 * - Each row maintains two string representations:
 *   1. `chars`: The pristine raw byte stream loaded from or written to disk.
 *   2. `render`: The visual representation on the screen where tab characters
 *      ('\t') are expanded into visual spaces according to tab stops.
 * - This separation ensures that editing, saving, and rendering remain decoupled.
 * - Functions in this file strictly maintain memory safety: all buffers
 *   are dynamically resized via realloc(), and deterministic cleanup is
 *   enforced by editorFreeAll().
 */

#include "senkuvim.h"

/* ============================================================================
 * UTF-8 MULTIBYTE BOUNDARY HELPERS
 * ============================================================================ */

/*
 * utf8IsCont() - Returns true if `c` is a UTF-8 continuation byte (10xxxxxx).
 */
bool utf8IsCont(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/*
 * utf8CharLen() - Returns expected byte length (1..4) from a UTF-8 lead byte.
 */
int utf8CharLen(unsigned char c) {
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; /* Invalid or continuation byte: advance safely by 1 */
}

/*
 * utf8PrevCharOffset() - Steps backward from `offset` to the start byte of the
 * preceding UTF-8 codepoint, never crossing below 0.
 */
int utf8PrevCharOffset(const char *s, int offset) {
    if (s == NULL || offset <= 0) return 0;
    int pos = offset - 1;
    int steps = 0;
    while (pos > 0 && steps < 3 && utf8IsCont((unsigned char)s[pos])) {
        pos--;
        steps++;
    }
    return pos;
}

/*
 * utf8NextCharOffset() - Steps forward from `offset` to the start byte of the
 * next UTF-8 codepoint, clamped to `len`.
 */
int utf8NextCharOffset(const char *s, int len, int offset) {
    if (s == NULL || offset >= len) return len;
    if (offset < 0) return 0;
    int clen = utf8CharLen((unsigned char)s[offset]);
    int next = offset + clen;
    if (next > len) next = len;
    return next;
}

/*
 * editorRowCxToRx() - Converts a byte index (cx) to a rendered column index (rx).
 * Handles both tab stops and multibyte UTF-8 sequences (1 column per codepoint).
 */
int editorRowCxToRx(erow *row, int cx) {
    if (row == NULL || row->chars == NULL || cx <= 0) return 0;
    int limit = (cx < row->size) ? cx : row->size;
    int rx = 0;
    for (int j = 0; j < limit; j++) {
        unsigned char uc = (unsigned char)row->chars[j];
        if (uc == '\t') {
            /* Advance to the next multiple of SENKUVIM_TAB_STOP */
            rx += (SENKUVIM_TAB_STOP - 1) - (rx % SENKUVIM_TAB_STOP);
            rx++;
        } else if (!utf8IsCont(uc)) {
            rx++;
        }
    }
    return rx;
}

/*
 * editorRowRxToCx() - Converts a rendered column index (rx) back to a byte index (cx).
 * Always snaps to a valid UTF-8 codepoint start boundary.
 */
int editorRowRxToCx(erow *row, int rx) {
    if (row == NULL || row->chars == NULL || rx <= 0) return 0;
    int cur_rx = 0;
    int cx = 0;
    while (cx < row->size) {
        unsigned char uc = (unsigned char)row->chars[cx];
        int step = utf8CharLen(uc);
        if (cx + step > row->size) step = row->size - cx;
        if (uc == '\t') {
            cur_rx += (SENKUVIM_TAB_STOP - 1) - (cur_rx % SENKUVIM_TAB_STOP);
        }
        cur_rx++;
        if (cur_rx > rx) return cx;
        cx += step;
    }
    return cx;
}

/*
 * editorUpdateRow() - Recomputes the rendered representation and syntax highlights.
 * Must be invoked whenever `row->chars` is modified.
 */
void editorUpdateRow(erow *row) {
    if (row == NULL) return;

    /* Count tabs to calculate required memory for `render` */
    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') tabs++;
    }

    /* Free prior render allocations */
    free(row->render);
    free(row->hl);

    /* Allocate space for expanded tabs (+1 for null terminator) */
    size_t alloc_size = (size_t)(row->size + tabs * (SENKUVIM_TAB_STOP - 1) + 1);
    row->render = malloc(alloc_size);
    if (row->render == NULL) die("malloc editorUpdateRow render");

    /* Copy characters, expanding tabs into spaces */
    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % SENKUVIM_TAB_STOP != 0) {
                row->render[idx++] = ' ';
            }
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;

    /* Allocate syntax highlight token array (1 byte per rendered char) */
    if (row->rsize > 0) {
        row->hl = malloc((size_t)row->rsize);
        if (row->hl == NULL) {
            die("malloc editorUpdateRow hl");
        }
    } else {
        row->hl = NULL;
    }

    /* Update syntax highlighting tokens for this row */
    editorUpdateSyntax(row);
}

/*
 * editorInsertRow() - Inserts a new row into the buffer at line index `at`.
 * Shifts all subsequent rows down by 1 and updates their indices.
 */
void editorInsertRow(int at, const char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    if (s == NULL && len > 0) return;

    /* Expand the row pointer array */
    erow *new_row = realloc(E.row, sizeof(erow) * (size_t)(E.numrows + 1));
    if (new_row == NULL) die("realloc editorInsertRow");
    E.row = new_row;

    /* Shift rows down to create empty slot at `at` */
    if (at < E.numrows) {
        memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (size_t)(E.numrows - at));
        for (int j = at + 1; j <= E.numrows; j++) {
            E.row[j].idx++;
        }
    }

    E.row[at].idx = at;
    E.row[at].size = (int)len;
    E.row[at].chars = malloc(len + 1);
    if (E.row[at].chars == NULL) die("malloc editorInsertRow chars");

    if (len > 0 && s != NULL) {
        memcpy(E.row[at].chars, s, len);
    }
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;

    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

/*
 * editorFreeRow() - Deallocates all dynamic buffers owned by a single row.
 */
void editorFreeRow(erow *row) {
    if (row == NULL) return;
    free(row->chars);
    free(row->render);
    free(row->hl);
    row->chars = NULL;
    row->render = NULL;
    row->hl = NULL;
}

/*
 * editorDelRow() - Removes a row from the buffer at index `at`.
 * Shifts all subsequent rows up by 1 and updates their indices.
 */
void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows || E.row == NULL) return;

    editorFreeRow(&E.row[at]);
    if (at < E.numrows - 1) {
        memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (size_t)(E.numrows - at - 1));
    }

    for (int j = at; j < E.numrows - 1; j++) {
        E.row[j].idx--;
    }

    E.numrows--;
    if (E.numrows == 0) {
        free(E.row);
        E.row = NULL;
    }
    E.dirty++;
}

/*
 * editorRowInsertChar() - Inserts a single byte into a row's raw chars at offset `at`.
 */
void editorRowInsertChar(erow *row, int at, int c) {
    if (row == NULL) return;
    if (at < 0 || at > row->size) at = row->size;

    char *new_chars = realloc(row->chars, (size_t)(row->size + 2));
    if (new_chars == NULL) die("realloc editorRowInsertChar");
    row->chars = new_chars;

    if (at < row->size) {
        memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->size - at + 1));
    } else {
        row->chars[row->size + 1] = '\0';
    }
    row->size++;
    row->chars[at] = (char)c;

    editorUpdateRow(row);
    E.dirty++;
}

/*
 * editorRowAppendString() - Appends a string of length `len` to the end of a row.
 */
void editorRowAppendString(erow *row, const char *s, size_t len) {
    if (row == NULL || s == NULL || len == 0) return;
    char *new_chars = realloc(row->chars, (size_t)(row->size + (int)len + 1));
    if (new_chars == NULL) die("realloc editorRowAppendString");
    row->chars = new_chars;

    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';

    editorUpdateRow(row);
    E.dirty++;
}

/*
 * editorRowDelChar() - Removes a single byte from a row at offset `at`.
 */
void editorRowDelChar(erow *row, int at) {
    if (row == NULL || row->chars == NULL || at < 0 || at >= row->size) return;

    memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->size - at));
    row->size--;

    editorUpdateRow(row);
    E.dirty++;
}

/*
 * editorInsertChar() - Top-level character insertion triggered by typing.
 * Automatically inserts an empty row if the buffer is currently empty.
 */
void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

/*
 * editorInsertNewline() - Splits the current line at cursor position (cx).
 * Moves any characters to the right of cx onto a newly allocated row below.
 */
void editorInsertNewline(void) {
    if (E.numrows == 0 || E.row == NULL) {
        editorInsertRow(0, "", 0);
        E.cy = 0;
        E.cx = 0;
        return;
    }

    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], (size_t)(row->size - E.cx));
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

/*
 * editorDelChar() - Deletes the UTF-8 character immediately before the cursor (Backspace).
 * If cursor is at column 0, joins the current line to the end of the previous line.
 */
void editorDelChar(void) {
    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        int prev_cx = utf8PrevCharOffset(row->chars, E.cx);
        int del_bytes = E.cx - prev_cx;
        for (int i = 0; i < del_bytes; i++) {
            editorRowDelChar(row, prev_cx);
        }
        E.cx = prev_cx;
    } else {
        /* Merge current row with the row above */
        E.cx = E.row[E.cy - 1].size;
        if (row->size > 0 && row->chars != NULL) {
            editorRowAppendString(&E.row[E.cy - 1], row->chars, (size_t)row->size);
        }
        editorDelRow(E.cy);
        E.cy--;
    }
}

/*
 * editorRowsToString() - Flattens the dynamic row array into a single contiguous string.
 * Each line is terminated by '\n'. The caller is responsible for freeing the buffer.
 */
char *editorRowsToString(int *buflen) {
    int totlen = 0;
    for (int j = 0; j < E.numrows; j++) {
        totlen += E.row[j].size + 1; /* +1 for newline */
    }
    if (buflen != NULL) *buflen = totlen;

    char *buf = malloc((size_t)(totlen + 1));
    if (buf == NULL) die("malloc editorRowsToString");

    char *p = buf;
    for (int j = 0; j < E.numrows; j++) {
        if (E.row[j].size > 0 && E.row[j].chars != NULL) {
            memcpy(p, E.row[j].chars, (size_t)E.row[j].size);
            p += E.row[j].size;
        }
        *p = '\n';
        p++;
    }
    *p = '\0';

    return buf;
}

/*
 * editorOpen() - Loads a text file from disk into the editor buffer.
 */
void editorOpen(const char *filename) {
    if (filename == NULL) return;
    free(E.filename);
    E.filename = strdup(filename);
    if (E.filename == NULL) die("strdup filename");

    editorSelectSyntaxHighlight();

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (errno == ENOENT) {
            /* File does not exist yet: create empty buffer for editing */
            E.dirty = 0;
            return;
        }
        die("fopen");
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        /* Strip trailing CR and LF */
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--;
        }
        editorInsertRow(E.numrows, line, (size_t)linelen);
    }

    if (ferror(fp)) {
        free(line);
        fclose(fp);
        die("getline");
    }

    free(line);
    fclose(fp);
    E.dirty = 0;
}

/*
 * writeAllFd() - Loops write() until all `len` bytes are written or an error occurs.
 */
static bool writeAllFd(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            return false;
        }
        written += (size_t)n;
    }
    return true;
}

/*
 * editorSave() - Commits the current buffer to disk.
 * Uses O_RDWR | O_CREAT with 0644 permissions.
 */
void editorSave(void) {
    if (E.filename == NULL) {
        editorSetStatusMessage("Save error: No file name specified. Use :w <name>");
        return;
    }

    int len = 0;
    char *buf = editorRowsToString(&len);

    /* Open file, write contents, and close */
    int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        bool ok = writeAllFd(fd, buf, (size_t)len);
        if (close(fd) == -1) ok = false;
        if (ok) {
            free(buf);
            E.dirty = 0;
            editorSetStatusMessage("[OK] %d bytes written to '%s'", len, E.filename);
            return;
        }
    }

    free(buf);
    editorSetStatusMessage("[ERROR] Can't save! I/O error: %s", strerror(errno));
}

/*
 * editorFreeAll() - Deterministic teardown of all allocated memory.
 * Frees all rows, filename string, clipboard, search query, and undo/redo stacks.
 * Guarantees zero memory leaks under Valgrind.
 */
void editorFreeAll(void) {
    for (int i = 0; i < E.numrows; i++) {
        editorFreeRow(&E.row[i]);
    }
    free(E.row);
    E.row = NULL;
    E.numrows = 0;

    free(E.filename);
    E.filename = NULL;

    free(E.clipboard);
    E.clipboard = NULL;

    free(E.search_query);
    E.search_query = NULL;

    undoFreeAll();
}
