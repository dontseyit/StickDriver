// -----------------------------------------------------------------------------
//  Host tests for the list window.  pio test -e native -f test_listwindow
//
//  The window is what decides which rows a list draws, and it is arithmetic
//  over three integers. Every list in the firmware uses it, so the cases that
//  nobody reaches by hand are worth pinning: an empty list, a window taller
//  than the list, and a cursor jumping past both ends.
// -----------------------------------------------------------------------------
#include <unity.h>

#include "mathx/ListWindow.h"

using namespace sd;

void setUp(void) {}
void tearDown(void) {}

void test_a_cursor_inside_the_window_does_not_scroll(void) {
    TEST_ASSERT_EQUAL_INT(3, listWindowTop(/*sel*/ 4, /*count*/ 20, /*visible*/ 4, /*top*/ 3));
}

void test_a_cursor_above_the_window_pulls_it_up(void) {
    TEST_ASSERT_EQUAL_INT(1, listWindowTop(1, 20, 4, 5));
}

void test_a_cursor_below_the_window_pushes_it_down(void) {
    // sel 9 with four visible rows puts the last row at 9, so the top is 6.
    TEST_ASSERT_EQUAL_INT(6, listWindowTop(9, 20, 4, 3));
}

void test_the_window_stops_at_the_end_of_the_list(void) {
    TEST_ASSERT_EQUAL_INT(16, listWindowTop(19, 20, 4, 18));
}

void test_an_empty_list_has_no_scroll(void) {
    TEST_ASSERT_EQUAL_INT(0, listWindowTop(0, 0, 4, 0));
}

void test_a_window_taller_than_the_list_starts_at_the_top(void) {
    TEST_ASSERT_EQUAL_INT(0, listWindowTop(2, 3, 10, 0));
}

void test_a_window_of_no_rows_is_answered_rather_than_divided_by(void) {
    TEST_ASSERT_EQUAL_INT(0, listWindowTop(5, 20, 0, 3));
}

void test_a_cursor_past_the_end_is_clamped(void) {
    // setCount() keeps sel in range, but the window must not trust that.
    TEST_ASSERT_EQUAL_INT(16, listWindowTop(99, 20, 4, 0));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_cursor_inside_the_window_does_not_scroll);
    RUN_TEST(test_a_cursor_above_the_window_pulls_it_up);
    RUN_TEST(test_a_cursor_below_the_window_pushes_it_down);
    RUN_TEST(test_the_window_stops_at_the_end_of_the_list);
    RUN_TEST(test_an_empty_list_has_no_scroll);
    RUN_TEST(test_a_window_taller_than_the_list_starts_at_the_top);
    RUN_TEST(test_a_window_of_no_rows_is_answered_rather_than_divided_by);
    RUN_TEST(test_a_cursor_past_the_end_is_clamped);
    return UNITY_END();
}
