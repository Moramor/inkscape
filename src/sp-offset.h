#ifndef __SP_OFFSET_H__
#define __SP_OFFSET_H__

/*
 * <sodipodi:offset> implementation
 *
 * Authors (of the sp-spiral.h upon which this file was created):
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "sp-shape.h"

#include <sigc++/sigc++.h>

#define SP_TYPE_OFFSET            (sp_offset_get_type ())
#define SP_OFFSET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SP_TYPE_OFFSET, SPOffset))
#define SP_OFFSET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SP_TYPE_OFFSET, SPOffsetClass))
#define SP_IS_OFFSET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SP_TYPE_OFFSET))
#define SP_IS_OFFSET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SP_TYPE_OFFSET))

class SPOffset;
class SPOffsetClass;
class SPUseReference;

struct SPOffset
{
  SPShape shape;

  /*
   * offset is defined by curve and radius
   * the original curve is kept as a path in a sodipodi:original attribute
   * it's not possible to change the original curve
   */
  void *originalPath; // will be a livarot Path, just don't declare it here to please the gcc linker
  char *original;     // SVG description of the source path
  float rad;			/* offset radius */

	// for interactive setting of the radius
  bool knotSet;
  NR::Point knot;
	
	bool           sourceDirty;
	bool           isUpdating;

	gchar					 *sourceHref;
	SPUseReference *sourceRef;
  SPRepr         *sourceRepr; // the repr associated with that id
	SPObject			 *sourceObject;
	
	gulong           _modified_connection;
	sigc::connection _delete_connection;
	sigc::connection _changed_connection;
	sigc::connection _transformed_connection;
};

struct SPOffsetClass
{
  SPShapeClass parent_class;
};


/* Standard Gtk function */
GType sp_offset_get_type (void);

double sp_offset_distance_to_original (SPOffset * offset, NR::Point px);
void sp_offset_top_point (SPOffset * offset, NR::Point *px);


#endif
