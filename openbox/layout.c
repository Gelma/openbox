/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   layout.c for the Openbox window manager
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

gboolean ob_layout_validate(ObDesktopLayout *l, guint num)
{
    if (num == 0)
        return FALSE;

    if (l->columns == 0 && l->rows == 0) /* both 0's is bad data.. */
        return FALSE;

    /* fill in a zero rows/columns */
    if (l->columns == 0) {
        l->columns = num / l->rows;
        if (l->rows * l->columns < num)
            l->columns++;
        if (l->rows * l->columns >= num + l->columns)
            l->rows--;
    } else if (l->rows == 0) {
        l->rows = num / l->columns;
        if (l->columns * l->rows < num)
            l->rows++;
        if (l->columns * l->rows >= num + l->rows)
            l->columns--;
    }

    /* bounds checking */
    if (l->orientation == OB_ORIENTATION_HORZ) {
        l->columns = MIN(num, l->columns);
        l->rows = MIN(l->rows, (num + l->columns - 1) / l->columns);
        l->columns = num / l->rows + !!(num % l->rows);
    } else {
        l->rows = MIN(num, l->rows);
        l->columns = MIN(l->columns, (num + l->rows - 1) / l->rows);
        l->rows = num / l->columns + !!(num % l->columns);
    }
    return TRUE;
}

gboolean ob_layout_equal(const ObDesktopLayout *a, const ObDesktopLayout *b)
{
    return a->orientation == b->orientation &&
        a->start_corner == b->start_corner &&
        a->rows == b->rows &&
        a->columns == b->columns;
}

ObLayoutSource ob_layout_resolve(const ObDesktopLayout *prop,
                                 const ObDesktopLayout *cfg,
                                 gboolean cfg_authoritative,
                                 const ObDesktopLayout *cur,
                                 guint num,
                                 ObDesktopLayout *out)
{
    ObDesktopLayout l;

    if (cfg && cfg_authoritative) {
        l = *cfg;
        if (ob_layout_validate(&l, num)) {
            *out = l;
            return OB_LAYOUT_SOURCE_CONFIG;
        }
    }
    if (prop) {
        l = *prop;
        if (ob_layout_validate(&l, num)) {
            *out = l;
            return OB_LAYOUT_SOURCE_PROPERTY;
        }
    }
    if (cfg) {
        l = *cfg;
        if (ob_layout_validate(&l, num)) {
            *out = l;
            return OB_LAYOUT_SOURCE_CONFIG;
        }
    }
    if (cur) {
        l = *cur;
        if (ob_layout_validate(&l, num)) {
            *out = l;
            return OB_LAYOUT_SOURCE_CURRENT;
        }
    }

    l.orientation = OB_ORIENTATION_HORZ;
    l.start_corner = OB_CORNER_TOPLEFT;
    l.rows = 1;
    l.columns = MAX(num, 1);
    *out = l;
    return OB_LAYOUT_SOURCE_DEFAULT;
}
