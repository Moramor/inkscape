#define __SP_PATH_CHEMISTRY_C__

/*
 * Here are handlers for modifying selections, specific to paths
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <string.h>
#include <libart_lgpl/art_misc.h>
#include "xml/repr.h"
#include "xml/repr-private.h"
#include "svg/svg.h"
#include "helper/sp-intl.h"
#include "sp-object.h"
#include "sp-path.h"
#include "sp-text.h"
#include "style.h"
#include "inkscape.h"
#include "document.h"
#include "selection.h"
#include "desktop-handles.h"
#include "path-chemistry.h"
#include "desktop.h"

/* Helper functions for sp_selected_path_to_curves */
static void sp_selected_path_to_curves0 (gboolean do_document_done, guint32 text_grouping_policy);
static SPRepr * sp_selected_item_to_curved_repr(SPItem * item, guint32 text_grouping_policy);
enum {				
  /* Not used yet. This is the placeholder of Lauris's idea. */
	SP_TOCURVE_INTERACTIVE       = 1 << 0,
	SP_TOCURVE_GROUPING_BY_WORD  = 1 << 1,
	SP_TOCURVE_GROUPING_BY_LINE  = 1 << 2,
	SP_TOCURVE_GROUPING_BY_WHOLE = 1 << 3
};

void
sp_selected_path_combine (void)
{
	SPDesktop *desktop = SP_ACTIVE_DESKTOP;
	if (!SP_IS_DESKTOP(desktop)) return;
	SPSelection *selection = SP_DT_SELECTION (desktop);

	GSList *items = (GSList *) sp_selection_item_list (selection);

	if (g_slist_length (items) < 2) {
		sp_view_set_statusf_flash (SP_VIEW (desktop), _("Select at least 2 objects to combine."));
		return;
	}

	for (GSList *i = items; i != NULL; i = i->next) {
		SPItem *item = (SPItem *) i->data;
		if (!SP_IS_SHAPE (item) && !SP_IS_TEXT(item)) {
			sp_view_set_statusf_flash (SP_VIEW (desktop), _("One of the objects is not a path, cannot combine."));
			return;
		}
	}

	SPRepr *parent = SP_OBJECT_REPR ((SPItem *) items->data)->parent;
	for (GSList *i = items; i != NULL; i = i->next) {
		if ( SP_OBJECT_REPR ((SPItem *) i->data)->parent != parent ) {
			sp_view_set_statusf_error (SP_VIEW (desktop), _("You cannot combine objects from different groups or layers."));
			return;
		}
	}

	sp_selected_path_to_curves0 (FALSE, 0);

	items = (GSList *) sp_selection_item_list (selection);

	items = g_slist_copy (items);

	items = g_slist_sort (items, (GCompareFunc) sp_item_repr_compare_position);

	// remember the position of the topmost object
	gint topmost = sp_repr_position (SP_OBJECT_REPR ((SPItem *) g_slist_last(items)->data));

	// FIXME: merge styles of combined objects instead of using the first one's style
	gchar *style = g_strdup (sp_repr_attr (SP_OBJECT_REPR ((SPItem *) items->data), "style"));

	GString *dstring = g_string_new("");
	for (GSList *i = items; i != NULL; i = i->next) {

		SPPath *path = (SPPath *) i->data;
		SPCurve *c = sp_shape_get_curve (SP_SHAPE (path));

		NRMatrix i2root;
		sp_item_i2root_affine (SP_ITEM (path), &i2root);

		ArtBpath *abp = art_bpath_affine_transform (c->bpath, NR_MATRIX_D_TO_DOUBLE (&i2root));
		sp_curve_unref (c);
		gchar *str = sp_svg_write_path (abp);
		art_free (abp);

		dstring = g_string_append(dstring, str);
		g_free (str);

		sp_repr_unparent (SP_OBJECT_REPR (path));
		topmost --;
	}

	g_slist_free (items);

	SPRepr *repr = sp_repr_new ("path");

	sp_repr_set_attr (repr, "style", style);
	g_free (style);

	sp_repr_set_attr (repr, "d", dstring->str);
	g_string_free (dstring, TRUE);

	// add the new group to the group members' common parent
	sp_repr_append_child (parent, repr);

	// move to the position of the topmost, reduced by the number of deleted items
	sp_repr_set_position_absolute (repr, topmost > 0 ? topmost + 1 : 0);

	sp_document_done (SP_DT_DOCUMENT (desktop));

	sp_selection_set_repr (selection, repr);

	sp_repr_unref (repr);
}

