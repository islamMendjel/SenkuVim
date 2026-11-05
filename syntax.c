/**
 * ============================================================================
 * SENKUVIM - syntax.c
 * Multi-Language Syntax Tokenizer, Parser State Machine & Bracket Matcher
 * ============================================================================
 *
 * Syntax Highlighting Architecture:
 * - Deterministic, finite-state machine (FSM) running line-by-line.
 * - Supports C/C++, Python, and Shell/Bash.
 * - Handles single-line comments, strings with escape sequences, numbers
 *   (decimals, hex 0x, binary 0b, floats), keywords, and built-in types.
 * - Supports multi-line comments across row boundaries: each row records
 *   `hl_open_comment` so that if an open comment spans multiple lines,
 *   subsequent lines inherit the comment state and propagate re-tokenization.
 * - Real-time bracket matching locates matching '(', '[', or '{' delimiters.
 * - Returns 24-bit TrueColor ANSI styling matching the Rick Sanchez Arch Rice.
 */

#include "senkuvim.h"

/* ============================================================================
 * LANGUAGE GRAMMAR DEFINITIONS
 * ============================================================================ */

/* C / C++ Grammar */
static const char *C_Extensions[] = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx", ".ino", NULL
};

static const char *C_Keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",
    "default", "goto", "sizeof", "constexpr", "nullptr", "true", "false",
    "public", "private", "protected", "virtual", "namespace", "using",
    "template", "typename", "new", "delete", "try", "catch", "throw",
    "inline", "extern", "volatile", "register", "alignas", "alignof",
    NULL
};

static const char *C_Types[] = {
    "int", "long", "double", "float", "char", "unsigned", "signed",
    "void", "short", "auto", "const", "bool", "size_t", "ssize_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "uintptr_t", "intptr_t", "ptrdiff_t",
    "FILE", "erow", "EditorConfig", "EditorMode", "SyntaxDefinition",
    "abuf", "UndoAction", "UndoType", "EditorHighlight", NULL
};

/* Python Grammar */
static const char *PY_Extensions[] = {
    ".py", ".pyw", ".pyx", NULL
};

static const char *PY_Keywords[] = {
    "def", "class", "if", "elif", "else", "while", "for", "in", "return",
    "yield", "import", "from", "as", "try", "except", "finally", "raise",
    "with", "pass", "continue", "break", "lambda", "global", "nonlocal",
    "assert", "async", "await", "and", "or", "not", "is", "True", "False",
    "None", NULL
};

static const char *PY_Types[] = {
    "int", "float", "str", "bool", "list", "dict", "set", "tuple",
    "bytes", "bytearray", "object", "type", "self", "cls", NULL
};

/* Shell / Bash Grammar */
static const char *SH_Extensions[] = {
    ".sh", ".bash", ".zsh", ".ksh", ".fish", ".profile", NULL
};

static const char *SH_Keywords[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "select",
    "while", "until", "do", "done", "in", "function", "time", "export",
    "local", "readonly", "return", "exit", "echo", NULL
};

static const char *SH_Types[] = {
    "alias", "source", "shift", "trap", "unset", "set", NULL
};

/* Master Syntax Highlighting Database */
static SyntaxDefinition HLDB[] = {
    {
        .filetype = "C/C++",
        .filematch = C_Extensions,
        .keywords = C_Keywords,
        .types = C_Types,
        .singleline_comment_start = "//",
        .multiline_comment_start = "/*",
        .multiline_comment_end = "*/",
        .flags = HL_FLAG_NUMBERS | HL_FLAG_STRINGS
    },
    {
        .filetype = "Python",
        .filematch = PY_Extensions,
        .keywords = PY_Keywords,
        .types = PY_Types,
        .singleline_comment_start = "#",
        .multiline_comment_start = NULL,
        .multiline_comment_end = NULL,
        .flags = HL_FLAG_NUMBERS | HL_FLAG_STRINGS
    },
    {
        .filetype = "Shell",
        .filematch = SH_Extensions,
        .keywords = SH_Keywords,
        .types = SH_Types,
        .singleline_comment_start = "#",
        .multiline_comment_start = NULL,
        .multiline_comment_end = NULL,
        .flags = HL_FLAG_NUMBERS | HL_FLAG_STRINGS
    }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* ============================================================================
 * HELPER PREDICATES
 * ============================================================================ */

/*
 * isSeparator() - Checks if character delimits word tokens.
 */
bool isSeparator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:!&|^?{}", c) != NULL;
}

