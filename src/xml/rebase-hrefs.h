// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2014 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef REBASE_HREFS_H_SEEN
#define REBASE_HREFS_H_SEEN

#include <vector>
#include "xml/attribute-record.h"
#include "xml/node.h"

class SPDocument;

namespace Inkscape {
namespace XML {

/**
 * Change relative hrefs in current root XML node (faster than full document generation)
 *
 * to be relative to \a new_base instead of doc.base.
 *
 * (NULL doc base or new_base is interpreted as current working directory.)
 *
 * @param spns True if doc should contain sodipodi:absref attributes.
 */
void rebase_hrefs(Inkscape::XML::Node *rootxml, gchar const *const old_base, gchar const *const new_base, bool const spns);
/**
 * Change relative hrefs in doc to be relative to \a new_base instead of doc.base.
 *
 * (NULL doc base or new_base is interpreted as current working directory.)
 *
 * @param spns True if doc should contain sodipodi:absref attributes.
 */
void rebase_hrefs(SPDocument *doc, char const *new_base, bool spns);

/**
 * Change relative xlink:href attributes to be relative to \a new_abs_base instead of old_abs_base.
 *
 * Note that old_abs_base and new_abs_base must each be non-NULL, absolute directory paths.
 */
AttributeVector rebase_href_attrs(
    char const *old_abs_base,
    char const *new_abs_base,
    const AttributeVector & attributes);


// /**
//  * .
//  * @return a non-empty replacement href if needed, empty otherwise.
//  */
// std::string rebase_href_attrs( std::string const &oldAbsBase, std::string const &newAbsBase, gchar const *href, gchar const *absref = 0 );

} // namespace XML
} // namespace Inkscape


#endif /* !REBASE_HREFS_H_SEEN */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vi: set autoindent shiftwidth=4 tabstop=8 filetype=cpp expandtab softtabstop=4 fileencoding=utf-8 textwidth=99 :
