#define __SP_TEXT_CONTEXT_C__

/*
 * SPTextContext
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 1999-2005 authors
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <math.h>

#include <ctype.h>
#include <glib-object.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkmain.h>
#include <display/sp-ctrlline.h>
#include <display/sodipodi-ctrlrect.h>
#include <display/sp-ctrlquadr.h>
#include <libnr/nr-point-matrix-ops.h>
#include <gtk/gtkimmulticontext.h>

#include <gtkmm.h>

#include "macros.h"
#include "sp-text.h"
#include "sp-flowtext.h"
#include "inkscape.h"
#include "document.h"
#include "style.h"
#include "selection.h"
#include "desktop.h"
#include "desktop-style.h"
#include "desktop-handles.h"
#include "desktop-affine.h"
#include "message-stack.h"
#include "message-context.h"
#include "pixmaps/cursor-text.xpm"
#include "pixmaps/cursor-text-insert.xpm"
#include "ui/view/view.h"
#include <glibmm/i18n.h>
#include "object-edit.h"
#include "knotholder.h"
#include "xml/repr.h"
#include "xml/attribute-record.h"
#include "xml/node-event-vector.h"
#include "event-context.h"
#include "prefs-utils.h"
#include "rubberband.h"
#include "sp-metrics.h"

#include "text-editing.h"

#include "text-context.h"


static void sp_text_context_class_init(SPTextContextClass *klass);
static void sp_text_context_init(SPTextContext *text_context);
static void sp_text_context_dispose(GObject *obj);

static void sp_text_context_setup(SPEventContext *ec);
static void sp_text_context_finish(SPEventContext *ec);
static gint sp_text_context_root_handler(SPEventContext *event_context, GdkEvent *event);
static gint sp_text_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event);

static void sp_text_context_selection_changed(Inkscape::Selection *selection, SPTextContext *tc);
static void sp_text_context_selection_modified(Inkscape::Selection *selection, guint flags, SPTextContext *tc);
static bool sp_text_context_style_set(SPCSSAttr const *css, SPTextContext *tc);
static int sp_text_context_style_query(SPStyle *style, int property, SPTextContext *tc);

static void sp_text_context_validate_cursor_iterators(SPTextContext *tc);
static void sp_text_context_update_cursor(SPTextContext *tc, bool scroll_to_see = true);
static void sp_text_context_update_text_selection(SPTextContext *tc);
static gint sp_text_context_timeout(SPTextContext *tc);
static void sp_text_context_forget_text(SPTextContext *tc);

static gint sptc_focus_in(GtkWidget *widget, GdkEventFocus *event, SPTextContext *tc);
static gint sptc_focus_out(GtkWidget *widget, GdkEventFocus *event, SPTextContext *tc);
static void sptc_commit(GtkIMContext *imc, gchar *string, SPTextContext *tc);

static SPEventContextClass *parent_class;

GType
sp_text_context_get_type()
{
    static GType type = 0;
    if (!type) {
        GTypeInfo info = {
            sizeof(SPTextContextClass),
            NULL, NULL,
            (GClassInitFunc) sp_text_context_class_init,
            NULL, NULL,
            sizeof(SPTextContext),
            4,
            (GInstanceInitFunc) sp_text_context_init,
            NULL,   /* value_table */
        };
        type = g_type_register_static(SP_TYPE_EVENT_CONTEXT, "SPTextContext", &info, (GTypeFlags)0);
    }
    return type;
}

static void
sp_text_context_class_init(SPTextContextClass *klass)
{
    GObjectClass *object_class=(GObjectClass *)klass;
    SPEventContextClass *event_context_class = (SPEventContextClass *) klass;

    parent_class = (SPEventContextClass*)g_type_class_peek_parent(klass);

    object_class->dispose = sp_text_context_dispose;

    event_context_class->setup = sp_text_context_setup;
    event_context_class->finish = sp_text_context_finish;
    event_context_class->root_handler = sp_text_context_root_handler;
    event_context_class->item_handler = sp_text_context_item_handler;
}

static void
sp_text_context_init(SPTextContext *tc)
{
    SPEventContext *event_context = SP_EVENT_CONTEXT(tc);

    event_context->cursor_shape = cursor_text_xpm;
    event_context->hot_x = 7;
    event_context->hot_y = 7;

    event_context->xp = 0;
    event_context->yp = 0;
    event_context->tolerance = 0;
    event_context->within_tolerance = false;

    event_context->shape_repr = NULL;
    event_context->shape_knot_holder = NULL;

    tc->imc = NULL;

    tc->text = NULL;
    tc->pdoc = NR::Point(0, 0);
    new (&tc->text_sel_start) Inkscape::Text::Layout::iterator();
    new (&tc->text_sel_end) Inkscape::Text::Layout::iterator();
    new (&tc->text_selection_quads) std::vector<SPCanvasItem*>();

    tc->unimode = false;

    tc->cursor = NULL;
    tc->indicator = NULL;
    tc->frame = NULL;
    tc->grabbed = NULL;
    tc->timeout = 0;
    tc->show = FALSE;
    tc->phase = 0;
    tc->nascent_object = 0;
    tc->over_text = 0;
    tc->dragging = 0;
    tc->creating = 0;

    new (&tc->sel_changed_connection) sigc::connection();
    new (&tc->sel_modified_connection) sigc::connection();
    new (&tc->style_set_connection) sigc::connection();
    new (&tc->style_query_connection) sigc::connection();
}

static void
sp_text_context_dispose(GObject *obj)
{
    SPTextContext *tc = SP_TEXT_CONTEXT(obj);
    SPEventContext *ec = SP_EVENT_CONTEXT(tc);
    tc->style_query_connection.~connection();
    tc->style_set_connection.~connection();
    tc->sel_changed_connection.~connection();
    tc->sel_modified_connection.~connection();
    tc->text_sel_end.~iterator();
    tc->text_sel_start.~iterator();
    tc->text_selection_quads.~vector();
    if (G_OBJECT_CLASS(parent_class)->dispose) {
        G_OBJECT_CLASS(parent_class)->dispose(obj);
    }
    if (tc->grabbed) {
        sp_canvas_item_ungrab(tc->grabbed, GDK_CURRENT_TIME);
        tc->grabbed = NULL;
    }
    NRRect b;
    if (sp_rubberband_rect(&b)) {
        sp_rubberband_stop();
    }
    if (ec->shape_knot_holder) {
        sp_knot_holder_destroy(ec->shape_knot_holder);
        ec->shape_knot_holder = NULL;
    }
    if (ec->shape_repr) { // remove old listener
        sp_repr_remove_listener_by_data(ec->shape_repr, ec);
        sp_repr_unref(ec->shape_repr);
        ec->shape_repr = 0;
    }
}

static Inkscape::XML::NodeEventVector ec_shape_repr_events = {
    NULL, /* child_added */
    NULL, /* child_removed */
    ec_shape_event_attr_changed,
    NULL, /* content_changed */
    NULL  /* order_changed */
};