/* ============================================================================
 * TOKENIZER STATE MACHINE
 * ============================================================================ */

/*
 * editorUpdateSyntax() - Tokenizes row->render into row->hl highlight tokens.
 * Operates character-by-character using state flags:
 * - in_string: tracks open quote character ('"' or '\'')
 * - in_comment: tracks whether currently within a multi-line comment
 */
void editorUpdateSyntax(erow *row) {
    if (row == NULL || row->hl == NULL || row->rsize == 0) return;

    memset(row->hl, HL_NORMAL, (size_t)row->rsize);

    if (E.syntax == NULL) return;

    const char **keywords = E.syntax->keywords;
    const char **types = E.syntax->types;
    const char *scs = E.syntax->singleline_comment_start;
    const char *mcs = E.syntax->multiline_comment_start;
    const char *mce = E.syntax->multiline_comment_end;

    int scs_len = scs ? (int)strlen(scs) : 0;
    int mcs_len = mcs ? (int)strlen(mcs) : 0;
    int mce_len = mce ? (int)strlen(mce) : 0;

    bool prev_sep = true;
    int in_string = 0;
    bool in_comment = (row->idx > 0 && E.row != NULL && E.row[row->idx - 1].hl_open_comment);

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        /* --- 1. Single-Line Comments --- */
        if (scs_len && !in_string && !in_comment) {
            if (!strncmp(&row->render[i], scs, (size_t)scs_len)) {
                memset(&row->hl[i], HL_COMMENT, (size_t)(row->rsize - i));
                break;
            }
        }

        /* --- 2. Multi-Line Comments --- */
        if (mcs_len && mce_len && !in_string) {
            if (in_comment) {
                row->hl[i] = HL_MLCOMMENT;
                if (!strncmp(&row->render[i], mce, (size_t)mce_len)) {
                    memset(&row->hl[i], HL_MLCOMMENT, (size_t)mce_len);
                    i += mce_len;
                    in_comment = false;
                    prev_sep = true;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, (size_t)mcs_len)) {
                memset(&row->hl[i], HL_MLCOMMENT, (size_t)mcs_len);
                i += mcs_len;
                in_comment = true;
                continue;
            }
        }

        /* --- 3. Strings & Characters --- */
        if (E.syntax->flags & HL_FLAG_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;

                /* Handle backslash escape sequence (e.g. \" or \n) */
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }

                if (c == in_string) {
                    in_string = 0; /* Closing quote reached */
                }
                i++;
                prev_sep = true;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        /* --- 4. Numbers (Decimals, Hex, Binary, Floats) --- */
        if (E.syntax->flags & HL_FLAG_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER) ||
                ((c == 'x' || c == 'X' || c == 'b' || c == 'B') && prev_hl == HL_NUMBER && row->render[i - 1] == '0')) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = false;
                continue;
            }
        }

        /* --- 5. Keywords and Types --- */
        if (prev_sep) {
            int j;
            /* Check Control-Flow Keywords */
            for (j = 0; keywords[j]; j++) {
                int klen = (int)strlen(keywords[j]);
                bool is_kw = (i + klen <= row->rsize) &&
                             !strncmp(&row->render[i], keywords[j], (size_t)klen) &&
                             (i + klen == row->rsize || isSeparator(row->render[i + klen]));
                if (is_kw) {
                    memset(&row->hl[i], HL_KEYWORD, (size_t)klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = false;
                continue;
            }

            /* Check Built-in Types */
            for (j = 0; types[j]; j++) {
                int tlen = (int)strlen(types[j]);
                bool is_type = (i + tlen <= row->rsize) &&
                               !strncmp(&row->render[i], types[j], (size_t)tlen) &&
                               (i + tlen == row->rsize || isSeparator(row->render[i + tlen]));
                if (is_type) {
                    memset(&row->hl[i], HL_TYPE, (size_t)tlen);
                    i += tlen;
                    break;
                }
            }
            if (types[j] != NULL) {
                prev_sep = false;
                continue;
            }
        }

        prev_sep = isSeparator(c);
        i++;
    }

    /* Record open multi-line comment state for next row */
    int changed = (row->hl_open_comment != (in_comment ? 1 : 0));
    row->hl_open_comment = in_comment ? 1 : 0;

    /* Propagate syntax update to subsequent lines if comment state changed */
    if (changed && row->idx + 1 < E.numrows && E.row != NULL) {
        editorUpdateSyntax(&E.row[row->idx + 1]);
    }
}

