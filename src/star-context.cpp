#define __SP_STAR_CONTEXT_C__

/*
 * Star drawing context
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 * Copyright (C) 2001-2002 Mitsuru Oka
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktable.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkspinbutton.h>
#include <gtk/gtklabel.h>
#include <gdk/gdkkeysyms.h>

#include "macros.h"
#include "helper/sp-canvas.h"
#include "sp-star.h"
#include "inkscape.h"
#include "document.h"
#include "selection.h"
#include "desktop-handles.h"
#include "desktop-affine.h"
#include "desktop-snap.h"
#include "pixmaps/cursor-star.xpm"
#include "sp-metrics.h"
#include "helper/sp-intl.h"
#include "prefs-utils.h"

#include "star-context.h"
#include <libnr/nr-point-fns.h>

static void sp_star_context_class_init (SPStarContextClass * klass);
static void sp_star_context_init (SPStarContext * star_context);
static void sp_star_context_dispose (GObject *object);

static void sp_star_context_setup (SPEventContext *ec);
static void sp_star_context_set (SPEventContext *ec, const gchar *key, const gchar *val);
static gint sp_star_context_root_handler (SPEventContext *ec, GdkEvent *event);
static GtkWidget *sp_star_context_config_widget (SPEventContext *ec);

static void sp_star_drag (SPStarContext * sc, double x, double y, guint state);
static void sp_star_finish (SPStarContext * sc);

static SPEventContextClass * parent_class;

GtkType
sp_star_context_get_type (void)
{
	static GType type = 0;
	if (!type) {
		GTypeInfo info = {
			sizeof (SPStarContextClass),
			NULL, NULL,
			(GClassInitFunc) sp_star_context_class_init,
			NULL, NULL,
			sizeof (SPStarContext),
			4,
			(GInstanceInitFunc) sp_star_context_init,
			NULL,	/* value_table */
		};
		type = g_type_register_static (SP_TYPE_EVENT_CONTEXT, "SPStarContext", &info, (GTypeFlags)0);
	}
	return type;
}

static void
sp_star_context_class_init (SPStarContextClass * klass)
{
	GObjectClass * object_class;
	SPEventContextClass * event_context_class;

	object_class = (GObjectClass *) klass;
	event_context_class = (SPEventContextClass *) klass;

	parent_class = (SPEventContextClass*)g_type_class_peek_parent (klass);

	object_class->dispose = sp_star_context_dispose;

	event_context_class->setup = sp_star_context_setup;
	event_context_class->set = sp_star_context_set;
	event_context_class->root_handler = sp_star_context_root_handler;
	event_context_class->config_widget = sp_star_context_config_widget;
}

static void
sp_star_context_init (SPStarContext * star_context)
{
	SPEventContext * event_context;
	
	event_context = SP_EVENT_CONTEXT (star_context);

	event_context->cursor_shape = cursor_star_xpm;
	event_context->hot_x = 4;
	event_context->hot_y = 4;

	star_context->item = NULL;

	star_context->magnitude = 5;
	star_context->proportion = 0.5;
}

