// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This is what gets executed to initialize all of the modules.  For
 * the internal modules this invovles executing their initialization
 * functions, for external ones it involves reading their .spmodule
 * files and bringing them into Sodipodi.
 *
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *
 * Copyright (C) 2002-2004 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_EXTENSION_INIT_H__
#define INKSCAPE_EXTENSION_INIT_H__

namespace Inkscape {
namespace Extension {

void init ();
void load_user_extensions();
void load_shared_extensions();
void refresh_user_extensions();
} } /* namespace Inkscape::Extension */

#endif /* INKSCAPE_EXTENSION_INIT_H__ */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
