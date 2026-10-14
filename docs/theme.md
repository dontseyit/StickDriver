# Theme

One palette for the whole device, in [src/ui/Theme.h](../src/ui/Theme.h). Apps
name roles rather than colours, so the look holds together as apps are added and
a restyle is one edit.

## The palette

| Role | Hex | Where it is used |
|---|---|---|
| `kBg` | `#0A0C10` | the ground: near-black, slightly cool |
| `kSurface` | `#161A22` | panels, vials, the selected row |
| `kLine` | `#2A313D` | hairlines, ticks, rules |
| `kText` | `#E6EDF3` | anything to be read |
| `kDim` | `#7D8590` | labels and secondary readings |
| `kFaint` | `#4A525E` | the line under a row, units, things present but not wanted |
| `kAccent` | `#FFB300` | sonic amber, the device's own colour |
| `kCyan` | `#39C5CF` | the default accent an app takes |
| `kOk` | `#3FB950` | running, armed, passed |
| `kWarn` | `#D29922` | a setting that costs something, a reading near a limit |
| `kErr` | `#F85149` | a fault, a refusal, a confirmation about to delete |

The six greys and the three status colours come from GitHub's Primer dark
palette. That is why they sit together without tuning. Amber and cyan are this
project's own.

Amber is the identity. On near-black it reads as instrument light, and an
instrument is what the device is. It stays distinctive beside the blue-grey that
most dark interfaces use. Spend it sparingly: a screen with amber everywhere has no accent
at all.

## What an app picks

An app declares one `accent` in its `AppInfo` and the framework tints its chrome
with it. Most apps take `kCyan`. Amber belongs to the device rather than to any
one tool, so an app claiming it should have a reason.

Each region has one accent carrier. In the status bar the hairline under the
bar carries the app's accent, and the title above it is amber on every screen,
the device's own colour. In a list row the carrier is the bar down the left of
the selected row. The text around them stays on the grey ramp, so the accent is
the one thing the eye finds.

The status colours are not decoration. `kOk` means a thing is running, `kWarn`
means a reading or a setting carries a cost, and `kErr` means something failed or
is about to be destroyed. An app that tints a row green because green is pleasant
has spent the one signal that was worth having.

A chip or a toast picks its text colour from the fill, so an app cannot make an
unreadable one by passing something dark. The rule is in `mathx/Contrast.h`.

Never write a raw colour in an app. A measurement that moves between two roles
belongs in `mix565()`, which unpacks each channel first: interpolating the packed
RGB565 value carries the green field's low bits into red.

Two things are not chrome and do not come from the palette. A data colormap is
one: a spectrum running blue to red carries a reading, and eleven semantic roles
cannot express it. A game is the other: DIVERS colours an alien by what it is
worth, and the player learns the scale by looking. Both state in a comment what
the colours mean. Everything else takes a role.

## Type

`Font0` does most of the work. It advances six pixels a glyph, so a panel holds
about forty characters across, and a page of labels costs almost nothing. `Font2`
carries row titles and anything the eye lands on first. The large
`FreeSansBold` faces are for a single readout, the number the tool exists to
show.

That ratio is the character of the interface: many small labels around one big
number. An instrument, rather than an app.

Every string is drawn into 240 pixels. A longer one is clipped silently and
reads as a sentence that stops mid-word, so the debug build logs any string
wider than the panel as it is drawn.

## Chrome

The framework owns a 14 px status bar and a 12 px footer, so an app draws into
109 of the 135 rows. The bar carries the battery, the time an app has been open
and an optional badge; the footer carries up to two hints naming what `A` and `B`
do right now.

A list row is 26 px with its text inset 10 px from the left. A row with a
subtitle centres the title on 9 px and the subtitle on 19 px. Both lines are
fixed, so rows stay aligned as fonts change. The metrics are in `Theme.h`
beside the palette.

An app that wants the whole panel sets `fullscreen` and draws its own. DIVERS
does, because the game is the column of empty panel above the ship.

## Away from the device

The same palette works for anything else the project needs: a page, a release
graphic, a diagram. Amber on `#0A0C10`, the Primer greys for text, dark by
default rather than as a preference. A page that looks like the device is worth
more than a logo.