static void
sp_star_context_dispose (GObject *object)
{
	SPStarContext * sc;

	sc = SP_STAR_CONTEXT (object);

	/* fixme: This is necessary because we do not grab */
	if (sc->item) sp_star_finish (sc);

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
sp_star_context_setup (SPEventContext *ec)
{
	SPStarContext *sc;

	sc = SP_STAR_CONTEXT (ec);

	if (((SPEventContextClass *) parent_class)->setup)
		((SPEventContextClass *) parent_class)->setup (ec);

	sp_event_context_read (ec, "magnitude");
	sp_event_context_read (ec, "proportion");
}

static void
sp_star_context_set (SPEventContext *ec, const gchar *key, const gchar *val)
{
	SPStarContext *sc;

	sc = SP_STAR_CONTEXT (ec);

	if (!strcmp (key, "magnitude")) {
		sc->magnitude = (val) ? atoi (val) : 5;
		sc->magnitude = CLAMP (sc->magnitude, 3, 32);
	} else if (!strcmp (key, "proportion")) {
		sc->proportion = (val) ? atof (val) : 0.5;
		sc->proportion = CLAMP (sc->proportion, 0.01, 1.0);
	}
}

static gint
sp_star_context_root_handler (SPEventContext * event_context, GdkEvent * event)
{
	static gboolean dragging;
	SPStarContext * sc;
	gint ret;
	SPDesktop * desktop;

	desktop = event_context->desktop;
	sc = SP_STAR_CONTEXT (event_context);
	ret = FALSE;

	switch (event->type) {
	case GDK_BUTTON_PRESS:
		if (event->button.button == 1) {
			NRPoint fp;
			dragging = TRUE;
			/* Position center */
			sp_desktop_w2d_xy_point (event_context->desktop, &fp, event->button.x, event->button.y);
			sc->center = fp;
			/* Snap center to nearest magnetic point */
			sp_desktop_free_snap (event_context->desktop, sc->center);
			sp_canvas_item_grab (SP_CANVAS_ITEM (desktop->acetate),
						GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK,
						NULL, event->button.time);
			ret = TRUE;
		}
		break;
	case GDK_MOTION_NOTIFY:
		if (dragging && (event->motion.state & GDK_BUTTON1_MASK)) {
			NRPoint p;
			sp_desktop_w2d_xy_point (event_context->desktop, &p, event->motion.x, event->motion.y);
			sp_star_drag (sc, p.x, p.y, event->motion.state);
			ret = TRUE;
		}
		break;
	case GDK_BUTTON_RELEASE:
		if (event->button.button == 1) {
			dragging = FALSE;
			sp_star_finish (sc);
			ret = TRUE;
			sp_canvas_item_ungrab (SP_CANVAS_ITEM (desktop->acetate), event->button.time);
		}
		break;
	case GDK_KEY_PRESS:
		switch (event->key.keyval) {
		case GDK_Up: 
		case GDK_Down: 
		case GDK_KP_Up: 
		case GDK_KP_Down: 
			// prevent the zoom field from activation
			if (!MOD__CTRL_ONLY)
				ret = TRUE;
			break;
		default:
			break;
		}
	default:
		break;
	}

	if (!ret) {
		if (((SPEventContextClass *) parent_class)->root_handler)
			ret = ((SPEventContextClass *) parent_class)->root_handler (event_context, event);
	}

	return ret;
}

static void
sp_star_drag (SPStarContext * sc, double x, double y, guint state)
{
	SPStar *star;
	SPDesktop * desktop;
	GString * xs, * ys;
	gchar status[80];

	desktop = SP_EVENT_CONTEXT (sc)->desktop;

	int snaps = prefs_get_int_attribute ("options.rotationsnapsperpi", "value", 12);

	if (!sc->item) {
		SPRepr * repr, * style;
		SPCSSAttr * css;
		/* Create object */
		repr = sp_repr_new ("polygon");
                sp_repr_set_attr (repr, "sodipodi:type", "star");
		/* Set style */
		style = inkscape_get_repr (INKSCAPE, "tools.shapes.star");
		if (style) {
			css = sp_repr_css_attr_inherited (style, "style");
			sp_repr_css_set (repr, css, "style");
			sp_repr_css_attr_unref (css);
		}
		sc->item = (SPItem *) sp_document_add_repr (SP_DT_DOCUMENT (desktop), repr);
		sp_repr_unref (repr);
	}

	/* Free movement for corner point */
	NRPoint fp;
	sp_desktop_dt2root_xy_point (desktop, &fp, sc->center[NR::X], sc->center[NR::Y]);
	NR::Point p0 = fp;
	sp_desktop_dt2root_xy_point (desktop, &fp, x, y);
	NR::Point p1 = fp;
	sp_desktop_free_snap (desktop, p1);

	star = SP_STAR(sc->item);

	gdouble sides = (gdouble) sc->magnitude;
	NR::Point d = p1 - p0;
	gdouble r1 = NR::L2 (d);
	gdouble arg1 = atan2 (d);

  	if (state & GDK_CONTROL_MASK) { 
		arg1 = round (arg1/(M_PI/snaps))*(M_PI/snaps);
  	} 
	
	sp_star_position_set (star, sc->magnitude, p0, r1, r1 * sc->proportion, arg1, arg1 + M_PI / sides);

	/* status text */
	xs = SP_PT_TO_METRIC_STRING (fabs(p0[NR::X]), SP_DEFAULT_METRIC);
	ys = SP_PT_TO_METRIC_STRING (fabs(p0[NR::Y]), SP_DEFAULT_METRIC);
	sprintf (status, "Draw star at (%s,%s)", xs->str, ys->str);
	sp_view_set_status (SP_VIEW (desktop), status, FALSE);
	g_string_free (xs, FALSE);
	g_string_free (ys, FALSE);
}

static void
sp_star_finish (SPStarContext * sc)
{
	if (sc->item != NULL) {
		SPDesktop *desktop;
		SPObject  *object;

		desktop = SP_EVENT_CONTEXT (sc)->desktop;
		object  = SP_OBJECT(sc->item);
		
                sp_shape_set_shape (SP_SHAPE (sc->item));

		sp_object_invoke_write (object, NULL, SP_OBJECT_WRITE_EXT);

		sp_selection_set_item (SP_DT_SELECTION (desktop), sc->item);
		sp_document_done (SP_DT_DOCUMENT (desktop));

		sc->item = NULL;
	}
}

/* Gtk stuff */

static void
sp_sc_magnitude_value_changed (GtkAdjustment *adj, SPStarContext *sc)
{
	sp_repr_set_int (SP_EVENT_CONTEXT_REPR (sc), "magnitude", (gint) adj->value);
}

static void
sp_sc_proportion_value_changed (GtkAdjustment *adj, SPStarContext *sc)
{
	sp_repr_set_double (SP_EVENT_CONTEXT_REPR (sc), "proportion", adj->value);
}

static void
sp_sc_make_sides_flat_clicked(GtkWidget *widget, GtkObject *obj)
{
	GtkAdjustment *adj;
	gint n;
	gfloat proportion;
	gdouble k;
	
	adj = (GtkAdjustment*)gtk_object_get_data (obj, "magnitude");
	n   = (gint)adj->value;
	
	
	k = sin(M_PI/n);
	k = (1 - k) * (1 + k);
	proportion = (gfloat)sqrt((double)k);

	adj = (GtkAdjustment*)gtk_object_get_data (obj, "proportion");
	gtk_adjustment_set_value (adj, proportion);
}

static void
sp_sc_defaults (GtkWidget *widget, GtkObject *obj)
{
	GtkAdjustment *adj;

	adj = (GtkAdjustment*)gtk_object_get_data (obj, "magnitude");
	gtk_adjustment_set_value (adj, 3);
	adj = (GtkAdjustment*)gtk_object_get_data (obj, "proportion");
	gtk_adjustment_set_value (adj, 0.5);
}

static GtkWidget *
sp_star_context_config_widget (SPEventContext *ec)
{
	SPStarContext *sc;
	GtkWidget *tbl, *l, *sb, *b;
	GtkObject *a;

	sc = SP_STAR_CONTEXT (ec);

	tbl = gtk_table_new (4, 2, FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (tbl), 4);
	gtk_table_set_row_spacings (GTK_TABLE (tbl), 4);

	/* Magnitude */
	l = gtk_label_new (_("Corners:"));
	gtk_widget_show (l);
	gtk_misc_set_alignment (GTK_MISC (l), 1.0, 0.5);
	gtk_table_attach (GTK_TABLE (tbl), l, 0, 1, 0, 1, 
			  (GtkAttachOptions)0, 
			  (GtkAttachOptions)0, 
			  0, 0);
	a = gtk_adjustment_new (sc->magnitude, 3, 32, 1, 1, 1);
	gtk_object_set_data (GTK_OBJECT (tbl), "magnitude", a);
	sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 1, 0);
	gtk_widget_show (sb);
	gtk_table_attach (GTK_TABLE (tbl), sb, 1, 2, 0, 1, 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  (GtkAttachOptions)0, 
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (a), "value_changed", GTK_SIGNAL_FUNC (sp_sc_magnitude_value_changed), sc);

	/* Proportion */
	l = gtk_label_new (_("Proportion:"));
	gtk_widget_show (l);
	gtk_misc_set_alignment (GTK_MISC (l), 1.0, 0.5);
	gtk_table_attach (GTK_TABLE (tbl), l, 0, 1, 1, 2, 
			  (GtkAttachOptions)0, 
			  (GtkAttachOptions)0, 
			  0, 0);
	a = gtk_adjustment_new (sc->proportion, 0.01, 1.0, 0.01, 0.1, 0.1);
	gtk_object_set_data (GTK_OBJECT (tbl), "proportion", a);
	sb = gtk_spin_button_new (GTK_ADJUSTMENT (a), 0.1, 2);
	gtk_widget_show (sb);
	gtk_table_attach (GTK_TABLE (tbl), sb, 1, 2, 1, 2, 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  (GtkAttachOptions)0, 
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (a), "value_changed", GTK_SIGNAL_FUNC (sp_sc_proportion_value_changed), sc);
	

	/* Making sides flat */
	b = gtk_button_new_with_label (_("Make sides flat"));
	gtk_widget_show (b);
	gtk_table_attach (GTK_TABLE (tbl), b, 0, 2, 2, 3, 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (b), "clicked", GTK_SIGNAL_FUNC (sp_sc_make_sides_flat_clicked), tbl);
	
	/* Reset */
	b = gtk_button_new_with_label (_("Defaults"));
	gtk_widget_show (b);
	gtk_table_attach (GTK_TABLE (tbl), b, 0, 2, 3, 4, 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 
			  0, 0);
	gtk_signal_connect (GTK_OBJECT (b), "clicked", GTK_SIGNAL_FUNC (sp_sc_defaults), tbl);

	return tbl;
}

