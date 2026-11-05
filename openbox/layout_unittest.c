/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   layout_unittest.c for the Openbox window manager
   Copyright (c) 2026        Andrea Gelmini

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "layout.h"

#include <stdio.h>

static gint failures = 0;

static ObDesktopLayout layout(ObOrientation o, ObCorner c,
                              guint rows, guint columns)
{
    ObDesktopLayout l;

    l.orientation = o;
    l.start_corner = c;
    l.rows = rows;
    l.columns = columns;
    return l;
}

static void check(const gchar *what, gboolean ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

static void check_validate(const gchar *what, ObDesktopLayout in, guint num,
                           gboolean expected_ok, ObDesktopLayout expected)
{
    gboolean ok = ob_layout_validate(&in, num);

    check(what, ok == expected_ok &&
          (!ok || ob_layout_equal(&in, &expected)));
    if (ok && !expected_ok)
        printf("     returned ok for %s\n", what);
    if (ok && expected_ok && !ob_layout_equal(&in, &expected))
        printf("     got %ux%u, expected %ux%u\n",
               in.rows, in.columns, expected.rows, expected.columns);
}

static void check_resolve(const gchar *what,
                          const ObDesktopLayout *prop,
                          const ObDesktopLayout *cfg,
                          gboolean authoritative,
                          const ObDesktopLayout *cur,
                          guint num,
                          ObLayoutSource expected_source,
                          ObDesktopLayout expected)
{
    ObDesktopLayout out;
    ObLayoutSource src = ob_layout_resolve(prop, cfg, authoritative, cur,
                                           num, &out);

    check(what, src == expected_source && ob_layout_equal(&out, &expected));
    if (src != expected_source)
        printf("     got source %d, expected %d\n", src, expected_source);
    if (!ob_layout_equal(&out, &expected))
        printf("     got %ux%u, expected %ux%u\n",
               out.rows, out.columns, expected.rows, expected.columns);
}

int main(void)
{
    const ObDesktopLayout grid = layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT,
                                        3, 4);
    const ObDesktopLayout strip = layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT,
                                         1, 12);
    ObDesktopLayout invalid = layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT,
                                     0, 0);
    ObDesktopLayout other_corner;

    /* validation */
    check_validate("4x3 is kept for 12 desktops", grid, 12, TRUE, grid);
    check_validate("missing rows is computed", layout(OB_ORIENTATION_HORZ,
                                                      OB_CORNER_TOPLEFT, 0, 4),
                   12, TRUE, grid);
    check_validate("missing columns is computed", layout(OB_ORIENTATION_HORZ,
                                                         OB_CORNER_TOPLEFT, 3, 0),
                   12, TRUE, grid);
    check_validate("both zero is rejected", layout(OB_ORIENTATION_HORZ,
                                                   OB_CORNER_TOPLEFT, 0, 0),
                   12, FALSE, grid);
    check_validate("12x1 is a valid grid", strip, 12, TRUE, strip);
    check_validate("2x2 is expanded to hold 12", layout(OB_ORIENTATION_HORZ,
                                                         OB_CORNER_TOPLEFT, 2, 2),
                   12, TRUE, layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT, 2, 6));
    check_validate("vertical orientation keeps 3x4",
                   layout(OB_ORIENTATION_VERT, OB_CORNER_TOPRIGHT, 3, 4),
                   12, TRUE, layout(OB_ORIENTATION_VERT, OB_CORNER_TOPRIGHT,
                                    3, 4));
    check_validate("zero desktops is rejected", grid, 0, FALSE, grid);
    check_validate("more rows than desktops is clamped",
                   layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT, 5, 0),
                   2, TRUE, layout(OB_ORIENTATION_HORZ, OB_CORNER_TOPLEFT, 2, 1));

    /* equality */
    other_corner = grid;
    check("equal grids compare equal", ob_layout_equal(&grid, &other_corner));
    other_corner.start_corner = OB_CORNER_BOTTOMRIGHT;
    check("different corners compare different",
          !ob_layout_equal(&grid, &other_corner));

    /* resolution policy */
    check_resolve("authoritative config beats a pager",
                  &strip, &grid, TRUE, &strip, 12,
                  OB_LAYOUT_SOURCE_CONFIG, grid);
    check_resolve("respectPager lets the pager win",
                  &strip, &grid, FALSE, &grid, 12,
                  OB_LAYOUT_SOURCE_PROPERTY, strip);
    check_resolve("config is the fallback without a property",
                  NULL, &grid, FALSE, &strip, 12,
                  OB_LAYOUT_SOURCE_CONFIG, grid);
    check_resolve("current layout survives a lost property",
                  NULL, NULL, FALSE, &grid, 12,
                  OB_LAYOUT_SOURCE_CURRENT, grid);
    check_resolve("no information falls back to one row",
                  NULL, NULL, FALSE, NULL, 12,
                  OB_LAYOUT_SOURCE_DEFAULT, strip);
    check_resolve("an invalid config does not win",
                  &grid, &invalid, TRUE, NULL, 12,
                  OB_LAYOUT_SOURCE_PROPERTY, grid);
    check_resolve("authoritative config already matching the property",
                  &grid, &grid, TRUE, &grid, 12,
                  OB_LAYOUT_SOURCE_CONFIG, grid);

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