static void
sp_text_context_setup(SPEventContext *ec)
{
    SPTextContext *tc = SP_TEXT_CONTEXT(ec);
    SPDesktop *desktop = ec->desktop;

    tc->cursor = sp_canvas_item_new(SP_DT_CONTROLS(desktop), SP_TYPE_CTRLLINE, NULL);
    sp_ctrlline_set_coords(SP_CTRLLINE(tc->cursor), 100, 0, 100, 100);
    sp_ctrlline_set_rgba32(SP_CTRLLINE(tc->cursor), 0x000000ff);
    sp_canvas_item_hide(tc->cursor);

    tc->indicator = sp_canvas_item_new(SP_DT_CONTROLS(desktop), SP_TYPE_CTRLRECT, NULL);
    sp_ctrlrect_set_area(SP_CTRLRECT(tc->indicator), 0, 0, 100, 100);
    sp_ctrlrect_set_color(SP_CTRLRECT(tc->indicator), 0x0000ff7f, FALSE, 0);
    sp_canvas_item_hide(tc->indicator);

    tc->frame = sp_canvas_item_new(SP_DT_CONTROLS(desktop), SP_TYPE_CTRLRECT, NULL);
    sp_ctrlrect_set_area(SP_CTRLRECT(tc->frame), 0, 0, 100, 100);
    sp_ctrlrect_set_color(SP_CTRLRECT(tc->frame), 0x0000ff7f, FALSE, 0);
    sp_canvas_item_hide(tc->frame);

    tc->timeout = gtk_timeout_add(250, (GtkFunction) sp_text_context_timeout, ec);

    tc->imc = gtk_im_multicontext_new();
    if (tc->imc) {
        GtkWidget *canvas = GTK_WIDGET(SP_DT_CANVAS(desktop));

        /* im preedit handling is very broken in inkscape for
         * multi-byte characters.  See bug 1086769.
         * We need to let the IM handle the preediting, and
         * just take in the characters when they're finished being
         * entered.
         */
        gtk_im_context_set_use_preedit(tc->imc, FALSE);
        gtk_im_context_set_client_window(tc->imc, canvas->window);

        g_signal_connect(G_OBJECT(canvas), "focus_in_event", G_CALLBACK(sptc_focus_in), tc);
        g_signal_connect(G_OBJECT(canvas), "focus_out_event", G_CALLBACK(sptc_focus_out), tc);
        g_signal_connect(G_OBJECT(tc->imc), "commit", G_CALLBACK(sptc_commit), tc);

        if (GTK_WIDGET_HAS_FOCUS(canvas)) {
            sptc_focus_in(canvas, NULL, tc);
        }
    }

    if (((SPEventContextClass *) parent_class)->setup)
        ((SPEventContextClass *) parent_class)->setup(ec);

    SPItem *item = SP_DT_SELECTION(ec->desktop)->singleItem();
    if (item && SP_IS_FLOWTEXT (item) && SP_FLOWTEXT(item)->has_internal_frame()) {
        ec->shape_knot_holder = sp_item_knot_holder(item, ec->desktop);
        Inkscape::XML::Node *shape_repr = SP_OBJECT_REPR(SP_FLOWTEXT(item)->get_frame(NULL));
        if (shape_repr) {
            ec->shape_repr = shape_repr;
            sp_repr_ref(shape_repr);
            sp_repr_add_listener(shape_repr, &ec_shape_repr_events, ec);
            sp_repr_synthesize_events(shape_repr, &ec_shape_repr_events, ec);
        }
    }

    tc->sel_changed_connection = SP_DT_SELECTION(desktop)->connectChanged(
        sigc::bind(sigc::ptr_fun(&sp_text_context_selection_changed), tc)
        );
    tc->sel_modified_connection = SP_DT_SELECTION(desktop)->connectModified(
        sigc::bind(sigc::ptr_fun(&sp_text_context_selection_modified), tc)
        );
    tc->style_set_connection = desktop->connectSetStyle(
        sigc::bind(sigc::ptr_fun(&sp_text_context_style_set), tc)
        );
    tc->style_query_connection = desktop->connectQueryStyle(
        sigc::bind(sigc::ptr_fun(&sp_text_context_style_query), tc)
        );

    sp_text_context_selection_changed(SP_DT_SELECTION(desktop), tc);

    if (prefs_get_int_attribute("tools.text", "selcue", 0) != 0) {
        ec->enableSelectionCue();
    }
    if (prefs_get_int_attribute("tools.text", "gradientdrag", 0) != 0) {
        ec->enableGrDrag();
    }
}

static void
sp_text_context_finish(SPEventContext *ec)
{
    SPTextContext *tc = SP_TEXT_CONTEXT(ec);

    ec->enableGrDrag(false);

    tc->style_set_connection.disconnect();
    tc->style_query_connection.disconnect();
    tc->sel_changed_connection.disconnect();
    tc->sel_modified_connection.disconnect();

    sp_text_context_forget_text(SP_TEXT_CONTEXT(ec));

    if (tc->imc) {
        g_object_unref(G_OBJECT(tc->imc));
        tc->imc = NULL;
    }

    if (tc->timeout) {
        gtk_timeout_remove(tc->timeout);
        tc->timeout = 0;
    }

    if (tc->cursor) {
        gtk_object_destroy(GTK_OBJECT(tc->cursor));
        tc->cursor = NULL;
    }

    if (tc->indicator) {
        gtk_object_destroy(GTK_OBJECT(tc->indicator));
        tc->indicator = NULL;
    }

    if (tc->frame) {
        gtk_object_destroy(GTK_OBJECT(tc->frame));
        tc->frame = NULL;
    }

    for (std::vector<SPCanvasItem*>::iterator it = tc->text_selection_quads.begin() ;
         it != tc->text_selection_quads.end() ; ++it) {
        sp_canvas_item_hide(*it);
        gtk_object_destroy(*it);
    }
    tc->text_selection_quads.clear();

    if (ec->desktop) {
        sp_signal_disconnect_by_data(SP_DT_CANVAS(ec->desktop), tc);
    }
}


