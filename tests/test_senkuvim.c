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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_insert_row);
    RUN_TEST(test_buffer_row_del_char);
    RUN_TEST(test_smart_tab_insertion);
    RUN_TEST(test_auto_pair_parentheses);
    return UNITY_END();
}