/*
 * editorSelectSyntaxHighlight() - Detects file language based on file extension.
 */
void editorSelectSyntaxHighlight(void) {
    E.syntax = NULL;
    if (E.filename == NULL) return;

    char *ext = strrchr(E.filename, '.');

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        SyntaxDefinition *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            bool is_ext = (s->filematch[i][0] == '.');
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
                (!is_ext && strstr(E.filename, s->filematch[i]))) {
                E.syntax = s;

                /* Re-highlight all loaded rows */
                for (int filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }
                return;
            }
            i++;
        }
    }
}

/*
 * editorSyntaxToColor() - Maps token enum to 24-bit TrueColor ANSI escape strings.
 * Enforces the Rick Sanchez Arch Rice color palette.
 */
const char *editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_KEYWORD:
            /* Rick Spiky Hair Ice Blue (Bold) */
            return COLOR_FG_ICE_BLUE COLOR_BOLD;
        case HL_TYPE:
            /* Rick Shirt Aqua Teal */
            return COLOR_FG_AQUA_TEAL;
        case HL_STRING:
            /* Portal Neon Green */
            return COLOR_FG_PORTAL_GREEN;
        case HL_NUMBER:
            /* Warm Amber / Warning Yellow */
            return COLOR_FG_WARNING;
        case HL_COMMENT:
        case HL_MLCOMMENT:
            /* Muted Slate Grey Unibrow (Italic) */
            return COLOR_FG_MUTED COLOR_ITALIC;
        case HL_MATCH:
            /* Live search match: Dark text on Amber highlight */
            return COLOR_BG_WARNING COLOR_FG_DARK_TEXT COLOR_BOLD;
        case HL_BRACKET:
            /* Matching bracket: Dark text on Aqua Teal highlight */
            return COLOR_BG_AQUA_TEAL COLOR_FG_DARK_TEXT COLOR_BOLD;
        case HL_NORMAL:
        default:
            /* Lab Coat Crisp White */
            return COLOR_FG_WHITE;
    }
}

/* ============================================================================
 * BRACKET MATCHER ENGINE
 * ============================================================================ */

/*
 * editorFindMatchingBracket() - Finds the matching bracket for delimiter under cursor.
 * If cursor is on '(', '{', or '[', searches forward.
 * If cursor is on ')', '}', or ']', searches backward.
 * Sets *match_row and *match_rx if found, otherwise sets them to -1.
 */
void editorFindMatchingBracket(int *match_row, int *match_rx) {
    *match_row = -1;
    *match_rx = -1;

    if (E.numrows == 0 || E.row == NULL || E.cy >= E.numrows) return;
    erow *row = &E.row[E.cy];
    if (row == NULL || row->chars == NULL || E.cx >= row->size) return;

    char ch = row->chars[E.cx];
    char target = 0;
    int dir = 0;

    switch (ch) {
        case '(': target = ')'; dir = 1; break;
        case '[': target = ']'; dir = 1; break;
        case '{': target = '}'; dir = 1; break;
        case ')': target = '('; dir = -1; break;
        case ']': target = '['; dir = -1; break;
        case '}': target = '{'; dir = -1; break;
        default: return; /* Not on a bracket */
    }

    int depth = 0;
    int cur_y = E.cy;
    int cur_x = E.cx;

    while (cur_y >= 0 && cur_y < E.numrows) {
        erow *r = &E.row[cur_y];
        while (cur_x >= 0 && cur_x < r->size) {
            char c = r->chars[cur_x];
            if (c == ch) {
                depth++;
            } else if (c == target) {
                depth--;
                if (depth == 0) {
                    *match_row = cur_y;
                    *match_rx = editorRowCxToRx(r, cur_x);
                    return;
                }
            }
            cur_x += dir;
        }

        cur_y += dir;
        if (cur_y >= 0 && cur_y < E.numrows) {
            cur_x = (dir == 1) ? 0 : E.row[cur_y].size - 1;
        }
    }
}