static gint
sp_text_context_item_handler(SPEventContext *ec, SPItem *item, GdkEvent *event)
{
    SPTextContext *tc = SP_TEXT_CONTEXT(ec);
    SPDesktop *desktop = ec->desktop;
    SPItem *item_ungrouped;

    gint ret = FALSE;

    sp_text_context_validate_cursor_iterators(tc);

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {
                // find out clicked item, disregarding groups
                item_ungrouped = sp_desktop_item_at_point(desktop, NR::Point(event->button.x, event->button.y), TRUE);
                if (SP_IS_TEXT(item_ungrouped) || SP_IS_FLOWTEXT(item_ungrouped)) {
                    SP_DT_SELECTION(ec->desktop)->set(item_ungrouped);
                    if (tc->text) {
                        // find out click point in document coordinates
                        NR::Point p = sp_desktop_w2d_xy_point(ec->desktop, NR::Point(event->button.x, event->button.y));
                        // set the cursor closest to that point
                        tc->text_sel_start = tc->text_sel_end = sp_te_get_position_by_coords(tc->text, p);
                        // update display
                        sp_text_context_update_cursor(tc);
                        sp_text_context_update_text_selection(tc);
                        tc->dragging = 1;
                    }
                    ret = TRUE;
                }
            }
            break;
        case GDK_2BUTTON_PRESS:
            if (event->button.button == 1 && tc->text) {
                Inkscape::Text::Layout const *layout = te_get_layout(tc->text);
                if (layout) {
                    if (!layout->isStartOfWord(tc->text_sel_start))
                        tc->text_sel_start.prevStartOfWord();
                    if (!layout->isEndOfWord(tc->text_sel_end))
                        tc->text_sel_end.nextEndOfWord();
                    sp_text_context_update_cursor(tc);
                    sp_text_context_update_text_selection(tc);
                    tc->dragging = 2;
                    ret = TRUE;
                }
            }
            break;
        case GDK_3BUTTON_PRESS:
            if (event->button.button == 1 && tc->text) {
                tc->text_sel_start.thisStartOfLine();
                tc->text_sel_end.thisEndOfLine();
                sp_text_context_update_cursor(tc);
                sp_text_context_update_text_selection(tc);
                tc->dragging = 3;
                ret = TRUE;
            }
            break;
        case GDK_BUTTON_RELEASE:
            if (event->button.button == 1 && tc->dragging) {
                tc->dragging = 0;
                ret = TRUE;
            }
            break;
        case GDK_MOTION_NOTIFY:
            if (event->motion.state & GDK_BUTTON1_MASK && tc->dragging) {
                Inkscape::Text::Layout const *layout = te_get_layout(tc->text);
                if (!layout) break;
                // find out click point in document coordinates
                NR::Point p = sp_desktop_w2d_xy_point(ec->desktop, NR::Point(event->button.x, event->button.y));
                // set the cursor closest to that point
                Inkscape::Text::Layout::iterator new_end = sp_te_get_position_by_coords(tc->text, p);
                if (tc->dragging == 2) {
                    // double-click dragging: go by word
                    if (new_end < tc->text_sel_start) {
                        if (!layout->isStartOfWord(new_end))
                            new_end.prevStartOfWord();
                    } else 
                        if (!layout->isEndOfWord(new_end))
                            new_end.nextEndOfWord();
                } else if (tc->dragging == 3) {
                    // triple-click dragging: go by line
                    if (new_end < tc->text_sel_start)
                        new_end.thisStartOfLine();
                    else 
                        new_end.thisEndOfLine();
                }
                // update display
                if (tc->text_sel_end != new_end) {
                    tc->text_sel_end = new_end;
                    sp_text_context_update_cursor(tc);
                    sp_text_context_update_text_selection(tc);
                }
                ret = TRUE;
                break;
            }
            // find out item under mouse, disregarding groups
            item_ungrouped = sp_desktop_item_at_point(desktop, NR::Point(event->button.x, event->button.y), TRUE);
            if (SP_IS_TEXT(item_ungrouped) || SP_IS_FLOWTEXT(item_ungrouped)) {
                NRRect bbox;
                sp_item_bbox_desktop(item_ungrouped, &bbox);
                sp_canvas_item_show(tc->indicator);
                sp_ctrlrect_set_area(SP_CTRLRECT(tc->indicator),
                                     bbox.x0, bbox.y0,
                                     bbox.x1, bbox.y1);

                ec->cursor_shape = cursor_text_insert_xpm;
                ec->hot_x = 7;
                ec->hot_y = 10;
                sp_event_context_update_cursor(ec);
                sp_text_context_update_text_selection(tc);

                if (SP_IS_TEXT (item_ungrouped)) {
                    desktop->event_context->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, _("<b>Click</b> to edit the text, <b>drag</b> to select part of the text."));
                } else {
                    desktop->event_context->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, _("<b>Click</b> to edit the flowed text, <b>drag</b> to select part of the text."));
                }

                tc->over_text = true;

                ret = TRUE;
            }
            break;
        default:
            break;
    }

    if (!ret) {
        if (((SPEventContextClass *) parent_class)->item_handler)
            ret = ((SPEventContextClass *) parent_class)->item_handler(ec, item, event);
    }

    return ret;
}

static void
sp_text_context_setup_text(SPTextContext *tc)
{
    SPEventContext *ec = SP_EVENT_CONTEXT(tc);

    /* Create <text> */
    Inkscape::XML::Node *rtext = sp_repr_new("svg:text");
    sp_repr_set_attr(rtext, "xml:space", "preserve"); // we preserve spaces in the text objects we create

    /* Set style */
    sp_desktop_apply_style_tool(SP_EVENT_CONTEXT_DESKTOP(ec), rtext, "tools.text", true);

    sp_repr_set_double(rtext, "x", tc->pdoc[NR::X]);
    sp_repr_set_double(rtext, "y", tc->pdoc[NR::Y]);

    /* Create <tspan> */
    Inkscape::XML::Node *rtspan = sp_repr_new("svg:tspan");
    sp_repr_set_attr(rtspan, "sodipodi:role", "line"); // otherwise, why bother creating the tspan?
    sp_repr_add_child(rtext, rtspan, NULL);
    sp_repr_unref(rtspan);

    /* Create TEXT */
    Inkscape::XML::Node *rstring = sp_repr_new_text("");
    sp_repr_add_child(rtspan, rstring, NULL);
    sp_repr_unref(rstring);
    SPItem *text_item = SP_ITEM(ec->desktop->currentLayer()->appendChildRepr(rtext));
    /* fixme: Is selection::changed really immediate? */
    /* yes, it's immediate .. why does it matter? */
    SP_DT_SELECTION(ec->desktop)->set(text_item);
    sp_repr_unref(rtext);
    text_item->transform = SP_ITEM(ec->desktop->currentRoot())->getRelativeTransform(ec->desktop->currentLayer());
    text_item->updateRepr();
    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
}

/**
 * Insert the character indicated by tc.uni to replace the current selection,
 * and reset tc.uni/tc.unipos to empty string.
 *
 * \pre tc.uni/tc.unipos non-empty.
 */
static void
insert_uni_char(SPTextContext *const tc)
{
    g_return_if_fail(tc->unipos
                     && tc->unipos < sizeof(tc->uni)
                     && tc->uni[tc->unipos] == '\0');
    unsigned int uv;
    sscanf(tc->uni, "%x", &uv);
    tc->unipos = 0;
    tc->uni[tc->unipos] = '\0';

    if (!g_unichar_isprint((gunichar) uv)) {
        // This may be due to bad input, so it goes to statusbar.
        tc->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE,
                                           _("Non-printable character"));
    } else {
        if (!tc->text) { // printable key; create text if none (i.e. if nascent_object)
            sp_text_context_setup_text(tc);
            tc->nascent_object = 0; // we don't need it anymore, having created a real <text>
        }

        gchar u[10];
        guint const len = g_unichar_to_utf8(uv, u);
        u[len] = '\0';

        tc->text_sel_start = tc->text_sel_end = sp_te_replace(tc->text, tc->text_sel_start, tc->text_sel_end, u);
        sp_text_context_update_cursor(tc);
        sp_text_context_update_text_selection(tc);
        sp_document_done(SP_DT_DOCUMENT(tc->desktop));
    }
}

static void
hex_to_printable_utf8_buf(char const *const hex, char *utf8)
{
    unsigned int uv;
    sscanf(hex, "%x", &uv);
    if (!g_unichar_isprint((gunichar) uv)) {
        uv = 0xfffd;
    }
    guint const len = g_unichar_to_utf8(uv, utf8);
    utf8[len] = '\0';
}

static void
show_curr_uni_char(SPTextContext *const tc)
{
    g_return_if_fail(tc->unipos < sizeof(tc->uni)
                     && tc->uni[tc->unipos] == '\0');
    if (tc->unipos) {
        char utf8[10];
        hex_to_printable_utf8_buf(tc->uni, utf8);

        /* Status bar messages are in pango markup, so we need xml escaping. */
        if (utf8[1] == '\0') {
            switch(utf8[0]) {
                case '<': strcpy(utf8, "&lt;"); break;
                case '>': strcpy(utf8, "&gt;"); break;
                case '&': strcpy(utf8, "&amp;"); break;
                default: break;
            }
        }
        tc->defaultMessageContext()->setF(Inkscape::NORMAL_MESSAGE,
                                          _("Unicode: %s: %s"), tc->uni, utf8);
    } else {
        tc->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, _("Unicode: "));
    }
}

