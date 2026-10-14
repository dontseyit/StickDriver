// -----------------------------------------------------------------------------
//  Host tests for the contrast rule.  pio test -e native -f test_contrast
//
//  A chip and a toast draw text on a colour the app chose. The rule picks the
//  text colour from that background, so an app cannot produce an unreadable
//  one by passing something dark.
// -----------------------------------------------------------------------------
#include <unity.h>

#include "mathx/Contrast.h"
#include "ui/Theme.h"

using namespace sd;
using namespace sd::ui::theme;

void setUp(void) {}
void tearDown(void) {}

void test_a_light_background_takes_dark_text(void) {
    TEST_ASSERT_EQUAL_HEX16(kBg, readableOn(kAccent, kText, kBg));
    TEST_ASSERT_EQUAL_HEX16(kBg, readableOn(kCyan, kText, kBg));
}

void test_a_dark_background_takes_light_text(void) {
    TEST_ASSERT_EQUAL_HEX16(kText, readableOn(kBg, kText, kBg));
    // The darkest grey an app is likely to pass, and still too dark for kBg.
    TEST_ASSERT_EQUAL_HEX16(kText, readableOn(kFaint, kText, kBg));
}

void test_the_status_colours_all_take_dark_text(void) {
    // Every colour an app is meant to pass is light enough for kBg, so the
    // rule changes nothing for a well-behaved app.
    TEST_ASSERT_EQUAL_HEX16(kBg, readableOn(kErr, kText, kBg));
}

void test_luma_is_ordered(void) {
    TEST_ASSERT_TRUE(luma565(kText) > luma565(kAccent));
    TEST_ASSERT_TRUE(luma565(kAccent) > luma565(kBg));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_light_background_takes_dark_text);
    RUN_TEST(test_a_dark_background_takes_light_text);
    RUN_TEST(test_the_status_colours_all_take_dark_text);
    RUN_TEST(test_luma_is_ordered);
    return UNITY_END();
}
