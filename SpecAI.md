# SpecAI - Openbox Analysis & Desktop Grid Layout Specification

## 1. Project Overview & Architectural Structure

Openbox is a highly configurable, standards-compliant, lightweight stacking window manager for the X Window System (X11).

### 1.1 Source Tree Layout

- **`openbox/`** - Core Window Manager logic:
  - `screen.c`, `screen.h`: Virtual desktop handling, monitor management, XRandR handling, desktop layout geometry, strut/usable area calculations.
  - `config.c`, `config.h`: Parsing configuration settings from `rc.xml`.
  - `event.c`, `event.h`: Central X11 event dispatcher (handling `PropertyNotify`, `ConfigureNotify`, `ClientMessage`, etc.).
  - `client.c`, `client.h`: Client window lifecycle, frame management, state, transients, layer stacking.
  - `actions/`: Built-in action implementations (desktop navigation, window movement, cycling, exec, etc.).
  - `popup.c`, `popup.h`: Desktop pager popup and Alt-Tab switching popup.
  - `session.c`, `session.h`: XSMP (X Session Management Protocol) state save/restore.
  - `openbox.c`, `openbox.h`: Entry point, initialization lifecycle, main loop, signal handling.
  - `focus.c`, `place.c`, `resist.c`, `stacking.c`, `menu.c`, `keyboard.c`, `mouse.c`: Specific WM subsystems.
- **`obrender/`** - Theme rendering engine:
  - Appearance, colors, font rendering via Xft/Pango, pixmap masks, gradient generation.
- **`obt/`** - Openbox Toolkit:
  - Low-level X11 connection wrappers (`xconnection.c`, `display.c`), X atom caching (`prop.c`), XML parsing abstraction over `libxml2` (`xml.c`), keyboard/ddx mappings.
- **`data/`** - Default configuration and schema definitions:
  - `rc.xml`, `rc.xsd`: Default user configuration template and XML schema.
  - `menu.xml`, `menu.xsd`: Root and window menu definitions.
  - `autostart/`: System and user session autostart scripts.
- **`tools/`** - Utilities:
  - `obxprop`, `openbox-xdg-autostart`, `gnome-panel-control`.
- **Build System**:
  - GNU Autotools (`bootstrap`, `configure.ac`, `Makefile.am`).

---

## 2. Deep Dive: Virtual Desktop Layout Mechanics

### 2.1 EWMH Specification & `_NET_DESKTOP_LAYOUT`

Under the Extended Window Manager Hints (EWMH) specification:
- Property name: `_NET_DESKTOP_LAYOUT, CARDINAL[4]/32` on the X11 Root Window.
- Fields:
  1. `orientation`: `0` (`_NET_WM_ORIENTATION_HORZ`) or `1` (`_NET_WM_ORIENTATION_VERT`).
  2. `columns`: Number of columns in the desktop grid.
  3. `rows`: Number of rows in the desktop grid.
  4. `starting_corner`: `0` (Top-Left), `1` (Top-Right), `2` (Bottom-Right), `3` (Bottom-Left).
- Intended usage: EWMH specifies that pagers set this property to inform the window manager how virtual desktops are arranged. If no pager sets it, the window manager provides a default layout.

### 2.2 Openbox In-Memory Representation

In `openbox/screen.h`:
```c
typedef struct ObDesktopLayout {
    ObOrientation orientation;
    ObCorner start_corner;
    guint rows;
    guint columns;
} ObDesktopLayout;

extern ObDesktopLayout screen_desktop_layout;
```

In `openbox/screen.c`:
- `screen_desktop_layout` stores the active grid dimensions.
- Directional navigation (`GoToDesktop` actions with directions `North`, `South`, `East`, `West`), wrap-around math, and the desktop switch pager popup (`openbox/popup.c`) rely directly on `screen_desktop_layout.columns` and `screen_desktop_layout.rows`.

---

## 3. Analysis of the Issue: 4x3 Grid Reverting to 1x12 Strip

### 3.1 Problem Description

In user configurations such as `/tmp/testnec/.config/openbox`, a 12-desktop setup is configured:
- `rc.xml` sets `<number>12</number>`.
- `autostart` invokes `set_desktop_layout.py 4 3 &` to write `_NET_DESKTOP_LAYOUT = [0, 4, 3, 0]` on the root window.
- During normal usage, the grid reverts from 4x3 to a single horizontal strip of 12 columns (1 row x 12 columns).
- When this happens:
  - Up/Down desktop navigation fails or wraps onto the same desktop.
  - The desktop switch popup shows a single long line of 12 rectangles instead of a 4x3 grid.
  - `openbox --restart` temporarily restores the layout if `autostart` or session reloads.

### 3.2 Root Cause in Openbox Code

Inspecting `openbox/screen.c` and `openbox/event.c`:

1. **Unconditional Destructive Reset in `screen_update_layout()`**:
   ```c
   void screen_update_layout(void)
   {
       ObDesktopLayout l;
       guint32 *data;
       guint num;

       screen_desktop_layout.orientation = OB_ORIENTATION_HORZ;
       screen_desktop_layout.start_corner = OB_CORNER_TOPLEFT;
       screen_desktop_layout.rows = 1;
       screen_desktop_layout.columns = screen_num_desktops;

       if (OBT_PROP_GETA32(obt_root(ob_screen),
                           NET_DESKTOP_LAYOUT, CARDINAL, &data, &num)) {
           // parse and validate layout...
       }
   }
   ```
   At the very start of `screen_update_layout()`, Openbox forcibly sets `rows = 1` and `columns = screen_num_desktops`.
   If reading `_NET_DESKTOP_LAYOUT` fails (because it was deleted, cleared, or unavailable), Openbox leaves `rows = 1` and `columns = screen_num_desktops`.