static gint
sp_text_context_root_handler(SPEventContext *const ec, GdkEvent *const event)
{
    SPTextContext *const tc = SP_TEXT_CONTEXT(ec);

    SPDesktop *desktop = ec->desktop;

    sp_canvas_item_hide(tc->indicator);

    sp_text_context_validate_cursor_iterators(tc);

    ec->tolerance = prefs_get_int_attribute_limited("options.dragtolerance", "value", 0, 0, 100);

    switch (event->type) {
        case GDK_BUTTON_PRESS:
            if (event->button.button == 1) {

                SPDesktop *desktop = SP_EVENT_CONTEXT_DESKTOP(ec);
                SPItem *layer=SP_ITEM(desktop->currentLayer());
                if ( !layer || desktop->itemIsHidden(layer)) {
                    desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("<b>Current layer is hidden</b>. Unhide it to be able to add text."));
                    return TRUE;
                }
                if ( !layer || layer->isLocked()) {
                    desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("<b>Current layer is locked</b>. Unlock it to be able to add text."));
                    return TRUE;
                }

                // save drag origin
                ec->xp = (gint) event->button.x;
                ec->yp = (gint) event->button.y;
                ec->within_tolerance = true;

                NR::Point const button_pt(event->button.x, event->button.y);
                tc->p0 = NR::Point(sp_desktop_w2d_xy_point(desktop, button_pt));
                sp_rubberband_start(desktop, tc->p0);
                sp_canvas_item_grab(SP_CANVAS_ITEM(desktop->acetate),
                                    GDK_KEY_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK |
                                        GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK,
                                    NULL, event->button.time);
                tc->grabbed = SP_CANVAS_ITEM(desktop->acetate);
                tc->creating = 1;

                /* Processed */
                return TRUE;
            }
            break;
        case GDK_MOTION_NOTIFY: 
            if (tc->over_text) {
                tc->over_text = 0;
                // update cursor and statusbar: we are not over a text object now
                ec->cursor_shape = cursor_text_xpm;
                ec->hot_x = 7;
                ec->hot_y = 7;
                sp_event_context_update_cursor(ec);
                desktop->event_context->defaultMessageContext()->clear();
            }

            if (tc->creating && event->motion.state & GDK_BUTTON1_MASK) {
                if ( ec->within_tolerance
                     && ( abs( (gint) event->motion.x - ec->xp ) < ec->tolerance )
                     && ( abs( (gint) event->motion.y - ec->yp ) < ec->tolerance ) ) {
                    break; // do not drag if we're within tolerance from origin
                }
                // Once the user has moved farther than tolerance from the original location
                // (indicating they intend to draw, not click), then always process the
                // motion notify coordinates as given (no snapping back to origin)
                ec->within_tolerance = false;

                NR::Point const motion_pt(event->motion.x, event->motion.y);
                NR::Point const p(sp_desktop_w2d_xy_point(desktop, motion_pt));

                sp_rubberband_move(p);
                gobble_motion_events(GDK_BUTTON1_MASK);

                // status text
                GString *xs = SP_PX_TO_METRIC_STRING(fabs((p - tc->p0)[NR::X]), sp_desktop_get_default_metric(desktop));
                GString *ys = SP_PX_TO_METRIC_STRING(fabs((p - tc->p0)[NR::Y]), sp_desktop_get_default_metric(desktop));
                ec->_message_context->setF(Inkscape::NORMAL_MESSAGE, _("<b>Flowed text frame</b>: %s &#215; %s"), xs->str, ys->str);
                g_string_free(xs, FALSE);
                g_string_free(ys, FALSE);

            }
            break;
        case GDK_BUTTON_RELEASE: 
            if (event->button.button == 1) {

                if (tc->grabbed) {
                    sp_canvas_item_ungrab(tc->grabbed, GDK_CURRENT_TIME);
                    tc->grabbed = NULL;
                }

                NRRect b;
                if (sp_rubberband_rect(&b)) {
                    sp_rubberband_stop();
                }

                if (tc->creating && ec->within_tolerance) {
                    /* Button 1, set X & Y & new item */
                    SP_DT_SELECTION(desktop)->clear();
                    NR::Point dtp = sp_desktop_w2d_xy_point(ec->desktop, NR::Point(event->button.x, event->button.y));
                    tc->pdoc = sp_desktop_dt2root_xy_point(ec->desktop, dtp);

                    tc->show = TRUE;
                    tc->phase = 1;
                    tc->nascent_object = 1; // new object was just created

                    /* Cursor */
                    sp_canvas_item_show(tc->cursor);
                    // Cursor height is defined by the new text object's font size; it needs to be set
                    // articifically here, for the text object does not exist yet:
                    double cursor_height = sp_desktop_get_font_size_tool(ec->desktop);
                    sp_ctrlline_set_coords(SP_CTRLLINE(tc->cursor), dtp, dtp + NR::Point(0, cursor_height));
                    ec->_message_context->set(Inkscape::NORMAL_MESSAGE, _("Type text; <b>Enter</b> to start new line.")); // FIXME:: this is a copy of a string from _update_cursor below, do not desync

                    ec->within_tolerance = false;
                } else if (tc->creating) {
                    NR::Point const button_pt(event->button.x, event->button.y);
                    NR::Point p1 = NR::Point(sp_desktop_w2d_xy_point(desktop, button_pt));
                    double cursor_height = sp_desktop_get_font_size_tool(ec->desktop);
                    if (fabs(p1[NR::Y] - tc->p0[NR::Y]) > cursor_height) {
                        // otherwise even one line won't fit; most probably a slip of hand (even if bigger than tolerance)
                        SPItem *ft = create_flowtext_with_internal_frame (desktop, tc->p0, p1);
                        SP_DT_SELECTION(desktop)->set(ft);
                        ec->desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("Flowed text is created."));
                        sp_document_done(SP_DT_DOCUMENT(desktop));
                    } else {
                        ec->desktop->messageStack()->flash(Inkscape::ERROR_MESSAGE, _("The frame is <b>too small</b> for the current font size. Flowed text not created."));
                    }
                }
                tc->creating = false;
                return TRUE;
            }
            break;
        case GDK_KEY_PRESS: {
            guint const group0_keyval = get_group0_keyval(&event->key);

            if (group0_keyval == GDK_KP_Add ||
                group0_keyval == GDK_KP_Subtract) {
                if (!(event->key.state & GDK_MOD2_MASK)) // mod2 is NumLock; if on, type +/- keys
                    break; // otherwise pass on keypad +/- so they can zoom
            }

            if ((tc->text) || (tc->nascent_object)) {
                // there is an active text object in this context, or a new object was just created

                if (tc->unimode || !tc->imc
                    || (MOD__CTRL && MOD__SHIFT)    // input methods tend to steal this for unimode,
                                                    // but we have our own so make sure they don't swallow it
                    || !gtk_im_context_filter_keypress(tc->imc, (GdkEventKey*) event)) {
                    //IM did not consume the key, or we're in unimode

                        if (!MOD__CTRL_ONLY && tc->unimode) {
                            /* TODO: ISO 14755 (section 3 Definitions) says that we should also
                               accept the first 6 characters of alphabets other than the latin
                               alphabet "if the Latin alphabet is not used".  The below is also
                               reasonable (viz. hope that the user's keyboard includes latin
                               characters and force latin interpretation -- just as we do for our
                               keyboard shortcuts), but differs from the ISO 14755
                               recommendation. */
                            switch (group0_keyval) {
                                case GDK_space:
                                case GDK_KP_Space: {
                                    if (tc->unipos) {
                                        insert_uni_char(tc);
                                    }
                                    /* Stay in unimode. */
                                    show_curr_uni_char(tc);
                                    return TRUE;
                                }

                                case GDK_BackSpace: {
                                    g_return_val_if_fail(tc->unipos < sizeof(tc->uni), TRUE);
                                    if (tc->unipos) {
                                        tc->uni[--tc->unipos] = '\0';
                                    }
                                    show_curr_uni_char(tc);
                                    return TRUE;
                                }

                                case GDK_Return:
                                case GDK_KP_Enter: {
                                    if (tc->unipos) {
                                        insert_uni_char(tc);
                                    }
                                    /* Exit unimode. */
                                    tc->unimode = false;
                                    ec->defaultMessageContext()->clear();
                                    return TRUE;
                                }

                                case GDK_Escape: {
                                    // Cancel unimode.
                                    tc->unimode = false;
                                    gtk_im_context_reset(tc->imc);
                                    ec->defaultMessageContext()->clear();
                                    return TRUE;
                                }

                                case GDK_Shift_L:
                                case GDK_Shift_R:
                                    break;

                                default: {
                                    if (g_ascii_isxdigit(group0_keyval)) {
                                        g_return_val_if_fail(tc->unipos < sizeof(tc->uni) - 1, TRUE);
                                        tc->uni[tc->unipos++] = group0_keyval;
                                        tc->uni[tc->unipos] = '\0';
                                        if (tc->unipos == 8) {
                                            /* This behaviour is partly to allow us to continue to
                                               use a fixed-length buffer for tc->uni.  Reason for
                                               choosing the number 8 is that it's the length of
                                               ``canonical form'' mentioned in the ISO 14755 spec.
                                               An advantage over choosing 6 is that it allows using
                                               backspace for typos & misremembering when entering a
                                               6-digit number. */
                                            insert_uni_char(tc);
                                        }
                                        show_curr_uni_char(tc);
                                        return TRUE;
                                    } else {
                                        /* The intent is to ignore but consume characters that could be
                                           typos for hex digits.  Gtk seems to ignore & consume all
                                           non-hex-digits, and we do similar here.  Though note that some
                                           shortcuts (like keypad +/- for zoom) get processed before
                                           reaching this code. */
                                        return TRUE;
                                    }
                                }
                            }
                        }

                        bool (Inkscape::Text::Layout::iterator::*cursor_movement_operator)() = NULL;

                        /* Neither unimode nor IM consumed key; process text tool shortcuts */
                        switch (group0_keyval) {
                            case GDK_space:
                                if (MOD__CTRL_ONLY) {
                                    /* No-break space */
                                    if (!tc->text) { // printable key; create text if none (i.e. if nascent_object)
                                        sp_text_context_setup_text(tc);
                                        tc->nascent_object = 0; // we don't need it anymore, having created a real <text>
                                    }
                                    tc->text_sel_start = tc->text_sel_end = sp_te_replace(tc->text, tc->text_sel_start, tc->text_sel_end, "\302\240");
                                    sp_text_context_update_cursor(tc);
                                    sp_text_context_update_text_selection(tc);
                                    ec->desktop->messageStack()->flash(Inkscape::NORMAL_MESSAGE, _("No-break space"));
                                    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                    return TRUE;
                                }
                                break;
                            case GDK_U:
                            case GDK_u:
                                if (MOD__CTRL_ONLY) {
                                    if (tc->unimode) {
                                        tc->unimode = false;
                                        ec->defaultMessageContext()->clear();
                                    } else {
                                        tc->unimode = true;
                                        tc->unipos = 0;
                                        ec->defaultMessageContext()->set(Inkscape::NORMAL_MESSAGE, _("Unicode: "));
                                    }
                                    if (tc->imc) {
                                        gtk_im_context_reset(tc->imc);
                                    }
                                    return TRUE;
                                }
                                break;
                            case GDK_B:
                            case GDK_b:
                                if (MOD__CTRL_ONLY && tc->text) {
                                    SPStyle const *style = sp_te_style_at_position(tc->text, std::min(tc->text_sel_start, tc->text_sel_end));
                                    SPCSSAttr *css = sp_repr_css_attr_new();
                                    if (style->font_weight.computed == SP_CSS_FONT_WEIGHT_NORMAL
                                        || style->font_weight.computed == SP_CSS_FONT_WEIGHT_100
                                        || style->font_weight.computed == SP_CSS_FONT_WEIGHT_200
                                        || style->font_weight.computed == SP_CSS_FONT_WEIGHT_300
                                        || style->font_weight.computed == SP_CSS_FONT_WEIGHT_400)
                                        sp_repr_css_set_property(css, "font-weight", "bold");
                                    else
                                        sp_repr_css_set_property(css, "font-weight", "normal");
                                    sp_te_apply_style(tc->text, tc->text_sel_start, tc->text_sel_end, css);
                                    sp_repr_css_attr_unref(css);
                                    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                    sp_text_context_update_cursor(tc);
                                    sp_text_context_update_text_selection(tc);
                                    return TRUE;
                                }
                                break;
                            case GDK_I:
                            case GDK_i:
                                if (MOD__CTRL_ONLY && tc->text) {
                                    SPStyle const *style = sp_te_style_at_position(tc->text, std::min(tc->text_sel_start, tc->text_sel_end));
                                    SPCSSAttr *css = sp_repr_css_attr_new();
                                    if (style->font_style.computed == SP_CSS_FONT_STYLE_NORMAL)
                                        sp_repr_css_set_property(css, "font-style", "italic");
                                    else
                                        sp_repr_css_set_property(css, "font-style", "normal");
                                    sp_te_apply_style(tc->text, tc->text_sel_start, tc->text_sel_end, css);
                                    sp_repr_css_attr_unref(css);
                                    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                    sp_text_context_update_cursor(tc);
                                    sp_text_context_update_text_selection(tc);
                                    return TRUE;
                                }
                                break;

                            case GDK_A:
                            case GDK_a:
                                if (MOD__CTRL_ONLY && tc->text) {
                                    Inkscape::Text::Layout const *layout = te_get_layout(tc->text);
                                    if (layout) {
                                        tc->text_sel_start = layout->begin();
                                        tc->text_sel_end = layout->end();
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        return TRUE;
                                    }
                                }
                                break;

                            case GDK_Return:
                            case GDK_KP_Enter:
                                if (!tc->text) { // printable key; create text if none (i.e. if nascent_object)
                                    sp_text_context_setup_text(tc);
                                    tc->nascent_object = 0; // we don't need it anymore, having created a real <text>
                                }
                                tc->text_sel_start = tc->text_sel_end = sp_te_delete(tc->text, tc->text_sel_start, tc->text_sel_end);
                                tc->text_sel_start = tc->text_sel_end = sp_te_insert_line(tc->text, tc->text_sel_start);
                                sp_text_context_update_cursor(tc);
                                sp_text_context_update_text_selection(tc);
                                sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                return TRUE;
                            case GDK_BackSpace:
                                if (tc->text) { // if nascent_object, do nothing, but return TRUE; same for all other delete and move keys
                                    if (tc->text_sel_start == tc->text_sel_end)
                                        tc->text_sel_start.prevCursorPosition();
                                    tc->text_sel_start = tc->text_sel_end = sp_te_delete(tc->text, tc->text_sel_start, tc->text_sel_end);
                                    sp_text_context_update_cursor(tc);
                                    sp_text_context_update_text_selection(tc);
                                    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                }
                                return TRUE;
                            case GDK_Delete:
                            case GDK_KP_Delete:
                                if (tc->text) {
                                    if (tc->text_sel_start == tc->text_sel_end)
                                        tc->text_sel_end.nextCursorPosition();
                                    tc->text_sel_start = tc->text_sel_end = sp_te_delete(tc->text, tc->text_sel_start, tc->text_sel_end);
                                    sp_text_context_update_cursor(tc);
                                    sp_text_context_update_text_selection(tc);
                                    sp_document_done(SP_DT_DOCUMENT(ec->desktop));
                                }
                                return TRUE;
                            case GDK_Left:
                            case GDK_KP_Left:
                            case GDK_KP_4:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__SHIFT)
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(-10, 0));
                                        else
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(-1, 0));
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "kern:left");
                                    } else {
                                        cursor_movement_operator = MOD__CTRL ? &Inkscape::Text::Layout::iterator::cursorLeftWithControl
                                                                             : &Inkscape::Text::Layout::iterator::cursorLeft;
                                        break;
                                    }
                                }
                                return TRUE;
                            case GDK_Right:
                            case GDK_KP_Right:
                            case GDK_KP_6:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__SHIFT)
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(10, 0));
                                        else
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(1, 0));
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "kern:right");
                                    } else {
                                        cursor_movement_operator = MOD__CTRL ? &Inkscape::Text::Layout::iterator::cursorRightWithControl
                                                                             : &Inkscape::Text::Layout::iterator::cursorRight;
                                        break;
                                    }
                                }
                                return TRUE;
                            case GDK_Up:
                            case GDK_KP_Up:
                            case GDK_KP_8:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__SHIFT)
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(0, -10));
                                        else
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(0, -1));
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "kern:up");
                                    } else {
                                        cursor_movement_operator = MOD__CTRL ? &Inkscape::Text::Layout::iterator::cursorUpWithControl
                                                                             : &Inkscape::Text::Layout::iterator::cursorUp;
                                        break;
                                    }
                                }
                                return TRUE;
                            case GDK_Down:
                            case GDK_KP_Down:
                            case GDK_KP_2:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__SHIFT)
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(0, 10));
                                        else
                                            sp_te_adjust_kerning_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, NR::Point(0, 1));
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "kern:down");
                                    } else {
                                        cursor_movement_operator = MOD__CTRL ? &Inkscape::Text::Layout::iterator::cursorDownWithControl
                                                                             : &Inkscape::Text::Layout::iterator::cursorDown;
                                        break;
                                    }
                                }
                                return TRUE;
                            case GDK_Home:
                            case GDK_KP_Home:
                                if (tc->text) {
                                    if (MOD__CTRL)
                                        cursor_movement_operator = &Inkscape::Text::Layout::iterator::thisStartOfShape;
                                    else 
                                        cursor_movement_operator = &Inkscape::Text::Layout::iterator::thisStartOfLine;
                                    break;
                                }
                                return TRUE;
                            case GDK_End:
                            case GDK_KP_End:
                                if (tc->text) {
                                    if (MOD__CTRL)
                                        cursor_movement_operator = &Inkscape::Text::Layout::iterator::nextStartOfShape;
                                    else 
                                        cursor_movement_operator = &Inkscape::Text::Layout::iterator::thisEndOfLine;
                                    break;
                                }
                                return TRUE;
                            case GDK_Escape:
                                if (tc->creating) {
                                    tc->creating = 0;
                                    if (tc->grabbed) {
                                        sp_canvas_item_ungrab(tc->grabbed, GDK_CURRENT_TIME);
                                        tc->grabbed = NULL;
                                    }
                                    NRRect b;
                                    if (sp_rubberband_rect(&b)) {
                                        sp_rubberband_stop();
                                    }
                                } else {
                                    SP_DT_SELECTION(ec->desktop)->clear();
                                }
                                return TRUE;
                            case GDK_bracketleft:
                                if (tc->text) {
                                    if (MOD__ALT || MOD__CTRL) {
                                        if (MOD__ALT) {
                                            if (MOD__SHIFT) {
                                                // FIXME: alt+shift+[] does not work, don't know why
                                                sp_te_adjust_rotation_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -10);
                                            } else {
                                                sp_te_adjust_rotation_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -1);
                                            } 
                                        } else {
                                            sp_te_adjust_rotation(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -90);
                                        }
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "textrot:ccw");
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        return TRUE;
                                    } 
                                }
                                break;
                            case GDK_bracketright:
                                if (tc->text) {
                                    if (MOD__ALT || MOD__CTRL) {
                                        if (MOD__ALT) {
                                            if (MOD__SHIFT) {
                                                // FIXME: alt+shift+[] does not work, don't know why
                                                sp_te_adjust_rotation_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 10);
                                            } else {
                                                sp_te_adjust_rotation_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 1);
                                            } 
                                        } else {
                                            sp_te_adjust_rotation(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 90);
                                        }
                                        sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "textrot:cw");
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        return TRUE;
                                    }
                                }
                                break;
                            case GDK_less:
                            case GDK_comma:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__CTRL) {
                                            if (MOD__SHIFT)
                                                sp_te_adjust_linespacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -10);
                                            else
                                                sp_te_adjust_linespacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -1);
                                            sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "linespacing:dec");
                                        } else {
                                            if (MOD__SHIFT)
                                                sp_te_adjust_tspan_letterspacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -10);
                                            else
                                                sp_te_adjust_tspan_letterspacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, -1);
                                            sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "letterspacing:dec");
                                        }
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        return TRUE;
                                    }
                                }
                                break;
                            case GDK_greater:
                            case GDK_period:
                                if (tc->text) {
                                    if (MOD__ALT) {
                                        if (MOD__CTRL) {
                                            if (MOD__SHIFT)
                                                sp_te_adjust_linespacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 10);
                                            else
                                                sp_te_adjust_linespacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 1);
                                            sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "linespacing:inc");
                                        } else {
                                            if (MOD__SHIFT)
                                                sp_te_adjust_tspan_letterspacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 10);
                                            else
                                                sp_te_adjust_tspan_letterspacing_screen(tc->text, tc->text_sel_start, tc->text_sel_end, ec->desktop, 1);
                                            sp_document_maybe_done(SP_DT_DOCUMENT(ec->desktop), "letterspacing:inc");
                                        }
                                        sp_text_context_update_cursor(tc);
                                        sp_text_context_update_text_selection(tc);
                                        return TRUE;
                                    }
                                }
                                break;
                            default:
                                break;
                        }

                        if (cursor_movement_operator) {
                            Inkscape::Text::Layout::iterator old_start = tc->text_sel_start;
                            Inkscape::Text::Layout::iterator old_end = tc->text_sel_end;
                            (tc->text_sel_end.*cursor_movement_operator)();
                            if (!MOD__SHIFT)
                                tc->text_sel_start = tc->text_sel_end;
                            if (old_start != tc->text_sel_start || old_end != tc->text_sel_end) {
                                sp_text_context_update_cursor(tc);
                                sp_text_context_update_text_selection(tc);
                            }
                            return TRUE;
                        }

                } else return TRUE; // return the "I took care of it" value if it was consumed by the IM
            } else { // do nothing if there's no object to type in - the key will be sent to parent context,
                // except up/down that are swallowed to prevent the zoom field from activation
                if ((group0_keyval == GDK_Up    ||
                     group0_keyval == GDK_Down  ||
                     group0_keyval == GDK_KP_Up ||
                     group0_keyval == GDK_KP_Down )
                    && !MOD__CTRL_ONLY) {
                    return TRUE;
                } else if (group0_keyval == GDK_Escape) { // cancel rubberband
                    if (tc->creating) {
                        tc->creating = 0;
                        if (tc->grabbed) {
                            sp_canvas_item_ungrab(tc->grabbed, GDK_CURRENT_TIME);
                            tc->grabbed = NULL;
                        }
                        NRRect b;
                        if (sp_rubberband_rect(&b)) {
                            sp_rubberband_stop();
                        }
                    } 
                }
            }
            break;
        }

        case GDK_KEY_RELEASE:
            if (!tc->unimode && tc->imc && gtk_im_context_filter_keypress(tc->imc, (GdkEventKey*) event)) {
                return TRUE;
            }
            break;
        default:
            break;
    }

    // if nobody consumed it so far
    if (((SPEventContextClass *) parent_class)->root_handler) { // and there's a handler in parent context,
        return ((SPEventContextClass *) parent_class)->root_handler(ec, event); // send event to parent
    } else {
        return FALSE; // return "I did nothing" value so that global shortcuts can be activated
    }
}

