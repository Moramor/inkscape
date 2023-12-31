// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * @file
 * Drag and drop of drawings onto canvas.
 */

/* Authors:
 *
 * Copyright (C) Tavmjong Bah 2019
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "drag-and-drop.h"

#include <array>
#include <glibmm/i18n.h>  // Internationalization

#include "desktop-style.h"
#include "document.h"
#include "document-undo.h"
#include "gradient-drag.h"
#include "file.h"
#include "selection.h"
#include "style.h"
#include "layer-manager.h"

#include "extension/db.h"
#include "extension/find_extension_by_mime.h"

#include "object/sp-shape.h"
#include "object/sp-text.h"
#include "object/sp-flowtext.h"

#include "path/path-util.h"

#include "svg/svg-color.h" // write color

#include "ui/clipboard.h"
#include "ui/interface.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"  // Target, canvas to world transform.

#include "widgets/desktop-widget.h"
#include "widgets/paintdef.h"

using Inkscape::DocumentUndo;

/* Drag and Drop */
enum ui_drop_target_info {
    URI_LIST,
    SVG_XML_DATA,
    SVG_DATA,
    PNG_DATA,
    JPEG_DATA,
    IMAGE_DATA,
    APP_X_INKY_COLOR,
    APP_X_COLOR,
    APP_OSWB_COLOR,
    APP_X_INK_PASTE
};

static const std::array<Gtk::TargetEntry, 8> ui_drop_target_entries = {
    Gtk::TargetEntry("text/uri-list",                Gtk::TargetFlags(0), URI_LIST       ),
    Gtk::TargetEntry("image/svg+xml",                Gtk::TargetFlags(0), SVG_XML_DATA   ),
    Gtk::TargetEntry("image/svg",                    Gtk::TargetFlags(0), SVG_DATA       ),
    Gtk::TargetEntry("image/png",                    Gtk::TargetFlags(0), PNG_DATA       ),
    Gtk::TargetEntry("image/jpeg",                   Gtk::TargetFlags(0), JPEG_DATA      ),
    Gtk::TargetEntry("application/x-oswb-color",     Gtk::TargetFlags(0), APP_OSWB_COLOR ),
    Gtk::TargetEntry("application/x-color",          Gtk::TargetFlags(0), APP_X_COLOR    ),
    Gtk::TargetEntry("application/x-inkscape-paste", Gtk::TargetFlags(0), APP_X_INK_PASTE)
};

static std::vector<Gtk::TargetEntry> completeDropTargets;

/** Convert screen (x, y) coordinates to desktop coordinates. */
inline Geom::Point world2desktop(SPDesktop *desktop, int x, int y)
{
    g_assert(desktop);
    return (Geom::Point(x, y) + desktop->canvas->get_area_world().min()) * desktop->w2d();
}

