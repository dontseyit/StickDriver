#pragma once
// -----------------------------------------------------------------------------
//  Which row a list draws first.
//
//  The list scrolls only when the cursor would otherwise leave the screen, so a
//  step down the list moves the cursor and not the page. It lives apart from
//  the drawing code so the host tests can reach it.
// -----------------------------------------------------------------------------
namespace sd {

/// The first visible row, given where the cursor is now and where the window
/// was. `visible` is how many rows fit; zero or fewer means nothing is drawn.
constexpr int listWindowTop(int sel, int count, int visible, int top) {
    if (count <= 0 || visible <= 0) return 0;
    if (sel >= count) sel = count - 1;
    if (sel < 0) sel = 0;

    if (sel < top) top = sel;
    if (sel >= top + visible) top = sel - visible + 1;
    // A list shorter than the window has nothing to scroll, and clamping the
    // other way round would leave a negative top.
    if (top > count - visible) top = count - visible;
    if (top < 0) top = 0;
    return top;
}

}  // namespace sd