/**
 Attempts to paste system clipboard into the currently edited text, returns true on success
 */
bool
sp_text_paste_inline(SPEventContext *ec)
{
    if (!SP_IS_TEXT_CONTEXT(ec))
        return false;

    SPTextContext *tc = SP_TEXT_CONTEXT(ec);

    if ((tc->text) || (tc->nascent_object)) {
        // there is an active text object in this context, or a new object was just created

        Glib::RefPtr<Gtk::Clipboard> refClipboard = Gtk::Clipboard::get();
        Glib::ustring const text = refClipboard->wait_for_text();

        if (!text.empty()) {

            if (!tc->text) { // create text if none (i.e. if nascent_object)
                sp_text_context_setup_text(tc);
                tc->nascent_object = 0; // we don't need it anymore, having created a real <text>
            }

            tc->text_sel_start = tc->text_sel_end = sp_te_replace(tc->text, tc->text_sel_start, tc->text_sel_end, text.c_str());
            sp_document_done(SP_DT_DOCUMENT(ec->desktop));

            return true;
        }
    } // FIXME: else create and select a new object under cursor!

    return false;
}

/**
 Gets the raw characters that comprise the currently selected text, converting line
 breaks into lf characters.
*/
Glib::ustring
sp_text_get_selected_text(SPEventContext const *ec)
{
    if (!SP_IS_TEXT_CONTEXT(ec))
        return "";
    SPTextContext const *tc = SP_TEXT_CONTEXT(ec);
    if (tc->text == NULL)
        return "";

    return sp_te_get_string_multiline(tc->text, tc->text_sel_start, tc->text_sel_end);
}

