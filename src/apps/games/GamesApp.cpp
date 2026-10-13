// -----------------------------------------------------------------------------
//  GAMES - the games, in one list. See apps/FolderApp.h for how a folder works.
// -----------------------------------------------------------------------------
#include "apps/FolderApp.h"

namespace sd {
inline constexpr AppInfo kGamesFolder{
    .id       = "games",
    .title    = "GAMES",
    .subtitle = "tiny little games",
    .group    = "PLAY",
    .order    = 50,
    .needs    = caps(Cap::Display),
    // Landscape, as the games are, so coming back does not turn the panel.
    .rotation = 1,
    .tickHz   = 20,
    .drawHz   = 20,
    // A list and a cursor; 160 MHz is plenty.
    .cpuMhz   = 160,
    .accent   = ui::theme::kOk,
    .what     = "The list of games. Each is still an app of its own.",
    .how      = "B to move the cursor, A to open.",
    .care     = "Hold B to go back to the list. A+B goes all the way to the launcher.",
};

using GamesApp = FolderApp<kGamesFolder>;

SD_REGISTER_APP(GamesApp, kGamesFolder)

}  // namespace sd