2. **Triggering Events**:
   `screen_update_layout()` is invoked in several scenarios:
   - **`openbox/event.c:812` (`PropertyNotify`)**:
     Whenever `_NET_DESKTOP_LAYOUT` changes on the root window. Note that X11 sends `PropertyNotify` with `state == PropertyDelete` when an external application removes the property. Openbox does not check `state` and calls `screen_update_layout()`, finding the property gone and reverting to 1x12.
   - **`openbox/screen.c:351` (`screen_startup(reconfig=TRUE)`)**:
     Triggered by `openbox --reconfigure`. If the root property was wiped, reconfigure reverts the layout to 1x12.
   - **`openbox/screen.c:525` (`screen_set_num_desktops`)**:
     Triggered dynamically if desktop count changes.
   - **Panel / Dock / Pager Lifecycle**:
     External pagers/panels (tint2, lxpanel, polybar, xfce4-panel) often delete or overwrite `_NET_DESKTOP_LAYOUT` during initialization, crash, or reload.
   - **RandR / Display / Resolution changes**:
     Screen configuration changes (e.g. `xrandr`, display hotplugging) trigger screen reconfiguration and can lead to property loss on the root window.
   - **Race Conditions at Startup**:
     `autostart` runs `set_desktop_layout.py 4 3 &` in the background. If a panel or background tool starts right after and clears root window properties or sets its own layout, the user script is overridden.

3. **Absence of Configuration in `rc.xml`**:
   `openbox/config.c` only supports:
   - `<number>`: desktop count
   - `<firstdesk>`: initial desktop index
   - `<names>`: desktop names
   - `<popupTime>`: popup display duration
   There is currently no XML configuration tag in `<desktops>` for `<rows>`, `<columns>`, or `<orientation>`.

### 3.3 Historical Git Log Insights

- **`a055fd9e57cc0715d491405c41494b5e155c795c`** ("validate the desktop layout loaded from session"):
  Introduced `screen_validate_layout()` and layout validation logic. It made layout parsing safer against corrupted data, but codified the fallback to `rows = 1, columns = screen_num_desktops`.
- **`6f6ab8c442b1626a1cd28868900a00fe4bf8cb08`** ("save and restore the desktop number/layout/names in the session"):
  Added XSMP session support for `<desktoplayout>` (`orientation`, `startcorner`, `columns`, `rows`) saved to `~/.config/openbox/sessions/`. However, this mechanism only activates when running under an active XSMP session manager, which modern lightweight setups (like standalone Openbox) do not use.
- **`ead75d7f9f3633bee90be932b8db9976e6ab13ca`** ("Sometimes after reconfigure we forget this, so check again"):
  Mikael Magnusson added `screen_update_layout()` into `screen_startup(reconfig=TRUE)` precisely because layout state was lost or forgotten after reconfiguring Openbox.

---

## 4. Proposed Solution Strategies

### Strategy 1: Native Configuration Support in `rc.xml` (Recommended Architecture)
- Extend `<desktops>` in `data/rc.xml` and `data/rc.xsd` to support:
  ```xml
  <desktops>
    <number>12</number>
    <rows>3</rows>
    <columns>4</columns>
    <orientation>horizontal</orientation> <!-- horizontal or vertical -->
  </desktops>
  ```
- In `openbox/config.c`:
  Parse `rows`, `columns`, and `orientation` into configuration globals (`config_desktops_rows`, `config_desktops_columns`, `config_desktops_orientation`).
- In `openbox/screen.c`:
  - When initializing or when `_NET_DESKTOP_LAYOUT` is not present, use the configured rows/columns instead of hardcoded `1 x N`.
  - When `screen_update_layout()` is called, if the root window property is deleted or invalid, fall back to the configured grid instead of `rows = 1`.
  - Set `_NET_DESKTOP_LAYOUT` onto the root window on startup/reconfigure if configured in `rc.xml`, ensuring pagers and panels also know the user's intended layout.

### Strategy 2: Defensive State Retention in `screen_update_layout()`
- If `_NET_DESKTOP_LAYOUT` is deleted or absent:
  - Do not blindly overwrite `screen_desktop_layout` with `1 x N` if `screen_desktop_layout` already holds a valid layout matching `screen_num_desktops`.
  - Only update `screen_desktop_layout` if a valid property is provided, or if the number of desktops changed such that the old layout is invalid.

### Strategy 3: External Watchdog / Resilience Script
- While developing or testing C-level changes, an external daemon or script can monitor root window `PropertyNotify` events and immediately re-apply `_NET_DESKTOP_LAYOUT` if it is removed or altered.

---

## 5. Next Steps & Implementation Roadmap

1. Verify build environment and compilation with Autotools (`./bootstrap`, `./configure`, `make`).
2. Add `<rows>`, `<columns>`, and `<orientation>` support to `openbox/config.c`, `openbox/config.h`, and `data/rc.xsd`.
3. Update `openbox/screen.c` to use configured defaults in `screen_startup()` and `screen_update_layout()`.
4. Ensure Openbox sets and preserves `_NET_DESKTOP_LAYOUT` on the root window.
5. Test against edge cases: reconfigure, panel restart, desktop additions/removals, and RandR screen changes.