/**
 Deletes the currently selected characters. Returns false if there is no
 text selection currently.
*/
bool sp_text_delete_selection(SPEventContext *ec)
{
    if (!SP_IS_TEXT_CONTEXT(ec))
        return false;
    SPTextContext *tc = SP_TEXT_CONTEXT(ec);
    if (tc->text == NULL)
        return false;

    if (tc->text_sel_start == tc->text_sel_end)
        return false;
    tc->text_sel_start = tc->text_sel_end = sp_te_delete(tc->text, tc->text_sel_start, tc->text_sel_end);
    sp_text_context_update_cursor(tc);
    sp_text_context_update_text_selection(tc);
    return true;
}

/**
 * \param selection Should not be NULL.
 */
static void
sp_text_context_selection_changed(Inkscape::Selection *selection, SPTextContext *tc)
{
    g_assert(selection != NULL);

    SPEventContext *ec = SP_EVENT_CONTEXT(tc);

    if (ec->shape_knot_holder) { // destroy knotholder
        sp_knot_holder_destroy(ec->shape_knot_holder);
        ec->shape_knot_holder = NULL;
    }

    if (ec->shape_repr) { // remove old listener
        sp_repr_remove_listener_by_data(ec->shape_repr, ec);
        sp_repr_unref(ec->shape_repr);
        ec->shape_repr = 0;
    }

    SPItem *item = selection->singleItem();
    if (item && SP_IS_FLOWTEXT (item) && SP_FLOWTEXT(item)->has_internal_frame()) {
        ec->shape_knot_holder = sp_item_knot_holder(item, ec->desktop);
        Inkscape::XML::Node *shape_repr = SP_OBJECT_REPR(SP_FLOWTEXT(item)->get_frame(NULL));
        if (shape_repr) {
            ec->shape_repr = shape_repr;
            sp_repr_ref(shape_repr);
            sp_repr_add_listener(shape_repr, &ec_shape_repr_events, ec);
            sp_repr_synthesize_events(shape_repr, &ec_shape_repr_events, ec);
        }
    }

    if (tc->text && (item != tc->text)) {
        sp_text_context_forget_text(tc);
    }
    tc->text = NULL;

    if (SP_IS_TEXT(item) || SP_IS_FLOWTEXT(item)) {
        tc->text = item;
        Inkscape::Text::Layout const *layout = te_get_layout(tc->text);
        if (layout)
            tc->text_sel_start = tc->text_sel_end = layout->end();
    } else {
        tc->text = NULL;
    }

    // we update cursor without scrolling, because this position may not be final;
    // item_handler moves cusros to the point of click immediately
    sp_text_context_update_cursor(tc, false);
    sp_text_context_update_text_selection(tc);
}

