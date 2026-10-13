// -----------------------------------------------------------------------------
//  Host tests for the app registry.  pio test -e native
//
//  Every app puts itself here at static-init time, and the order they come back
//  in IS the launcher. That order has to be a property of the apps rather than
//  of the build: link order is not something anybody chose, and a list that
//  rearranges itself because a file was renamed is one you cannot learn.
//
//  So the sort is worth pinning. It is stable, two apps that tie keeping the
//  order they registered in, and it sorts on what the reader can SEE: the pin
//  and the title. Sorting on group first put RIDE between LEVEL and ROBOT JIG
//  for no visible reason.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "core/AppRegistry.h"

using namespace sd;

namespace {

/// A factory that is never called. These tests are about the list, not about
/// starting anything, but a null one is refused and rightly.
App* never() { return nullptr; }

AppInfo app(const char* id, const char* title, uint8_t order = 128) {
    AppInfo a;
    a.id    = id;
    a.title = title;
    a.order = order;
    return a;
}

AppRegistry& fresh() {
    AppRegistry& r = AppRegistry::instance();
    r.clear();
    return r;
}

/// The titles in the order the launcher would draw them.
void titles(const AppRegistry& r, char* out, size_t cap) {
    out[0] = '\0';
    for (size_t i = 0; i < r.count(); ++i) {
        std::snprintf(out + std::strlen(out), cap - std::strlen(out), "%s%s",
                      i ? "," : "", r.at(i).info->title);
    }
}

}  // namespace

// -----------------------------------------------------------------------------
//  Taking an app
// -----------------------------------------------------------------------------
void test_an_app_can_be_found_by_its_id(void) {
    AppRegistry&  r = fresh();
    const AppInfo a = app("ride", "RIDE");
    TEST_ASSERT_TRUE(r.add(a, never));
    TEST_ASSERT_EQUAL_UINT32(1u, r.count());
    TEST_ASSERT_EQUAL_PTR(&a, r.find("ride")->info);
}

void test_an_id_nobody_registered_is_not_found(void) {
    AppRegistry&  r = fresh();
    const AppInfo a = app("ride", "RIDE");
    r.add(a, never);
    TEST_ASSERT_NULL(r.find("art"));
    TEST_ASSERT_NULL(r.find(nullptr));
    TEST_ASSERT_NULL(r.find(""));
}

void test_an_app_with_no_way_to_start_it_is_refused(void) {
    AppRegistry&  r = fresh();
    const AppInfo a = app("ride", "RIDE");
    TEST_ASSERT_FALSE(r.add(a, nullptr));
    TEST_ASSERT_EQUAL_UINT32(0u, r.count());
}

void test_the_registry_stops_rather_than_overruns(void) {
    AppRegistry& r = fresh();
    AppInfo      held[AppRegistry::kMaxApps + 2];
    char         ids[AppRegistry::kMaxApps + 2][8];

    for (size_t i = 0; i < AppRegistry::kMaxApps + 2; ++i) {
        std::snprintf(ids[i], sizeof(ids[i]), "a%u", static_cast<unsigned>(i));
        held[i] = app(ids[i], ids[i]);
        TEST_ASSERT_EQUAL(i < AppRegistry::kMaxApps, r.add(held[i], never));
    }
    TEST_ASSERT_EQUAL_UINT32(AppRegistry::kMaxApps, r.count());
}