// Drag and Drop
static void ink_drag_data_received(GtkWidget *widget,
                         GdkDragContext *drag_context,
                         gint x, gint y,
                         GtkSelectionData *data,
                         guint info,
                         guint /*event_time*/,
                         gpointer user_data)
{
    auto dtw = static_cast<SPDesktopWidget *>(user_data);
    SPDesktop *desktop = dtw->desktop;
    SPDocument *doc = desktop->doc();

    switch (info) {
        case APP_X_COLOR:
        {
            int destX = 0;
            int destY = 0;
            auto canvas = dtw->get_canvas();
            gtk_widget_translate_coordinates( widget, GTK_WIDGET(canvas->gobj()), x, y, &destX, &destY );
            Geom::Point where( canvas->canvas_to_world(Geom::Point(destX, destY)));
            Geom::Point const button_dt(desktop->w2d(where));
            Geom::Point const button_doc(desktop->dt2doc(button_dt));

            if ( gtk_selection_data_get_length (data) == 8 ) {
                gchar colorspec[64] = {0};
                // Careful about endian issues.
                guint16* dataVals = (guint16*)gtk_selection_data_get_data (data);
                sp_svg_write_color( colorspec, sizeof(colorspec),
                                    SP_RGBA32_U_COMPOSE(
                                        0x0ff & (dataVals[0] >> 8),
                                        0x0ff & (dataVals[1] >> 8),
                                        0x0ff & (dataVals[2] >> 8),
                                        0xff // can't have transparency in the color itself
                                        //0x0ff & (data->data[3] >> 8),
                                        ));

                SPItem *item = desktop->getItemAtPoint( where, true );

                bool consumed = false;
                if (desktop->event_context && desktop->event_context->get_drag()) {
                    consumed = desktop->event_context->get_drag()->dropColor(item, colorspec, button_dt);
                    if (consumed) {
                        DocumentUndo::done( doc , _("Drop color on gradient"), "" );
                        desktop->event_context->get_drag()->updateDraggers();
                    }
                }

                //if (!consumed && tools_active(desktop, TOOLS_TEXT)) {
                //    consumed = sp_text_context_drop_color(c, button_doc);
                //    if (consumed) {
                //        SPDocumentUndo::done( doc , _("Drop color on gradient stop"), "");
                //    }
                //}

                if (!consumed && item) {
                    bool fillnotstroke = (gdk_drag_context_get_actions (drag_context) != GDK_ACTION_MOVE);
                    if (fillnotstroke &&
                        (is<SPShape>(item) || is<SPText>(item) || is<SPFlowtext>(item))) {
                        if (auto livarot_path = Path_for_item(item, true, true)) {
                            livarot_path->ConvertWithBackData(0.04);

                            if (auto position = get_nearest_position_on_Path(livarot_path.get(), button_doc)) {
                                Geom::Point nearest = get_point_on_Path(livarot_path.get(), position->piece, position->t);
                                Geom::Point delta = nearest - button_doc;
                                Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                                delta = desktop->d2w(delta);
                                double stroke_tolerance =
                                    (!item->style->stroke.isNone() ?
                                     desktop->current_zoom() *
                                     item->style->stroke_width.computed *
                                     item->i2dt_affine().descrim() * 0.5
                                     : 0.0)
                                    + prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

                                if (Geom::L2 (delta) < stroke_tolerance) {
                                    fillnotstroke = false;
                                }
                            }
                        }

                        SPCSSAttr *css = sp_repr_css_attr_new();
                        sp_repr_css_set_property(css, fillnotstroke ? "fill" : "stroke", colorspec);

                        sp_desktop_apply_css_recursive(item, css, true);
                        item->updateRepr();

                        DocumentUndo::done(doc ,  _("Drop color"), "");
                    }
                }
            }
        }
        break;

        case APP_OSWB_COLOR:
        {
            bool worked = false;
            Glib::ustring colorspec;
            if ( gtk_selection_data_get_format (data) == 8 ) {
                PaintDef color;
                worked = color.fromMIMEData("application/x-oswb-color",
                                            reinterpret_cast<char const*>(gtk_selection_data_get_data(data)),
                                            gtk_selection_data_get_length(data));
                if ( worked ) {
                    if ( color.get_type() == PaintDef::NONE ) {
                        colorspec = "none";
                    } else {
                        auto [r, g, b] = color.get_rgb();

                        SPGradient* matches = nullptr;
                        std::vector<SPObject *> gradients = doc->getResourceList("gradient");
                        for (auto gradient : gradients) {
                            auto grad = cast<SPGradient>(gradient);
                            if (color.get_description() == grad->getId()) {
                                if (grad->hasStops()) {
                                    matches = grad;
                                    break;
                                }
                            }
                        }
                        if (matches) {
                            colorspec = "url(#";
                            colorspec += matches->getId();
                            colorspec += ")";
                        } else {
                            gchar* tmp = g_strdup_printf("#%02x%02x%02x", r, g, b);
                            colorspec = tmp;
                            g_free(tmp);
                        }
                    }
                }
            }
            if ( worked ) {
                int destX = 0;
                int destY = 0;
                auto canvas = dtw->get_canvas();
                gtk_widget_translate_coordinates( widget, GTK_WIDGET(canvas->gobj()), x, y, &destX, &destY );
                Geom::Point where( canvas->canvas_to_world(Geom::Point(destX, destY)));
                Geom::Point const button_dt(desktop->w2d(where));
                Geom::Point const button_doc(desktop->dt2doc(button_dt));

                SPItem *item = desktop->getItemAtPoint( where, true );

                bool consumed = false;
                if (desktop->event_context && desktop->event_context->get_drag()) {
                    consumed = desktop->event_context->get_drag()->dropColor(item, colorspec.c_str(), button_dt);
                    if (consumed) {
                        DocumentUndo::done( doc, _("Drop color on gradient"), "" );
                        desktop->event_context->get_drag()->updateDraggers();
                    }
                }

                if (!consumed && item) {
                    bool fillnotstroke = (gdk_drag_context_get_actions (drag_context) != GDK_ACTION_MOVE);
                    if (fillnotstroke &&
                        (is<SPShape>(item) || is<SPText>(item) || is<SPFlowtext>(item))) {
                        auto livarot_path = Path_for_item(item, true, true);
                        livarot_path->ConvertWithBackData(0.04);

                        std::optional<Path::cut_position> position = get_nearest_position_on_Path(livarot_path.get(), button_doc);
                        if (position) {
                            Geom::Point nearest = get_point_on_Path(livarot_path.get(), position->piece, position->t);
                            Geom::Point delta = nearest - button_doc;
                            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                            delta = desktop->d2w(delta);
                            double stroke_tolerance =
                                ( !item->style->stroke.isNone() ?
                                  desktop->current_zoom() *
                                  item->style->stroke_width.computed *
                                  item->i2dt_affine().descrim() * 0.5
                                  : 0.0)
                                + prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);

                            if (Geom::L2 (delta) < stroke_tolerance) {
                                fillnotstroke = false;
                            }
                        }
                    }

                    SPCSSAttr *css = sp_repr_css_attr_new();
                    sp_repr_css_set_property( css, fillnotstroke ? "fill":"stroke", colorspec.c_str() );

                    sp_desktop_apply_css_recursive( item, css, true );
                    item->updateRepr();

                    DocumentUndo::done( doc, _("Drop color"), "" );
                }
            }
        }
        break;

        case SVG_DATA:
        case SVG_XML_DATA: {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            prefs->setBool("/options/onimport", true);
            gchar *svgdata = (gchar *)gtk_selection_data_get_data (data);

            Inkscape::XML::Document *rnewdoc = sp_repr_read_mem(svgdata, gtk_selection_data_get_length (data), SP_SVG_NS_URI);

            if (rnewdoc == nullptr) {
                sp_ui_error_dialog(_("Could not parse SVG data"));
                return;
            }

            Inkscape::XML::Node *repr = rnewdoc->root();
            gchar const *style = repr->attribute("style");


            Inkscape::XML::Document * xml_doc =  doc->getReprDoc();
            Inkscape::XML::Node *newgroup = xml_doc->createElement("svg:g");
            newgroup->setAttribute("style", style);
            for (Inkscape::XML::Node *child = repr->firstChild(); child != nullptr; child = child->next()) {
                Inkscape::XML::Node *newchild = child->duplicate(xml_doc);
                newgroup->appendChild(newchild);
            }

            Inkscape::GC::release(rnewdoc);

            // Add it to the current layer

            // Greg's edits to add intelligent positioning of svg drops
            SPObject *new_obj = nullptr;
            new_obj = desktop->layerManager().currentLayer()->appendChildRepr(newgroup);

            Inkscape::Selection *selection = desktop->getSelection();
            selection->set(cast<SPItem>(new_obj));

            // move to mouse pointer
            {
                desktop->getDocument()->ensureUpToDate();
                Geom::OptRect sel_bbox = selection->visualBounds();
                if (sel_bbox) {
                    Geom::Point m( desktop->point() - sel_bbox->midpoint() );
                    selection->moveRelative(m, false);
                }
            }

            Inkscape::GC::release(newgroup);
            DocumentUndo::done( doc, _("Drop SVG"), "" );
            prefs->setBool("/options/onimport", false);
            break;
        }

        case URI_LIST: {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            prefs->setBool("/options/onimport", true);
            gchar *uri = (gchar *)gtk_selection_data_get_data (data);
            sp_ui_import_files(uri);
            prefs->setBool("/options/onimport", false);
            break;
        }

        case APP_X_INK_PASTE: {
            auto *cm = Inkscape::UI::ClipboardManager::get();
            cm->insertSymbol(desktop, world2desktop(desktop, x, y));
            DocumentUndo::done(doc, _("Drop Symbol"), "");
            break;
        }

        case PNG_DATA:
        case JPEG_DATA:
        case IMAGE_DATA: {
            Inkscape::Extension::Extension *ext = Inkscape::Extension::find_by_mime((info == JPEG_DATA ? "image/jpeg" : "image/png"));
            bool save = (strcmp(ext->get_param_optiongroup("link"), "embed") == 0);
            ext->set_param_optiongroup("link", "embed");
            ext->set_gui(false);

            gchar *filename = g_build_filename( g_get_tmp_dir(), "inkscape-dnd-import", nullptr );
            g_file_set_contents(filename,
                reinterpret_cast<gchar const *>(gtk_selection_data_get_data (data)),
                gtk_selection_data_get_length (data),
                nullptr);
            file_import(doc, filename, ext);
            g_free(filename);

            ext->set_param_optiongroup("link", save ? "embed" : "link");
            ext->set_gui(true);
            DocumentUndo::done( doc, _("Drop bitmap image"), "" );
            break;
        }
    }
}