void
sp_selected_path_break_apart (void)
{
	SPDesktop *desktop = SP_ACTIVE_DESKTOP;
	if (!SP_IS_DESKTOP(desktop)) return;
	SPSelection *selection = SP_DT_SELECTION (desktop);

	if (sp_selection_is_empty (selection)) {
		sp_view_set_statusf_flash (SP_VIEW(desktop), _("Select some paths to break apart."));
		return;
	}

	bool did = false;

	for (GSList *items = g_slist_copy((GSList *) sp_selection_item_list(SP_DT_SELECTION(desktop)));
			 items != NULL;
			 items = items->next) {

		SPItem *item = (SPItem *) items->data;

		if (!SP_IS_PATH (item)) 
			continue;

		SPPath *path = SP_PATH (item);

		SPCurve *curve = sp_shape_get_curve (SP_SHAPE (path));
		if (curve == NULL) 
			continue;

		did = true;

		SPRepr *parent = SP_OBJECT_REPR (item)->parent;
		gint pos = sp_repr_position (SP_OBJECT_REPR (item));

		NRMatrix i2root;
		sp_item_i2root_affine (SP_ITEM (path), &i2root);

		gchar *style = g_strdup (sp_repr_attr (SP_OBJECT (item)->repr, "style"));

		ArtBpath *abp = art_bpath_affine_transform (curve->bpath, NR_MATRIX_D_TO_DOUBLE(&i2root));

		sp_curve_unref (curve);
		sp_repr_unparent (SP_OBJECT_REPR (item));

		curve = sp_curve_new_from_bpath (abp);

		GSList *list = sp_curve_split (curve);

		sp_curve_unref (curve);

		for (GSList *l = list; l != NULL; l = l->next) {
			curve = (SPCurve *) l->data;

			SPRepr *repr = sp_repr_new ("path");
			sp_repr_set_attr (repr, "style", style);

			gchar *str = sp_svg_write_path (curve->bpath);
			sp_repr_set_attr (repr, "d", str);
			g_free (str);

			// add the new repr to the parent
			sp_repr_append_child (parent, repr);

			// move to the saved position 
			sp_repr_set_position_absolute (repr, pos > 0 ? pos : 0);

			sp_selection_add_repr (selection, repr);

			sp_repr_unref (repr);
		}

		g_slist_free (list);
		g_free (style);

	}

 	if (did) {
		sp_document_done (SP_DT_DOCUMENT (desktop));
	} else {
		sp_view_set_statusf_flash (SP_VIEW(desktop), _("No paths to break apart in the selection."));
		return;
	} 
}

/* This function is an entry point from GUI */
void
sp_selected_path_to_curves (void)
{
	sp_selected_path_to_curves0(TRUE, SP_TOCURVE_INTERACTIVE);
}

static void
sp_selected_path_to_curves0 (gboolean do_document_done, guint32 text_grouping_policy)
{
	SPDesktop *dt;
	SPItem *item;
	SPRepr  *repr;
	
	GSList * il;
	GSList * l;
	SPObject *parent;
	
	dt = SP_ACTIVE_DESKTOP;
	if (!dt) return;
	il = (GSList *) sp_selection_item_list (SP_DT_SELECTION (dt));
	if (!il) return;

	l = il;
	while (l) {
		item = (SPItem *)l->data;
		l    = l->next;
		repr = sp_selected_item_to_curved_repr (item, 0);
		if (!repr)
			continue;
		parent = SP_OBJECT_PARENT (item);
		sp_repr_add_child (SP_OBJECT_REPR (parent), 
				   repr, SP_OBJECT_REPR (item));
		sp_repr_unparent (SP_OBJECT_REPR (item));
		sp_selection_add_repr (SP_DT_SELECTION (dt), repr);
		sp_repr_unref(repr);
	}
	if (do_document_done)
		sp_document_done (SP_DT_DOCUMENT (dt));
}

static SPRepr *
sp_selected_item_to_curved_repr(SPItem * item, guint32 text_grouping_policy)
{
	SPCurve *curve;
	SPRepr  *repr;
	gchar  *style_str;
	gchar  *def_str;
	
	if (!item)
		return NULL;
		
	if (SP_IS_SHAPE (item)) {
		curve = sp_shape_get_curve (SP_SHAPE (item));
	} else if (SP_IS_TEXT (item)) {
		curve = sp_text_normalized_bpath (SP_TEXT (item));
	} else {
		curve = NULL;
	}
	
	if (!curve) return NULL;
	
	repr = sp_repr_new ("path");
	/* Transformation */
	sp_repr_set_attr (repr, "transform", 
			  sp_repr_attr (SP_OBJECT_REPR (item), "transform"));
	/* Style */
	style_str = sp_style_write_difference (SP_OBJECT_STYLE (item), 
					       SP_OBJECT_STYLE (SP_OBJECT_PARENT (item)));
	sp_repr_set_attr (repr, "style", style_str);
	g_free (style_str);
	/* Definition */
	def_str = sp_svg_write_path (curve->bpath);
	sp_repr_set_attr (repr, "d", def_str);
	g_free (def_str);
	sp_curve_unref (curve);
	return repr;
}

void
sp_path_cleanup (SPPath *path)
{
	SPCurve *curve;
	GSList *curves, *c;
	SPStyle *style;
	gboolean dropped;

	if (strcmp (sp_repr_name (SP_OBJECT_REPR (path)), "path")) return;

	style = SP_OBJECT_STYLE (path);
	if (style->fill.type == SP_PAINT_TYPE_NONE) return;

	curve = sp_shape_get_curve (SP_SHAPE (path));
	if (!curve) return;
	c = sp_curve_split (curve);
	sp_curve_unref (curve);

	dropped = FALSE;
	curves = NULL;
	while (c) {
		curve = (SPCurve *) c->data;
		if (curve->closed) {
			curves = g_slist_prepend (curves, curve);
		} else {
			dropped = TRUE;
			sp_curve_unref (curve);
		}
		c = g_slist_remove (c, c->data);
	}
	curves = g_slist_reverse (curves);

	curve = sp_curve_concat (curves);

	while (curves) {
		sp_curve_unref ((SPCurve *) curves->data);
		curves = g_slist_remove (curves, curves->data);
	}

	if (sp_curve_is_empty (curve)) {
		sp_repr_unparent (SP_OBJECT_REPR (path));
	} else if (dropped) {
		gchar *svgpath;
		svgpath = sp_svg_write_path (curve->bpath);
		sp_repr_set_attr (SP_OBJECT_REPR (path), "d", svgpath);
		g_free (svgpath);
	}

	sp_curve_unref (curve);
}