// -----------------------------------------------------------------------------
//  The order the launcher draws
// -----------------------------------------------------------------------------
void test_the_list_is_in_title_order(void) {
    AppRegistry&  r = fresh();
    const AppInfo c = app("c", "MINES");
    const AppInfo a = app("a", "LEVEL");
    const AppInfo b = app("b", "RIDE");
    r.add(c, never); r.add(a, never); r.add(b, never);
    r.sort();

    char got[128];
    titles(r, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("LEVEL,MINES,RIDE", got);
}

void test_a_lower_order_comes_first(void) {
    // Order is a sort key because the launcher is where it shows.
    AppRegistry&  r = fresh();
    const AppInfo a = app("a", "ANALYSER");
    const AppInfo z = app("z", "ZZZ", /*order*/ 0);
    r.add(a, never); r.add(z, never);
    r.sort();

    char got[128];
    titles(r, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("ZZZ,ANALYSER", got);
}

void test_a_higher_order_goes_to_the_bottom(void) {
    AppRegistry& r = fresh();
    r.add(app("h", "HELP", /*order*/ 255), never);
    r.add(app("a", "ALPHA"), never);
    r.add(app("z", "ZULU"), never);
    r.sort();

    char got[128];
    titles(r, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("ALPHA,ZULU,HELP", got);
}

void test_every_order_is_available_not_just_the_two_ends(void) {
    // The point of a number rather than a flag: an order nobody could spell
    // with a top and a bottom.
    AppRegistry& r = fresh();
    r.add(app("d", "DELTA", 200), never);
    r.add(app("a", "ALPHA", 10), never);
    r.add(app("c", "CHARLIE", 100), never);
    r.add(app("b", "BRAVO", 50), never);
    r.sort();

    char got[128];
    titles(r, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("ALPHA,BRAVO,CHARLIE,DELTA", got);
}

void test_apps_sharing_an_order_fall_back_to_the_title(void) {
    AppRegistry& r = fresh();
    r.add(app("z", "ZEBRA", 10), never);
    r.add(app("h", "HELP", 10), never);
    r.add(app("a", "ALPHA"), never);
    r.sort();

    char got[128];
    titles(r, got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("HELP,ZEBRA,ALPHA", got);
}

void test_the_default_leaves_room_at_both_ends(void) {
    AppRegistry& r = fresh();
    AppInfo      held[12];
    char         ids[12][8];
    const char*  names[12] = {"M","C","Z","A","Q","B","Y","D","N","E","X","F"};
    for (int i = 0; i < 12; ++i) {
        std::snprintf(ids[i], sizeof(ids[i]), "i%d", i);
        held[i] = app(ids[i], names[i], (i % 4) == 0 ? 0 : 128);
        r.add(held[i], never);
    }
    r.sort();

    TEST_ASSERT_EQUAL_UINT32(12u, r.count());
    for (int i = 0; i < 12; ++i) TEST_ASSERT_NOT_NULL(r.find(ids[i]));
    // And it really is sorted: by order, then ascending by title within a run.
    for (size_t i = 1; i < r.count(); ++i) {
        const AppInfo& prev = *r.at(i - 1).info;
        const AppInfo& cur  = *r.at(i).info;
        if (prev.order != cur.order) {
            TEST_ASSERT_TRUE(prev.order < cur.order);
        } else {
            TEST_ASSERT_TRUE(std::strcmp(prev.title, cur.title) <= 0);
        }
    }
}

void test_apps_that_tie_keep_the_order_they_registered_in(void) {
    // The whole point of a stable sort: two apps sharing a title must not swap
    // places because somebody renamed a file and the linker changed its mind.
    // Distinguished by id, since the titles cannot tell them apart.
    AppRegistry&  r = fresh();
    const AppInfo first  = app("first",  "SAME");
    const AppInfo second = app("second", "SAME");
    const AppInfo third  = app("third",  "SAME");
    r.add(first, never); r.add(second, never); r.add(third, never);
    r.sort();

    TEST_ASSERT_EQUAL_STRING("first",  r.at(0).info->id);
    TEST_ASSERT_EQUAL_STRING("second", r.at(1).info->id);
    TEST_ASSERT_EQUAL_STRING("third",  r.at(2).info->id);
}

void test_sorting_loses_nothing_and_invents_nothing(void) {
    AppRegistry& r = fresh();
    AppInfo      held[12];
    char         ids[12][8];
    // Titles that fall in a different order from the one they arrive in.
    const char*  names[12] = {"M","C","Z","A","Q","B","Y","D","N","E","X","F"};
    for (int i = 0; i < 12; ++i) {
        std::snprintf(ids[i], sizeof(ids[i]), "i%d", i);
        held[i] = app(ids[i], names[i], /*order*/ (i % 4) == 0 ? 0 : 128);
        r.add(held[i], never);
    }
    r.sort();

    TEST_ASSERT_EQUAL_UINT32(12u, r.count());
    for (int i = 0; i < 12; ++i) TEST_ASSERT_NOT_NULL(r.find(ids[i]));
    // And it really is sorted: by order, each run ascending by title.
    for (size_t i = 1; i < r.count(); ++i) {
        const AppInfo& prev = *r.at(i - 1).info;
        const AppInfo& cur  = *r.at(i).info;
        if (prev.order != cur.order) {
            TEST_ASSERT_TRUE(prev.order < cur.order);  // never out of order
        } else {
            TEST_ASSERT_TRUE(std::strcmp(prev.title, cur.title) <= 0);
        }
    }
}

void test_sorting_nothing_is_not_a_crash(void) {
    AppRegistry& r = fresh();
    r.sort();
    TEST_ASSERT_EQUAL_UINT32(0u, r.count());
}


// -----------------------------------------------------------------------------
int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_an_app_can_be_found_by_its_id);
    RUN_TEST(test_an_id_nobody_registered_is_not_found);
    RUN_TEST(test_an_app_with_no_way_to_start_it_is_refused);
    RUN_TEST(test_the_registry_stops_rather_than_overruns);

    RUN_TEST(test_the_list_is_in_title_order);
    RUN_TEST(test_a_lower_order_comes_first);
    RUN_TEST(test_a_higher_order_goes_to_the_bottom);
    RUN_TEST(test_every_order_is_available_not_just_the_two_ends);
    RUN_TEST(test_apps_sharing_an_order_fall_back_to_the_title);
    RUN_TEST(test_the_default_leaves_room_at_both_ends);
    RUN_TEST(test_apps_that_tie_keep_the_order_they_registered_in);
    RUN_TEST(test_sorting_loses_nothing_and_invents_nothing);
    RUN_TEST(test_sorting_nothing_is_not_a_crash);

    return UNITY_END();
}
