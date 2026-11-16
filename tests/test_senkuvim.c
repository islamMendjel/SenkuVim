#include "unity.h"
#include "../senkuvim.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Global editor state matching senkuvim.h typedef */
EditorConfig E;

/* Forward declarations of IDE features from ide_features.c */
void editorSmartTab(void);
bool editorHandleAutoPairs(int c);

void setUp(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.mode = MODE_NORMAL;
}

void tearDown(void) {
    editorFreeAll();
}

void test_buffer_insert_row(void) {
    editorInsertRow(0, "int x = 42;", 11);
    TEST_ASSERT_EQUAL_INT(1, E.numrows);
    TEST_ASSERT_EQUAL_STRING("int x = 42;", E.row[0].chars);
    TEST_ASSERT_EQUAL_INT(11, E.row[0].size);
}

void test_buffer_row_del_char(void) {
    editorInsertRow(0, "Wubba", 5);
    editorRowDelChar(&E.row[0], 4);
    TEST_ASSERT_EQUAL_STRING("Wubb", E.row[0].chars);
    TEST_ASSERT_EQUAL_INT(4, E.row[0].size);
}

void test_smart_tab_insertion(void) {
    editorInsertRow(0, "", 0);
    E.cx = 0;
    E.cy = 0;
    E.mode = MODE_INSERT;

    editorSmartTab();

    TEST_ASSERT_EQUAL_INT(4, E.row[0].size);
    TEST_ASSERT_EQUAL_STRING("    ", E.row[0].chars);
    TEST_ASSERT_EQUAL_INT(4, E.cx);
}

void test_auto_pair_parentheses(void) {
    editorInsertRow(0, "", 0);
    E.cx = 0;
    E.cy = 0;
    E.mode = MODE_INSERT;

    bool handled = editorHandleAutoPairs('(');

    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(2, E.row[0].size);
    TEST_ASSERT_EQUAL_STRING("()", E.row[0].chars);
    TEST_ASSERT_EQUAL_INT(1, E.cx);
}

void test_utf8_cursor_and_column_mapping(void) {
    /* "café" -> 'c'(1), 'a'(1), 'f'(1), 'é'(2 bytes: 0xC3 0xA9) => 5 bytes, 4 columns */
    const char *word = "caf\xC3\xA9";
    editorInsertRow(0, word, 5);
    TEST_ASSERT_EQUAL_INT(5, E.row[0].size);
    TEST_ASSERT_EQUAL_INT(3, editorRowCxToRx(&E.row[0], 3));
    TEST_ASSERT_EQUAL_INT(4, editorRowCxToRx(&E.row[0], 5));
    TEST_ASSERT_EQUAL_INT(3, editorRowRxToCx(&E.row[0], 3));
    TEST_ASSERT_EQUAL_INT(5, editorRowRxToCx(&E.row[0], 4));
    TEST_ASSERT_EQUAL_INT(3, utf8PrevCharOffset(E.row[0].chars, 5));
    TEST_ASSERT_EQUAL_INT(5, utf8NextCharOffset(E.row[0].chars, 5, 3));
}

void test_utf8_backspace_deletes_whole_codepoint(void) {
    const char *word = "caf\xC3\xA9";
    editorInsertRow(0, word, 5);
    E.cx = 5;
    E.cy = 0;

    editorDelChar();

    TEST_ASSERT_EQUAL_INT(3, E.row[0].size);
    TEST_ASSERT_EQUAL_STRING("caf", E.row[0].chars);
    TEST_ASSERT_EQUAL_INT(3, E.cx);
}

void test_abuf_geometric_growth(void) {
    abuf ab = ABUF_INIT;
    for (int i = 0; i < 100; i++) {
        abAppend(&ab, "SenkuVim", 8);
    }
    TEST_ASSERT_EQUAL_INT(800, ab.len);
    TEST_ASSERT_TRUE(ab.cap >= 800);
    abFree(&ab);
    TEST_ASSERT_NULL(ab.b);
    TEST_ASSERT_EQUAL_INT(0, ab.len);
    TEST_ASSERT_EQUAL_INT(0, ab.cap);
}

void test_undo_redo_and_line_ops(void) {
    editorInsertRow(0, "Alpha", 5);
    editorInsertRow(1, "Beta", 4);
    E.cy = 0;
    E.cx = 0;

    editorDeleteCurrentLine();
    TEST_ASSERT_EQUAL_INT(1, E.numrows);
    TEST_ASSERT_EQUAL_STRING("Beta", E.row[0].chars);

    editorUndo();
    TEST_ASSERT_EQUAL_INT(2, E.numrows);
    TEST_ASSERT_EQUAL_STRING("Alpha", E.row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Beta", E.row[1].chars);

    editorRedo();
    TEST_ASSERT_EQUAL_INT(1, E.numrows);
    TEST_ASSERT_EQUAL_STRING("Beta", E.row[0].chars);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_insert_row);
    RUN_TEST(test_buffer_row_del_char);
    RUN_TEST(test_smart_tab_insertion);
    RUN_TEST(test_auto_pair_parentheses);
    RUN_TEST(test_utf8_cursor_and_column_mapping);
    RUN_TEST(test_utf8_backspace_deletes_whole_codepoint);
    RUN_TEST(test_abuf_geometric_growth);
    RUN_TEST(test_undo_redo_and_line_ops);
    return UNITY_END();
}