static void
sp_text_context_selection_modified(Inkscape::Selection *selection, guint flags, SPTextContext *tc)
{
    sp_text_context_update_cursor(tc);
    sp_text_context_update_text_selection(tc);
}

static bool
sp_text_context_style_set(SPCSSAttr const *css, SPTextContext *tc)
{
    if (tc->text == NULL)
        return false;
    if (tc->text_sel_start == tc->text_sel_end)
        return false;    // will get picked up by the parent and applied to the whole text object

    sp_te_apply_style(tc->text, tc->text_sel_start, tc->text_sel_end, css);
    sp_document_done(SP_DT_DOCUMENT(tc->desktop));
    sp_text_context_update_cursor(tc);
    sp_text_context_update_text_selection(tc);

    return true;
}

static int
sp_text_context_style_query(SPStyle *style, int property, SPTextContext *tc)
{
    if (tc->text == NULL)
        return QUERY_STYLE_NOTHING;
    const Inkscape::Text::Layout *layout = te_get_layout(tc->text);
    if (layout == NULL)
        return QUERY_STYLE_NOTHING;
    sp_text_context_validate_cursor_iterators(tc);

    GSList *styles_list = NULL;
    int result = QUERY_STYLE_NOTHING;

    Inkscape::Text::Layout::iterator begin_it, end_it;
    if (tc->text_sel_start < tc->text_sel_end) {
        begin_it = tc->text_sel_start;
        end_it = tc->text_sel_end;
    } else {
        begin_it = tc->text_sel_end;
        end_it = tc->text_sel_start;
    }
    if (begin_it == end_it)
        if (!begin_it.prevCharacter())
            end_it.nextCharacter();
    for (Inkscape::Text::Layout::iterator it = begin_it ; it < end_it ; it.nextStartOfSpan()) {
        SPObject const *pos_obj = NULL;
        layout->getSourceOfCharacter(it, (void**)&pos_obj);
        if (pos_obj == NULL) continue;
        while (SP_OBJECT_STYLE(pos_obj) == NULL && SP_OBJECT_PARENT(pos_obj))
            pos_obj = SP_OBJECT_PARENT(pos_obj);   // SPStrings don't have style
        styles_list = g_slist_prepend(styles_list, (gpointer)pos_obj);
    }

    if (property == QUERY_STYLE_PROPERTY_FONTFAMILY)
        result = objects_query_fontfamily(styles_list, style);
    else if (property == QUERY_STYLE_PROPERTY_FONTSTYLE)
        result = objects_query_fontstyle(styles_list, style);
    else if (property == QUERY_STYLE_PROPERTY_FONTNUMBERS)
        result = objects_query_fontnumbers(styles_list, style);
    else if (property == QUERY_STYLE_PROPERTY_FILL)
        result = objects_query_fillstroke(styles_list, style, true);
    else if (property == QUERY_STYLE_PROPERTY_STROKE)
        result = objects_query_fillstroke(styles_list, style, false);

    g_slist_free(styles_list);
    return result;
}

