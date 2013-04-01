#ifndef SEEN_SP_PAINT_SERVER_H
#define SEEN_SP_PAINT_SERVER_H

/*
 * Base class for gradients and patterns
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2010 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <cairo.h>
#include <2geom/rect.h>
#include "sp-object.h"
#include "uri-references.h"

#define SP_TYPE_PAINT_SERVER (sp_paint_server_get_type())
#define SP_PAINT_SERVER(obj) ((SPPaintServer*)obj)
#define SP_IS_PAINT_SERVER(obj) (obj != NULL && static_cast<const SPObject*>(obj)->typeHierarchy.count(typeid(SPPaintServer)))

GType sp_paint_server_get_type(void) G_GNUC_CONST;

class CPaintServer;

class SPPaintServer : public SPObject {
public:
	CPaintServer* cpaintserver;

protected:
    bool swatch;
public:

    bool isSwatch() const;
    bool isSolid() const;
};

struct SPPaintServerClass {
    SPObjectClass sp_object_class;
};


class CPaintServer : public CObject {
public:
	CPaintServer(SPPaintServer* paintserver);
	virtual ~CPaintServer();

	virtual cairo_pattern_t* pattern_new(cairo_t *ct, Geom::OptRect const &bbox, double opacity);

protected:
	SPPaintServer* sppaintserver;
};


cairo_pattern_t *sp_paint_server_create_pattern(SPPaintServer *ps, cairo_t *ct, Geom::OptRect const &bbox, double opacity);


#endif // SEEN_SP_PAINT_SERVER_H
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
