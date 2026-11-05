/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   layout.h for the Openbox window manager
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

#ifndef __ob__layout_h
#define __ob__layout_h

#include "misc.h"

#include <glib.h>

/*! The geometry of the virtual desktop grid */
typedef struct ObDesktopLayout {
    ObOrientation orientation;
    ObCorner      start_corner;
    guint         rows;
    guint         columns;
} ObDesktopLayout;

/*! Where the layout chosen by ob_layout_resolve() comes from */
typedef enum {
    OB_LAYOUT_SOURCE_NONE,
    OB_LAYOUT_SOURCE_PROPERTY, /*!< _NET_DESKTOP_LAYOUT, from a pager */
    OB_LAYOUT_SOURCE_CONFIG,   /*!< the <layout> section of rc.xml */
    OB_LAYOUT_SOURCE_CURRENT,  /*!< the layout already held in memory */
    OB_LAYOUT_SOURCE_DEFAULT   /*!< one row, no other information */
} ObLayoutSource;

/*! Fill in a missing rows/columns and clamp the grid to hold num desktops.
  The layout is modified in place. Returns FALSE when the data is unusable. */
gboolean ob_layout_validate(ObDesktopLayout *l, guint num);

/*! TRUE when the two layouts describe the same grid. */
gboolean ob_layout_equal(const ObDesktopLayout *a, const ObDesktopLayout *b);

/*! Choose the layout to use.

  A configuration layout wins over everything when cfg_authoritative is TRUE
  (the default policy: the user asked for that grid in rc.xml). Without an
  authoritative configuration the property set by a pager is used, then the
  configuration as a fallback, then the layout already in use, so that a
  missing or malformed property never destroys a valid grid.

  prop, cfg and cur may be NULL. The result is written to out and its source
  is returned. */
ObLayoutSource ob_layout_resolve(const ObDesktopLayout *prop,
                                 const ObDesktopLayout *cfg,
                                 gboolean cfg_authoritative,
                                 const ObDesktopLayout *cur,
                                 guint num,
                                 ObDesktopLayout *out);

#endif