static void
sp_text_context_validate_cursor_iterators(SPTextContext *tc)
{
    if (tc->text == NULL)
        return;
    Inkscape::Text::Layout const *layout = te_get_layout(tc->text);
    if (layout) {     // undo can change the text length without us knowing it
        layout->validateIterator(&tc->text_sel_start);
        layout->validateIterator(&tc->text_sel_end);
    }
}

static void
sp_text_context_update_cursor(SPTextContext *tc,  bool scroll_to_see)
{
    GdkRectangle im_cursor = { 0, 0, 1, 1 };

    if (tc->text) {
        NR::Point p0, p1;
        sp_te_get_cursor_coords(tc->text, tc->text_sel_end, p0, p1);
        NR::Point const d0 = p0 * sp_item_i2d_affine(SP_ITEM(tc->text));
        NR::Point const d1 = p1 * sp_item_i2d_affine(SP_ITEM(tc->text));

        // scroll to show cursor
        if (scroll_to_see) {
            NR::Point const dm = (d0 + d1) / 2;
            // unlike mouse moves, here we must scroll all the way at first shot, so we override the autoscrollspeed
            sp_desktop_scroll_to_point(SP_EVENT_CONTEXT(tc)->desktop, &dm, 1.0);
        }

        sp_canvas_item_show(tc->cursor);
        sp_ctrlline_set_coords(SP_CTRLLINE(tc->cursor), d0, d1);

        /* fixme: ... need another transformation to get canvas widget coordinate space? */
        im_cursor.x = (int) floor(d0[NR::X]);
        im_cursor.y = (int) floor(d0[NR::Y]);
        im_cursor.width = (int) floor(d1[NR::X]) - im_cursor.x;
        im_cursor.height = (int) floor(d1[NR::Y]) - im_cursor.y;

        tc->show = TRUE;
        tc->phase = 1;

        if (SP_IS_FLOWTEXT(tc->text)) {
            SPItem *frame = SP_FLOWTEXT(tc->text)->get_frame (NULL); // first frame only
            if (frame) {
                NRRect bbox;
                sp_item_bbox_desktop(frame, &bbox);
                sp_canvas_item_show(tc->frame);
                sp_ctrlrect_set_area(SP_CTRLRECT(tc->frame), bbox.x0, bbox.y0, bbox.x1, bbox.y1);
            }
            SP_EVENT_CONTEXT(tc)->_message_context->set(Inkscape::NORMAL_MESSAGE, _("Type flowed text; <b>Enter</b> to start new paragraph."));
        } else {
            SP_EVENT_CONTEXT(tc)->_message_context->set(Inkscape::NORMAL_MESSAGE, _("Type text; <b>Enter</b> to start new line."));
        }

    } else {
        sp_canvas_item_hide(tc->cursor);
        sp_canvas_item_hide(tc->frame);
        tc->show = FALSE;
        if (!tc->nascent_object) {
            SP_EVENT_CONTEXT(tc)->_message_context->set(Inkscape::NORMAL_MESSAGE, _("<b>Click</b> to select or create text, <b>drag</b> to create flowed text; then type.")); // FIXME: this is a copy of string from tools-switch, do not desync
        }
    }

    if (tc->imc) {
        gtk_im_context_set_cursor_location(tc->imc, &im_cursor);
    }
    SP_EVENT_CONTEXT(tc)->desktop->emitToolSubselectionChanged((gpointer)tc);
}

static void sp_text_context_update_text_selection(SPTextContext *tc)
{
    for (std::vector<SPCanvasItem*>::iterator it = tc->text_selection_quads.begin() ; it != tc->text_selection_quads.end() ; it++) {
        sp_canvas_item_hide(*it);
        gtk_object_destroy(*it);
    }
    tc->text_selection_quads.clear();

    std::vector<NR::Point> quads;
    if (tc->text != NULL)
        quads = sp_te_create_selection_quads(tc->text, tc->text_sel_start, tc->text_sel_end, sp_item_i2d_affine(tc->text));
    for (unsigned i = 0 ; i < quads.size() ; i += 4) {
        SPCanvasItem *quad_canvasitem;
        quad_canvasitem = sp_canvas_item_new(SP_DT_CONTROLS(tc->desktop), SP_TYPE_CTRLQUADR, NULL);
        sp_ctrlquadr_set_rgba32(SP_CTRLQUADR(quad_canvasitem), 0x000000ff);
        sp_ctrlquadr_set_coords(SP_CTRLQUADR(quad_canvasitem), quads[i], quads[i+1], quads[i+2], quads[i+3]);
        sp_canvas_item_show(quad_canvasitem);
        tc->text_selection_quads.push_back(quad_canvasitem);
    }
}

static gint
sp_text_context_timeout(SPTextContext *tc)
{
    if (tc->show) {
        if (tc->phase) {
            tc->phase = 0;
            sp_canvas_item_hide(tc->cursor);
        } else {
            tc->phase = 1;
            sp_canvas_item_show(tc->cursor);
        }
    }

    return TRUE;
}

static void
sp_text_context_forget_text(SPTextContext *tc)
{
    if (! tc->text) return;
    SPItem *ti = tc->text;
    /* We have to set it to zero,
     * or selection changed signal messes everything up */
    tc->text = NULL;
    if ((SP_IS_TEXT(ti) || SP_IS_FLOWTEXT(ti)) && sp_te_input_is_empty(ti)) {
        Inkscape::XML::Node *text_repr=SP_OBJECT_REPR(ti);
        // the repr may already have been unparented
        // if we were called e.g. as the result of
        // an undo or the element being removed from
        // the XML editor
        if ( text_repr && sp_repr_parent(text_repr) ) {
            sp_repr_unparent(text_repr);
        }
    }
}

gint
sptc_focus_in(GtkWidget *widget, GdkEventFocus *event, SPTextContext *tc)
{
    gtk_im_context_focus_in(tc->imc);
    return FALSE;
}

gint
sptc_focus_out(GtkWidget *widget, GdkEventFocus *event, SPTextContext *tc)
{
    gtk_im_context_focus_out(tc->imc);
    return FALSE;
}

static void
sptc_commit(GtkIMContext *imc, gchar *string, SPTextContext *tc)
{
    if (!tc->text) {
        sp_text_context_setup_text(tc);
        tc->nascent_object = 0; // we don't need it anymore, having created a real <text>
    }

    tc->text_sel_start = tc->text_sel_end = sp_te_replace(tc->text, tc->text_sel_start, tc->text_sel_end, string);
    sp_text_context_update_cursor(tc);
    sp_text_context_update_text_selection(tc);

    sp_document_done(SP_OBJECT_DOCUMENT(tc->text));
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