#if 0
static
void ink_drag_motion( GtkWidget */*widget*/,
                        GdkDragContext */*drag_context*/,
                        gint /*x*/, gint /*y*/,
                        GtkSelectionData */*data*/,
                        guint /*info*/,
                        guint /*event_time*/,
                        gpointer /*user_data*/)
{
//     SPDocument *doc = SP_ACTIVE_DOCUMENT;
//     SPDesktop *desktop = SP_ACTIVE_DESKTOP;


//     g_message("drag-n-drop motion (%4d, %4d)  at %d", x, y, event_time);
}

static void ink_drag_leave( GtkWidget */*widget*/,
                              GdkDragContext */*drag_context*/,
                              guint /*event_time*/,
                              gpointer /*user_data*/ )
{
//     g_message("drag-n-drop leave                at %d", event_time);
}
#endif

void ink_drag_setup(SPDesktopWidget *dtw)
{
    if (completeDropTargets.empty()) {
        for (auto const &entry : ui_drop_target_entries) {
            completeDropTargets.emplace_back(entry);
        }
        for (auto const &fmt : Gdk::Pixbuf::get_formats()) {
            for (auto &type : fmt.get_mime_types()) {
                completeDropTargets.emplace_back(std::move(type), Gtk::TargetFlags(0), IMAGE_DATA);
            }
        }
    }

    auto canvas = dtw->get_canvas();

    canvas->drag_dest_set(completeDropTargets,
                          Gtk::DestDefaults::DEST_DEFAULT_ALL,
                          Gdk::DragAction::ACTION_COPY | Gdk::DragAction::ACTION_MOVE);

    g_signal_connect(G_OBJECT(canvas->gobj()),
                     "drag_data_received",
                     G_CALLBACK(ink_drag_data_received),
                     dtw);
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
