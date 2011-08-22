/** @file
 * @brief Document properties dialog, Gtkmm-style
 */
/* Authors:
 *   bulia byak <buliabyak@users.sf.net>
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   Jon Phillips <jon@rejon.org>
 *   Ralf Stephan <ralf@ark.in-berlin.de> (Gtkmm)
 *   Diederik van Lierop <mail@diedenrezi.nl>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006-2008 Johan Engelen  <johan@shouraizou.nl>
 * Copyright (C) 2000 - 2008 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "display/canvas-grid.h"
#include "document-properties.h"
#include "document.h"
#include "desktop-handles.h"
#include "desktop.h"
#include <gtkmm.h>
#include "helper/units.h"
#include "inkscape.h"
#include "io/sys.h"
#include "preferences.h"
#include "sp-namedview.h"
#include "sp-object-repr.h"
#include "sp-root.h"
#include "sp-script.h"
#include "ui/widget/color-picker.h"
#include "ui/widget/scalar-unit.h"
#include "verbs.h"
#include "widgets/icon.h"
#include "xml/node-event-vector.h"
#include "xml/repr.h"
#include "select-context.h"
#include "../../widgets/sp-attribute-widget.h"
#include <fstream>
#include <string>

#if ENABLE_LCMS
#include "color-profile.h"
#endif // ENABLE_LCMS

using std::pair;

namespace Inkscape {
namespace UI {
namespace Dialog {

#define SPACE_SIZE_X 15
#define SPACE_SIZE_Y 10


//===================================================

//---------------------------------------------------

static void on_child_added(Inkscape::XML::Node *repr, Inkscape::XML::Node *child, Inkscape::XML::Node *ref, void * data);
static void on_child_removed(Inkscape::XML::Node *repr, Inkscape::XML::Node *child, Inkscape::XML::Node *ref, void * data);
static void on_repr_attr_changed (Inkscape::XML::Node *, gchar const *, gchar const *, gchar const *, bool, gpointer);

static Inkscape::XML::NodeEventVector const _repr_events = {
    on_child_added, // child_added
    on_child_removed, // child_removed
    on_repr_attr_changed,
    NULL, // content_changed
    NULL  // order_changed
};

const gchar* int_labels[10] = {"onclick", "onmouseover", "onmouseout", "onmousedown", "onmouseup", "onmousemove","onfocusin", "onfocusout", "onactivate", "onload"};

DocumentProperties &
DocumentProperties::getInstance()
{
    DocumentProperties &instance = *new DocumentProperties();
    instance.init();

    return instance;
}

DocumentProperties::DocumentProperties()
    : UI::Widget::Panel ("", "/dialogs/documentoptions", SP_VERB_DIALOG_NAMEDVIEW),
      _page_page(1, 1, true, true), _page_guides(1, 1),
      _page_snap(1, 1), _page_cms(1, 1), _page_scripting(1, 1),
      _page_external_scripts(1, 1, true, true), _page_embedded_scripts(1, 1, true, true),
      _page_object_list(1, 1, true, true), _page_global_events(1, 1),
      _page_embed_unembed_scripts(1, 1, true, true),
    //---------------------------------------------------------------
      _rcb_canb(_("Show page _border"), _("If set, rectangular page border is shown"), "showborder", _wr, false),
      _rcb_bord(_("Border on _top of drawing"), _("If set, border is always on top of the drawing"), "borderlayer", _wr, false),
      _rcb_shad(_("_Show border shadow"), _("If set, page border shows a shadow on its right and lower side"), "inkscape:showpageshadow", _wr, false),
      _rcp_bg(_("Back_ground:"), _("Background color"), _("Color and transparency of the page background (also used for bitmap export)"), "pagecolor", "inkscape:pageopacity", _wr),
      _rcp_bord(_("Border _color:"), _("Page border color"), _("Color of the page border"), "bordercolor", "borderopacity", _wr),
      _rum_deflt(_("Default _units:"), "inkscape:document-units", _wr),
      _page_sizer(_wr),
    //---------------------------------------------------------------
      //General snap options
      _rcb_sgui(_("Show _guides"), _("Show or hide guides"), "showguides", _wr),
      _rcbsng(_("_Snap guides while dragging"), _("While dragging a guide, snap to object nodes or bounding box corners ('Snap to nodes' or 'snap to bounding box corners' must be enabled; only a small part of the guide near the cursor will snap)"),
                  "inkscape:snap-from-guide", _wr),
      _rcp_gui(_("Guide co_lor:"), _("Guideline color"), _("Color of guidelines"), "guidecolor", "guideopacity", _wr),
      _rcp_hgui(_("_Highlight color:"), _("Highlighted guideline color"), _("Color of a guideline when it is under mouse"), "guidehicolor", "guidehiopacity", _wr),
    //---------------------------------------------------------------
      _grids_label_crea("", Gtk::ALIGN_LEFT),
      _grids_button_new(C_("Grid", "_New"), _("Create new grid.")),
      _grids_button_remove(C_("Grid", "_Remove"), _("Remove selected grid.")),
      _grids_label_def("", Gtk::ALIGN_LEFT)
    //---------------------------------------------------------------
{
    _tt.enable();
    _getContents()->set_spacing (4);
    _getContents()->pack_start(_notebook, true, true);

    _notebook.append_page(_page_page,      _("Page"));
    _notebook.append_page(_page_guides,    _("Guides"));
    _notebook.append_page(_grids_vbox,     _("Grids"));
    _notebook.append_page(_page_snap,      _("Snap"));
    _notebook.append_page(_page_cms, _("Color Management"));
    _notebook.append_page(_page_scripting, _("Scripting"));

    build_page();
    build_guides();
    build_gridspage();
    build_snap();
#if ENABLE_LCMS
    build_cms();
#endif // ENABLE_LCMS
    build_scripting();

    _grids_button_new.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::onNewGrid));
    _grids_button_remove.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::onRemoveGrid));

    signalDocumentReplaced().connect(sigc::mem_fun(*this, &DocumentProperties::_handleDocumentReplaced));
    signalActivateDesktop().connect(sigc::mem_fun(*this, &DocumentProperties::_handleActivateDesktop));
    signalDeactiveDesktop().connect(sigc::mem_fun(*this, &DocumentProperties::_handleDeactivateDesktop));
}

void
DocumentProperties::init()
{
    update();

    Inkscape::XML::Node *repr = sp_desktop_namedview(getDesktop())->getRepr();
    repr->addListener (&_repr_events, this);
    Inkscape::XML::Node *root = sp_desktop_document(getDesktop())->getRoot()->getRepr();
    root->addListener (&_repr_events, this);

    show_all_children();
    _grids_button_remove.hide();
}

DocumentProperties::~DocumentProperties()
{
    Inkscape::XML::Node *repr = sp_desktop_namedview(getDesktop())->getRepr();
    repr->removeListenerByData (this);
    Inkscape::XML::Node *root = sp_desktop_document(getDesktop())->getRoot()->getRepr();
    root->removeListenerByData (this);
}

//========================================================================

/**
 * Helper function that attaches widgets in a 3xn table. The widgets come in an
 * array that has two entries per table row. The two entries code for four
 * possible cases: (0,0) means insert space in first column; (0, non-0) means
 * widget in columns 2-3; (non-0, 0) means label in columns 1-3; and
 * (non-0, non-0) means two widgets in columns 2 and 3.
**/
inline void
attach_all(Gtk::Table &table, Gtk::Widget *const arr[], unsigned const n, int start = 0)
{
    for (unsigned i = 0, r = start; i < n; i += 2)
    {
        if (arr[i] && arr[i+1])
        {
            table.attach(*arr[i],   1, 2, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
            table.attach(*arr[i+1], 2, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
        }
        else
        {
            if (arr[i+1]) {
                Gtk::AttachOptions yoptions = (Gtk::AttachOptions)0;
                if (dynamic_cast<Inkscape::UI::Widget::PageSizer*>(arr[i+1])) {
                    // only the PageSizer in Document Properties|Page should be stretched vertically
                    yoptions = Gtk::FILL|Gtk::EXPAND;
                }
                table.attach(*arr[i+1], 1, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, yoptions, 0,0);
            }
            else if (arr[i])
            {
                Gtk::Label& label = reinterpret_cast<Gtk::Label&>(*arr[i]);
                label.set_alignment (0.0);
                table.attach (label, 0, 3, r, r+1,
                      Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0,0,0);
            }
            else
            {
                Gtk::HBox *space = manage (new Gtk::HBox);
                space->set_size_request (SPACE_SIZE_X, SPACE_SIZE_Y);
                table.attach (*space, 0, 1, r, r+1,
                      (Gtk::AttachOptions)0, (Gtk::AttachOptions)0,0,0);
            }
        }
        ++r;
    }
}

void
DocumentProperties::build_page()
{
    _page_page.show();

    Gtk::Label* label_gen = manage (new Gtk::Label);
    label_gen->set_markup (_("<b>General</b>"));
    Gtk::Label* label_bor = manage (new Gtk::Label);
    label_bor->set_markup (_("<b>Border</b>"));
    Gtk::Label *label_for = manage (new Gtk::Label);
    label_for->set_markup (_("<b>Page Size</b>"));
    _page_sizer.init();

    Gtk::Widget *const widget_array[] =
    {
        label_gen,         0,
        0,                 &_rum_deflt,
        _rcp_bg._label,    &_rcp_bg,
        0,                 0,
        label_for,         0,
        0,                 &_page_sizer,
        0,                 0,
        label_bor,         0,
        0,                 &_rcb_canb,
        0,                 &_rcb_bord,
        0,                 &_rcb_shad,
        _rcp_bord._label,  &_rcp_bord,
    };

    attach_all(_page_page.table(), widget_array, G_N_ELEMENTS(widget_array));
}

void
DocumentProperties::build_guides()
{
    _page_guides.show();

    Gtk::Label *label_gui = manage (new Gtk::Label);
    label_gui->set_markup (_("<b>Guides</b>"));

    Gtk::Widget *const widget_array[] =
    {
        label_gui,        0,
        0,                &_rcb_sgui,
        _rcp_gui._label,  &_rcp_gui,
        _rcp_hgui._label, &_rcp_hgui,
        0,                &_rcbsng,
    };

    attach_all(_page_guides.table(), widget_array, G_N_ELEMENTS(widget_array));
}

void
DocumentProperties::build_snap()
{
    _page_snap.show();

    _rsu_sno.init (_("Snap _distance"), _("Snap only when _closer than:"), _("Always snap"),
                  _("Snapping distance, in screen pixels, for snapping to objects"), _("Always snap to objects, regardless of their distance"),
                  _("If set, objects only snap to another object when it's within the range specified below"),
                  "objecttolerance", _wr);

    //Options for snapping to grids
    _rsu_sn.init (_("Snap d_istance"), _("Snap only when c_loser than:"), _("Always snap"),
                  _("Snapping distance, in screen pixels, for snapping to grid"), _("Always snap to grids, regardless of the distance"),
                  _("If set, objects only snap to a grid line when it's within the range specified below"),
                  "gridtolerance", _wr);

    //Options for snapping to guides
    _rsu_gusn.init (_("Snap dist_ance"), _("Snap only when close_r than:"), _("Always snap"),
                _("Snapping distance, in screen pixels, for snapping to guides"), _("Always snap to guides, regardless of the distance"),
                _("If set, objects only snap to a guide when it's within the range specified below"),
                "guidetolerance", _wr);

    Gtk::Label *label_o = manage (new Gtk::Label);
    label_o->set_markup (_("<b>Snap to objects</b>"));
    Gtk::Label *label_gr = manage (new Gtk::Label);
    label_gr->set_markup (_("<b>Snap to grids</b>"));
    Gtk::Label *label_gu = manage (new Gtk::Label);
    label_gu->set_markup (_("<b>Snap to guides</b>"));

    Gtk::Widget *const array[] =
    {
        label_o,            0,
        0,                  _rsu_sno._vbox,
        0,                  0,
        label_gr,           0,
        0,                  _rsu_sn._vbox,
        0,                  0,
        label_gu,           0,
        0,                  _rsu_gusn._vbox
    };

    attach_all(_page_snap.table(), array, G_N_ELEMENTS(array));
 }

#if ENABLE_LCMS
void DocumentProperties::populate_available_profiles(){
    Glib::ListHandle<Gtk::Widget*> children = _menu.get_children();
    for ( Glib::ListHandle<Gtk::Widget*>::iterator it2 = children.begin(); it2 != children.end(); ++it2 ) {
        _menu.remove(**it2);
        delete(*it2);
    }

    std::vector<std::pair<Glib::ustring, Glib::ustring> > pairs = ColorProfile::getProfileFilesWithNames();
    for ( std::vector<std::pair<Glib::ustring, Glib::ustring> >::const_iterator it = pairs.begin(); it != pairs.end(); ++it ) {
        Glib::ustring file = it->first;
        Glib::ustring name = it->second;

        Gtk::MenuItem* mi = manage(new Gtk::MenuItem());
        mi->set_data("filepath", g_strdup(file.c_str()));
        mi->set_data("name", g_strdup(name.c_str()));
        Gtk::HBox *hbox = manage(new Gtk::HBox());
        hbox->show();
        Gtk::Label* lbl = manage(new Gtk::Label(name));
        lbl->show();
        hbox->pack_start(*lbl, true, true, 0);
        mi->add(*hbox);
        mi->show_all();
        _menu.append(*mi);
    }

    _menu.show_all();
}

/**
 * Cleans up name to remove disallowed characters.
 * Some discussion at http://markmail.org/message/bhfvdfptt25kgtmj
 * Allowed ASCII first characters:  ':', 'A'-'Z', '_', 'a'-'z'
 * Allowed ASCII remaining chars add: '-', '.', '0'-'9', 
 *
 * @param str the string to clean up.
 */
static void sanitizeName( Glib::ustring& str )
{
    if (str.size() > 1) {
        char val = str.at(0);
        if (((val < 'A') || (val > 'Z'))
            && ((val < 'a') || (val > 'z'))
            && (val != '_')
            && (val != ':')) {
            str.replace(0, 1, "-");
        }
        for (Glib::ustring::size_type i = 1; i < str.size(); i++) {
            char val = str.at(i);
            if (((val < 'A') || (val > 'Z'))
                && ((val < 'a') || (val > 'z'))
                && ((val < '0') || (val > '9'))
                && (val != '_')
                && (val != ':')
                && (val != '-')
                && (val != '.')) {
                str.replace(i, 1, "-");
            }
        }
    }
}

void
DocumentProperties::linkSelectedProfile()
{
//store this profile in the SVG document (create <color-profile> element in the XML)
    // TODO remove use of 'active' desktop
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop){
        g_warning("No active desktop");
    } else {
        if (!_menu.get_active()){
            g_warning("No color profile available.");
            return;
        }
        Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
        Inkscape::XML::Node *cprofRepr = xml_doc->createElement("svg:color-profile");
        gchar* tmp = static_cast<gchar*>(_menu.get_active()->get_data("name"));
        Glib::ustring nameStr = tmp ? tmp : "profile"; // TODO add some auto-numbering to avoid collisions
        sanitizeName(nameStr);
        cprofRepr->setAttribute("name", nameStr.c_str());
        cprofRepr->setAttribute("xlink:href", (gchar*) _menu.get_active()->get_data("filepath"));

        // Checks whether there is a defs element. Creates it when needed
        Inkscape::XML::Node *defsRepr = sp_repr_lookup_name(xml_doc, "svg:defs");
        if (!defsRepr){
            defsRepr = xml_doc->createElement("svg:defs");
            xml_doc->root()->addChild(defsRepr, NULL);
        }

        g_assert(desktop->doc()->getDefs());
        defsRepr->addChild(cprofRepr, NULL);

        // TODO check if this next line was sometimes needed. It being there caused an assertion.
        //Inkscape::GC::release(defsRepr);

        // inform the document, so we can undo
        DocumentUndo::done(desktop->doc(), SP_VERB_EDIT_LINK_COLOR_PROFILE, _("Link Color Profile"));

        populate_linked_profiles_box();
    }
}

void
DocumentProperties::populate_linked_profiles_box()
{
    _LinkedProfilesListStore->clear();
    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "iccprofile" );
    if (current) {
        _emb_profiles_observer.set(SP_OBJECT(current->data)->parent);
    }
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
        Gtk::TreeModel::Row row = *(_LinkedProfilesListStore->append());
        row[_LinkedProfilesListColumns.nameColumn] = prof->name;
//        row[_LinkedProfilesListColumns.previewColumn] = "Color Preview";
        current = g_slist_next(current);
    }
}

void DocumentProperties::external_scripts_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _ExternalScriptsContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::embedded_scripts_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _EmbeddedScriptsContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::auto_unembed_scripts_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _AutoUnembedScriptsContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::auto_embed_scripts_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _AutoEmbedScriptsContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::linked_profiles_list_button_release(GdkEventButton* event)
{
    if((event->type == GDK_BUTTON_RELEASE) && (event->button == 3)) {
        _EmbProfContextMenu.popup(event->button, event->time);
    }
}

void DocumentProperties::cms_create_popup_menu(Gtk::Widget& parent, sigc::slot<void> rem)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::REMOVE));
    _EmbProfContextMenu.append(*mi);
    mi->signal_activate().connect(rem);
    mi->show();
    _EmbProfContextMenu.accelerate(parent);
}


void DocumentProperties::external_create_popup_menu(Gtk::Widget& parent, sigc::slot<void> rem)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::REMOVE));
    _ExternalScriptsContextMenu.append(*mi);
    mi->signal_activate().connect(rem);
    mi->show();
    _ExternalScriptsContextMenu.accelerate(parent);
}

void DocumentProperties::embedded_create_popup_menu(Gtk::Widget& parent, sigc::slot<void> rem)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::REMOVE));
    _EmbeddedScriptsContextMenu.append(*mi);
    mi->signal_activate().connect(rem);
    mi->show();
    _EmbeddedScriptsContextMenu.accelerate(parent);
}

void DocumentProperties::auto_unembed_create_popup_menu(Gtk::Widget& parent, sigc::slot<void> ren)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::EDIT));
    mi->set_label(_("Rename"));
    _AutoUnembedScriptsContextMenu.append(*mi);
    mi->signal_activate().connect(ren);
    mi->show();
    _AutoUnembedScriptsContextMenu.accelerate(parent);
}

void DocumentProperties::auto_embed_create_popup_menu(Gtk::Widget& parent, sigc::slot<void> ren)
{
    Gtk::MenuItem* mi = Gtk::manage(new Gtk::ImageMenuItem(Gtk::Stock::EDIT));
    mi->set_label(_("Change"));
    _AutoEmbedScriptsContextMenu.append(*mi);
    mi->signal_activate().connect(ren);
    mi->show();
    _AutoEmbedScriptsContextMenu.accelerate(parent);
}

void DocumentProperties::removeSelectedProfile(){
    Glib::ustring name;
    if(_LinkedProfilesList.get_selection()) {
        Gtk::TreeModel::iterator i = _LinkedProfilesList.get_selection()->get_selected();

        if(i){
            name = (*i)[_LinkedProfilesListColumns.nameColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "iccprofile" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        Inkscape::ColorProfile* prof = reinterpret_cast<Inkscape::ColorProfile*>(obj);
        if (!name.compare(prof->name)){

            //XML Tree being used directly here while it shouldn't be.
            sp_repr_unparent(obj->getRepr());
            DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_REMOVE_COLOR_PROFILE, _("Remove linked color profile"));
        }
        current = g_slist_next(current);
    }

    populate_linked_profiles_box();
}

void
DocumentProperties::build_cms()
{
    _page_cms.show();

    Gtk::Label *label_link= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_link->set_markup (_("<b>Linked Color Profiles:</b>"));
    Gtk::Label *label_avail = manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_avail->set_markup (_("<b>Available Color Profiles:</b>"));

    _link_btn.set_label(_("Link Profile"));

    _page_cms.set_spacing(4);
    gint row = 0;

    label_link->set_alignment(0.0);
    _page_cms.table().attach(*label_link, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
    _page_cms.table().attach(_LinkedProfilesListScroller, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    Gtk::HBox* spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);
    _page_cms.table().attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    label_avail->set_alignment(0.0);
    _page_cms.table().attach(*label_avail, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
    _page_cms.table().attach(_combo_avail, 0, 2, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    _page_cms.table().attach(_link_btn, 2, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    populate_available_profiles();

    _combo_avail.set_menu(_menu);
    _combo_avail.set_history(0);
    _combo_avail.show_all();

    //# Set up the Linked Profiles combo box
    _LinkedProfilesListStore = Gtk::ListStore::create(_LinkedProfilesListColumns);
    _LinkedProfilesList.set_model(_LinkedProfilesListStore);
    _LinkedProfilesList.append_column(_("Profile Name"), _LinkedProfilesListColumns.nameColumn);
//    _LinkedProfilesList.append_column(_("Color Preview"), _LinkedProfilesListColumns.previewColumn);
    _LinkedProfilesList.set_headers_visible(false);
// TODO restore?    _LinkedProfilesList.set_fixed_height_mode(true);

    populate_linked_profiles_box();

    _LinkedProfilesListScroller.add(_LinkedProfilesList);
    _LinkedProfilesListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _LinkedProfilesListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _LinkedProfilesListScroller.set_size_request(-1, 90);

    _link_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::linkSelectedProfile));

    _LinkedProfilesList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::linked_profiles_list_button_release));
    cms_create_popup_menu(_LinkedProfilesList, sigc::mem_fun(*this, &DocumentProperties::removeSelectedProfile));

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "defs" );
    if (current) {
        _emb_profiles_observer.set(SP_OBJECT(current->data)->parent);
    }
    _emb_profiles_observer.signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::populate_linked_profiles_box));
}
#endif // ENABLE_LCMS

void
DocumentProperties::build_scripting()
{
    _page_scripting.show();

    _page_scripting.set_spacing (4);
    _page_scripting.pack_start(_scripting_notebook, true, true);
    _scripting_notebook.set_scrollable(true);

    _scripting_notebook.append_page(_page_embed_unembed_scripts, _("Embed/unembed scripts"));
    _scripting_notebook.append_page(_page_external_scripts, _("External scripts"));
    _scripting_notebook.append_page(_page_embedded_scripts, _("Embedded scripts"));
    _scripting_notebook.append_page(_page_object_list, _("Objects with script events"));
    _scripting_notebook.append_page(_page_global_events, _("Global events"));

    //# External scripts tab
    _page_external_scripts.show();
    
    _external_paned.pack1(_external_table1);
    _external_paned.pack2(_external_table2);
    _external_paned.set_position(60);

    _page_external_scripts.table().attach(_external_paned, 0, 1, 0, 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    Gtk::Label *label_external= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_external->set_markup (_("<b>External script files:</b>"));

    _add_btn.set_label(_("Add"));
    _file_btn.set_label(_("..."));

    _page_external_scripts.set_spacing(4);
    gint row = 0;

    label_external->set_alignment(0.0);
    _external_table1.attach(*label_external, 0, 4, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
    _external_table1.attach(_ExternalScriptsListScroller, 0, 4, row, row + 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);
    row++;

    Gtk::HBox* spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);
    _external_table1.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    _external_table1.attach(_script_entry, 0, 2, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    _external_table1.attach(_file_btn, 2, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    _external_table1.attach(_add_btn, 3, 4, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _external_table1.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    //# Set up the External Scripts box
    _ExternalScriptsListStore = Gtk::ListStore::create(_ExternalScriptsListColumns);
    _ExternalScriptsList.set_model(_ExternalScriptsListStore);
    _ExternalScriptsList.append_column(_("Filename"), _ExternalScriptsListColumns.filenameColumn);
    _ExternalScriptsList.set_headers_visible(true);
// TODO restore?    _ExternalScriptsList.set_fixed_height_mode(true);

    //# Set up the External Scripts content box
    row = 0;
    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _external_table2.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    Gtk::Label *label_external_content= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_external_content->set_markup (_("<b>Content:</b>"));

    label_external_content->set_alignment(0.0);
    _external_table2.attach(*label_external_content, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    _external_table2.attach(_ExternalContentScroller, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    _ExternalContentScroller.add(_ExternalContent);
    _ExternalContentScroller.set_shadow_type(Gtk::SHADOW_IN);
    _ExternalContentScroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    _ExternalContentScroller.set_size_request(-1, -1);

    _ExternalScriptsList.signal_cursor_changed().connect(sigc::mem_fun(*this, &DocumentProperties::changeExternalScript));
    _ExternalContent.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::editExternalScript));
    _ExternalContent.set_sensitive(false);


    //# Embedded scripts tab
    _page_embedded_scripts.show();

    _embedded_paned.pack1(_embedded_table1);
    _embedded_paned.pack2(_embedded_table2);
    _embedded_paned.set_position(60);

    _page_embedded_scripts.table().attach(_embedded_paned, 0, 1, 0, 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    Gtk::Label *label_embedded= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_embedded->set_markup (_("<b>Embedded script files:</b>"));

    _new_btn.set_label(_("New"));

    _page_embedded_scripts.set_spacing(4);
    row = 0;

    label_embedded->set_alignment(0.0);
    _embedded_table1.attach(*label_embedded, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
    _embedded_table1.attach(_EmbeddedScriptsListScroller, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);
    row++;

    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);
    _embedded_table1.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    _embedded_table1.attach(_new_btn, 2, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _embedded_table1.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    //# Set up the Embedded Scripts box
    _EmbeddedScriptsListStore = Gtk::ListStore::create(_EmbeddedScriptsListColumns);
    _EmbeddedScriptsList.set_model(_EmbeddedScriptsListStore);
    _EmbeddedScriptsList.append_column(_("Script id"), _EmbeddedScriptsListColumns.idColumn);
    _EmbeddedScriptsList.set_headers_visible(true);
// TODO restore?    _EmbeddedScriptsList.set_fixed_height_mode(true);

    //# Set up the Embedded Scripts content box
    row = 0;
    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _embedded_table2.attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    Gtk::Label *label_embedded_content= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_embedded_content->set_markup (_("<b>Content:</b>"));

    label_embedded_content->set_alignment(0.0);
    _embedded_table2.attach(*label_embedded_content, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    _embedded_table2.attach(_EmbeddedContentScroller, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    _EmbeddedContentScroller.add(_EmbeddedContent);
    _EmbeddedContentScroller.set_shadow_type(Gtk::SHADOW_IN);
    _EmbeddedContentScroller.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    _EmbeddedContentScroller.set_size_request(-1, -1);

    _EmbeddedScriptsList.signal_cursor_changed().connect(sigc::mem_fun(*this, &DocumentProperties::changeEmbeddedScript));
    _EmbeddedContent.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::editEmbeddedScript));
    _EmbeddedContent.set_sensitive(false);


    //# Objects with script events tab
    _page_object_list.show();

    _page_object_list.set_spacing(4);
    row = 0;

    Gtk::Label *label_object= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_object->set_markup (_("<b>Objects with script events:</b>"));
    label_object->set_alignment(0.0);
    _page_object_list.table().attach(*label_object, 0, 4, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    //# Set up the Object Scripts box
    _page_object_list.table().attach(_ObjectScriptsListScroller, 0, 3, row, row + 2, Gtk::SHRINK|Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    _ObjectScriptsListStore = Gtk::ListStore::create(_ObjectScriptsListColumns);
    _ObjectScriptsList.set_model(_ObjectScriptsListStore);
    _ObjectScriptsList.append_column(_("Object id"), _ObjectScriptsListColumns.idColumn);
    _ObjectScriptsList.set_headers_visible(true);

    _ObjectScriptsList.signal_cursor_changed().connect(sigc::mem_fun(*this, &DocumentProperties::changeObjectScript));


    //# Display the events
    _object_events_container = gtk_table_new (1, 1, TRUE);
    _page_object_list.table().attach(*Glib::wrap(_object_events_container), 3, 4, row, row + 1, (Gtk::AttachOptions)0, (Gtk::AttachOptions)0, 0, 0);
    _object_events = NULL;
    changeObjectScript();
    row+=2;

    // Instructions
    Gtk::Label *label_object_instr= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_object_instr->set_markup (_("To edit these events:\nright-click the object > Object Properties > Interactivity"));
    label_object_instr->set_alignment(0.0);
    _page_object_list.table().attach(*label_object_instr, 0, 4, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);


    //# Global events tab
    _page_global_events.show();

    _page_global_events.set_spacing(4);
    row = 0;

    Gtk::Label *label_global= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_global->set_markup (_("<b>Global events:</b>"));
    label_global->set_alignment(0.0);
    _page_global_events.table().attach(*label_global, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    Gtk::Label *label_global_desc= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_global_desc->set_line_wrap();
    label_global_desc->set_markup (_("This interface adds script events that aren't attached to any specific object, but only to the document itself. They are added to its SVG tag."));
    label_global_desc->set_alignment(0.0);
    _page_global_events.table().attach(*label_global_desc, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y);
    _page_global_events.table().attach(*spacer, 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    //# Events list
    _global_events_container = gtk_table_new (1, 1, TRUE);
    _page_global_events.table().attach(*Glib::wrap(_global_events_container), 0, 3, row, row + 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    SPObject *obj = SP_OBJECT(SP_ACTIVE_DOCUMENT->getRoot());
    _global_events = sp_attribute_table_new (obj, 10, int_labels, int_labels, true);
    gtk_container_add (GTK_CONTAINER (_global_events_container), _global_events);



    //# Embed/unembed scripts tab
    _page_embed_unembed_scripts.show();

    _embed_unembed_paned.pack1(_embed_unembed_table1);
    _embed_unembed_paned.pack2(_embed_unembed_table2);
    _page_embed_unembed_scripts.set_spacing(4);
    row = 0;
    

    _page_embed_unembed_scripts.table().attach(_embed_unembed_paned, 0, 1, 0, 1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);

    Gtk::Label *label_enbed_unembed= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_enbed_unembed->set_markup (_("<b>Enbed/unembed Scripts:</b>"));
    label_enbed_unembed->set_alignment(0.0);
    _embed_unembed_table1.attach(*label_enbed_unembed, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
/*
    Gtk::Label *label_enbed_unembed_desc= manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label_enbed_unembed_desc->set_line_wrap();
    label_enbed_unembed_desc->set_markup (_("This interface lets you embed or unembed scripts.\n\nIf it is an embedded script, a file will be created on the SVG document folder, using the script id as the file name.\nIf it is an external script, its content will be copied to an embedded script, using the file name as the script id."));
    label_enbed_unembed_desc->set_alignment(0.0);
    _embed_unembed_table1.attach(*label_enbed_unembed_desc, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;
*/

    _embed_unembed_table1.attach(_AutoUnembedScriptsListScroller, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);
    row++;

    _unembed_btn.set_label(_("Save to an external file"));
    _embed_unembed_table1.attach(_unembed_btn, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _embed_unembed_table1.attach(*spacer, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    row=0;
    spacer = Gtk::manage(new Gtk::HBox());
    spacer->set_size_request(SPACE_SIZE_X, SPACE_SIZE_Y/2);
    _embed_unembed_table2.attach(*spacer, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);
    row++;

    _embed_unembed_table2.attach(_AutoEmbedScriptsListScroller, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, Gtk::FILL|Gtk::EXPAND, 0, 0);
    row++;

    _embed_btn.set_label(_("Embed"));
    _embed_unembed_table2.attach(_embed_btn, 0, 1, row, row+1, Gtk::FILL|Gtk::EXPAND, (Gtk::AttachOptions)0, 0, 0);

    //# Set up the Embedded Scripts box
    _AutoUnembedScriptsListStore = Gtk::ListStore::create(_AutoUnembedScriptsListColumns);
    _AutoUnembedScriptsList.set_model(_EmbeddedScriptsListStore);
    _AutoUnembedScriptsList.append_column(_("Script id"), _AutoUnembedScriptsListColumns.idColumn);
    _AutoUnembedScriptsList.set_headers_visible(true);

    //# Set up the External Scripts box
    _AutoEmbedScriptsListStore = Gtk::ListStore::create(_ExternalScriptsListColumns);
    _AutoEmbedScriptsList.set_model(_ExternalScriptsListStore);
    _AutoEmbedScriptsList.append_column(_("Filename"), _AutoEmbedScriptsListColumns.filenameColumn);
    _AutoEmbedScriptsList.set_headers_visible(true);


    // Must be done after we have the lists, but before we add them
    populate_script_lists();
    populate_object_list();

    _ExternalScriptsListScroller.add(_ExternalScriptsList);
    _ExternalScriptsListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _ExternalScriptsListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _ExternalScriptsListScroller.set_size_request(-1, 90);

    _add_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::addExternalScript));
    _file_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::selectExternalScript));

    _EmbeddedScriptsListScroller.add(_EmbeddedScriptsList);
    _EmbeddedScriptsListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _EmbeddedScriptsListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    //_EmbeddedScriptsListScroller.set_size_request(-1, 90);

    _new_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::addEmbeddedScript));

    _ObjectScriptsListScroller.add(_ObjectScriptsList);
    _ObjectScriptsListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _ObjectScriptsListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _ObjectScriptsListScroller.set_size_request(-1, 90);

    _AutoUnembedScriptsListScroller.add(_AutoUnembedScriptsList);
    _AutoUnembedScriptsListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _AutoUnembedScriptsListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _unembed_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::unembedScript));
    _AutoEmbedScriptsListScroller.add(_AutoEmbedScriptsList);
    _AutoEmbedScriptsListScroller.set_shadow_type(Gtk::SHADOW_IN);
    _AutoEmbedScriptsListScroller.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_ALWAYS);
    _embed_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::embedScript));

#if ENABLE_LCMS
    _ExternalScriptsList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::external_scripts_list_button_release));
    external_create_popup_menu(_ExternalScriptsList, sigc::mem_fun(*this, &DocumentProperties::removeExternalScript));

    _EmbeddedScriptsList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::embedded_scripts_list_button_release));
    embedded_create_popup_menu(_EmbeddedScriptsList, sigc::mem_fun(*this, &DocumentProperties::removeEmbeddedScript));

    _AutoUnembedScriptsList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::auto_unembed_scripts_list_button_release));
    auto_unembed_create_popup_menu(_AutoUnembedScriptsList, sigc::mem_fun(*this, &DocumentProperties::renameEmbeddedScript));

    _AutoEmbedScriptsList.signal_button_release_event().connect_notify(sigc::mem_fun(*this, &DocumentProperties::auto_embed_scripts_list_button_release));
    auto_embed_create_popup_menu(_AutoEmbedScriptsList, sigc::mem_fun(*this, &DocumentProperties::renameExternalScript));
#endif // ENABLE_LCMS

//TODO: review this observers code:
    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    if (current) {
        _scripts_observer.set(SP_OBJECT(current->data)->parent);
    }
    _scripts_observer.signal_changed().connect(sigc::mem_fun(*this, &DocumentProperties::populate_script_lists));
}

void DocumentProperties::selectExternalScript(){
    Gtk::FileChooserDialog dialog("Please choose a javascript file", Gtk::FILE_CHOOSER_ACTION_OPEN);
    if(SP_ACTIVE_DOCUMENT->getBase())
        dialog.set_current_folder(SP_ACTIVE_DOCUMENT->getBase());
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.add_button(_("Select"), Gtk::RESPONSE_OK);

    int result = dialog.run();
    if(result == Gtk::RESPONSE_OK) {
        int page = _scripting_notebook.get_current_page();
        if(_scripting_notebook.get_nth_page(page) == (Gtk::Widget*) &_page_embed_unembed_scripts)
            _href_entry.set_text( dialog.get_filename() );
        else
            _script_entry.set_text( dialog.get_filename() );
    }
}

void DocumentProperties::addExternalScript(){
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop){
        g_warning("No active desktop");
    } else {
        if (!_script_entry.get_text().empty()) {
            Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
            Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");
            scriptRepr->setAttribute("xlink:href", (gchar*) _script_entry.get_text().c_str());
            _script_entry.set_text("");

            xml_doc->root()->addChild(scriptRepr, NULL);

            // inform the document, so we can undo
            DocumentUndo::done(desktop->doc(), SP_VERB_EDIT_ADD_EXTERNAL_SCRIPT, _("Add external script..."));

            populate_script_lists();
        }
    }
}

void DocumentProperties::addEmbeddedScript(){
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop){
        g_warning("No active desktop");
    } else {
        Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
        Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");

        xml_doc->root()->addChild(scriptRepr, NULL);

        // inform the document, so we can undo
        DocumentUndo::done(desktop->doc(), SP_VERB_EDIT_ADD_EMBEDDED_SCRIPT, _("Add embedded script..."));

        populate_script_lists();
    }
}

void DocumentProperties::removeExternalScript(){
    Glib::ustring name;
    if(_ExternalScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _ExternalScriptsList.get_selection()->get_selected();

        if(i){
            name = (*i)[_ExternalScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;
        if (name == script->xlinkhref && script->xlinkhref){
            //XML Tree being used directly here while it shouldn't be.
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                sp_repr_unparent(repr);

                // inform the document, so we can undo
                DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_REMOVE_EXTERNAL_SCRIPT, _("Remove external script"));
            }
        }
        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::removeEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        if (id == obj->getId()){

            //XML Tree being used directly here while it shouldn't be.
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                sp_repr_unparent(repr);

                // inform the document, so we can undo
                DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_REMOVE_EMBEDDED_SCRIPT, _("Remove embedded script"));
            }
        }
        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::renameEmbeddedScript(){
    Gtk::Window window;
    Gtk::Entry id_entry;
    Gtk::Dialog dialog(_("Rename"), window);
    Gtk::Label *label = manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label->set_markup(_("Please insert the new Id:"));

    dialog.get_vbox()->pack_start(*label);
    dialog.get_vbox()->pack_start(id_entry);
    dialog.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.show_all_children();
    // Enter = OK
    dialog.set_default_response(Gtk::RESPONSE_OK);
    id_entry.set_activates_default();

    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _AutoUnembedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_AutoUnembedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }
    id_entry.set_text(id);

    int btn_press;
    while(1) {
        btn_press = dialog.run();
        if ( btn_press != Gtk::RESPONSE_OK || id_entry.get_text().empty() || id_entry.get_text() == id )
            return;

        if (SP_ACTIVE_DOCUMENT->getObjectById(id_entry.get_text().c_str()) != NULL) {
            Gtk::Window window;
            Gtk::MessageDialog dialog(window, _("Error"));
            dialog.set_secondary_text(_("There is already a script with this Id."));
            dialog.run();
        } else {
            break;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        if (id == obj->getId()){

            //XML Tree being used directly here while it shouldn't be.
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                repr->setAttribute("id", id_entry.get_text().c_str());

                // inform the document, so we can undo
                DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_RENAME_EMBEDDED_SCRIPT, _("Rename embedded script"));
            }
        }
        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::renameExternalScript(){
    Gtk::Window window;
    Gtk::Dialog dialog(_("Change"), window);
    Gtk::Label *label = manage (new Gtk::Label("", Gtk::ALIGN_LEFT));
    label->set_markup(_("Please insert the new link:"));
    Gtk::HBox form;
    Gtk::Button file_btn;

    dialog.get_vbox()->pack_start(*label);
    form.pack_start(_href_entry);
    file_btn.set_label(_("..."));
    file_btn.signal_clicked().connect(sigc::mem_fun(*this, &DocumentProperties::selectExternalScript));
    form.pack_start(file_btn);
    dialog.get_vbox()->pack_start(form);
    dialog.add_button(Gtk::Stock::OK, Gtk::RESPONSE_OK);
    dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    dialog.show_all_children();
    // Enter = OK
    dialog.set_default_response(Gtk::RESPONSE_OK);
    _href_entry.set_activates_default();

    Glib::ustring href;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _AutoEmbedScriptsList.get_selection()->get_selected();

        if(i){
            href = (*i)[_AutoEmbedScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }
    _href_entry.set_text(href);

    int btn_press = dialog.run();
    if ( btn_press != Gtk::RESPONSE_OK || _href_entry.get_text().empty() || _href_entry.get_text() == href )
        return;

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;
        int count=0;
        for ( SPObject *child = obj->children ; child; child = child->next ) {
            count++;
        }
        if (count>1)
            g_warning("TODO: Found a script element with multiple (%d) child nodes! We must implement support for that!", count);

        if (script->xlinkhref && script->xlinkhref == href) {
            obj->getRepr()->setAttribute("xlink:href", _href_entry.get_text().c_str());

            // inform the document, so we can undo
            DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_RENAME_EXTERNAL_SCRIPT, _("Rename external script"));
        }

        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::changeEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    bool voidscript=true;
    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        if (id == obj->getId()){

            int count=0;
            for ( SPObject *child = obj->children ; child; child = child->next )
            {
                count++;
            }

            if (count>1)
                g_warning("TODO: Found a script element with multiple (%d) child nodes! We must implement support for that!", count);

            //XML Tree being used directly here while it shouldn't be.
            SPObject* child = obj->firstChild();
            //TODO: shouldnt we get all children instead of simply the first child?

            if (child && child->getRepr()){
                const gchar* content = child->getRepr()->content();
                if (content){
                    voidscript=false;
                    _EmbeddedContent.get_buffer()->set_text(content);
                }
            }
        }
        current = g_slist_next(current);
    }

    if (voidscript)
        _EmbeddedContent.get_buffer()->set_text("");

    _EmbeddedContent.set_sensitive(true);
}

void DocumentProperties::changeExternalScript(){
    Glib::ustring href;
    if(_ExternalScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _ExternalScriptsList.get_selection()->get_selected();

        if(i){
            href = (*i)[_ExternalScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;

        if (script->xlinkhref && script->xlinkhref == href) {
            const gchar* address[2];
            std::string text;
            address[0] = script->xlinkhref;
            // Relative path
            address[1] = g_strconcat(SP_ACTIVE_DOCUMENT->getBase(), "/", script->xlinkhref, NULL);
            for (int i=0; i<2; i++) {
                std::ifstream in(address[i]);
                text.assign( (std::istreambuf_iterator<char>(in)), (std::istreambuf_iterator<char>()) );
                in.close();
                if (!text.empty()) break;
            }
            if (!text.empty()) {
                _ExternalContent.get_buffer()->set_text( text.c_str() );
                _ExternalContent.set_sensitive(true);
            } else {
                _ExternalContent.get_buffer()->set_text(_("Could not open the file"));
                _ExternalContent.set_sensitive(false);
                
            }
        }
        current = g_slist_next(current);
    }
}

void DocumentProperties::editEmbeddedScript(){
    Glib::ustring id;
    if(_EmbeddedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _EmbeddedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_EmbeddedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    Inkscape::XML::Document *xml_doc = SP_ACTIVE_DOCUMENT->getReprDoc();
    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        if (id == obj->getId()){

            //XML Tree being used directly here while it shouldn't be.
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                SPObject *child;
                while (NULL != (child = obj->firstChild())) child->deleteObject();
                obj->appendChildRepr(xml_doc->createTextNode(_EmbeddedContent.get_buffer()->get_text().c_str()));

                //TODO repr->set_content(_EmbeddedContent.get_buffer()->get_text());

                // inform the document, so we can undo
                DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_EMBEDDED_SCRIPT, _("Edit embedded script"));
            }
        }
        current = g_slist_next(current);
    }
}

void DocumentProperties::editExternalScript(){
    Glib::ustring href;
    if(_ExternalScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _ExternalScriptsList.get_selection()->get_selected();

        if(i){
            href = (*i)[_ExternalScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;

        if (script->xlinkhref && script->xlinkhref == href) {
            if (!_ExternalContent.get_buffer()->get_text().empty()) {
                std::ofstream out( script->xlinkhref );
                out << _ExternalContent.get_buffer()->get_text();
                out.close();
            }
        }
        current = g_slist_next(current);
    }
}

void DocumentProperties::changeObjectScript(){
    Glib::ustring id;
    if(_ObjectScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _ObjectScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_ObjectScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    SPObject *obj = SP_OBJECT(SP_ACTIVE_DOCUMENT->getRoot());
    changeObjectScriptAux(obj, id);
}

void DocumentProperties::changeObjectScriptAux(SPObject *obj, Glib::ustring id){
    if (obj == 0) return;
    //XML Tree being used directly here while it shouldn't be.
    Inkscape::XML::Node *repr = obj->getRepr();
    if (repr == 0) return;

    if (id == obj->getId() && obj->getId()){
        Inkscape::Selection *selection = sp_desktop_selection(SP_ACTIVE_DESKTOP);
        selection->set(obj);
        // Display its events
        if (_object_events){
            gtk_container_remove(GTK_CONTAINER(_object_events_container), _object_events);
        }
        _object_events = sp_attribute_table_new (obj, 10, int_labels, int_labels, true);
        gtk_widget_show_all (_object_events);

        gtk_container_add (GTK_CONTAINER (_object_events_container), _object_events);
        gtk_widget_set_sensitive(GTK_WIDGET(_object_events_container), FALSE);
    } else {
        SPObject *child = obj->children;
        for (; child; child = child->next) {
            changeObjectScriptAux(child, id);
        }
    }
}

void DocumentProperties::embedScript(){
    // Get the script link
    Glib::ustring name;
    if(_AutoEmbedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _AutoEmbedScriptsList.get_selection()->get_selected();

        if(i){
            name = (*i)[_AutoEmbedScriptsListColumns.filenameColumn];
        } else {
            return;
        }
    }
    
    // Generate the id of the new embedded script
    Glib::ustring id = name;
    Glib::ustring base = SP_ACTIVE_DOCUMENT->getBase();
    if ( !id.find(base) )
        id.erase(0, base.size()+1);
    if (SP_ACTIVE_DOCUMENT->getObjectById(id.c_str()) != NULL) {
        Gtk::Window window;
        Gtk::MessageDialog dialog(window, _("Error"));
        dialog.set_secondary_text(_("There is already a script with this Id."));
        dialog.run();
        return;
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;
        if (name == script->xlinkhref && script->xlinkhref){
            // Try to get its content
            const gchar* address[2];
            std::string text;
            address[0] = script->xlinkhref;
            // Relative path
            address[1] = g_strconcat(SP_ACTIVE_DOCUMENT->getBase(), "/", script->xlinkhref, NULL);
            for (int i=0; i<2; i++) {
                std::ifstream in(address[i]);
                text.assign( (std::istreambuf_iterator<char>(in)), (std::istreambuf_iterator<char>()) );
                in.close();
                if (!text.empty()) break;
            }

            if (text.empty()) {
                Gtk::Window window;
                Gtk::MessageDialog dialog(window, _("Error"));
                dialog.set_secondary_text( g_strconcat(_("Could not read file \""), script->xlinkhref, _("\"."), NULL) );
                dialog.run();
                return;
            } else { 
                SPDesktop *desktop = SP_ACTIVE_DESKTOP;
                if (!desktop){
                    g_warning("No active desktop");
                } else {
                    Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();
                    Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");
                    scriptRepr->addChild(xml_doc->createTextNode(text.c_str()), NULL);
                    scriptRepr->setAttribute("id", id.c_str());
                    xml_doc->root()->addChild(scriptRepr, NULL);

                    /* remove the external script */
                    Inkscape::XML::Node *repr = obj->getRepr();
                    if (repr){
                        sp_repr_unparent(repr);

                        // inform the document, so we can undo
                        DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_EMBED_SCRIPT, _("Embed script"));
                    }
                }
            }
        }
        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::unembedScript(){
    Glib::ustring id;
    if(_AutoUnembedScriptsList.get_selection()) {
        Gtk::TreeModel::iterator i = _AutoUnembedScriptsList.get_selection()->get_selected();

        if(i){
            id = (*i)[_AutoUnembedScriptsListColumns.idColumn];
        } else {
            return;
        }
    }

    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        if (id == obj->getId()){

            int count=0;
            for ( SPObject *child = obj->children ; child; child = child->next )
            {
                count++;
            }

            if (count>1)
                g_warning("TODO: Found a script element with multiple (%d) child nodes! We must implement support for that!", count);

            //XML Tree being used directly here while it shouldn't be.
            SPObject* child = obj->firstChild();
            //TODO: shouldnt we get all children instead of simply the first child?

            if (child && child->getRepr()){
                std::ofstream out( g_strconcat(SP_ACTIVE_DOCUMENT->getBase(), "/", obj->getId(), NULL) );
                out << child->getRepr()->content();
                out.close();
                Inkscape::XML::Document *xml_doc = SP_ACTIVE_DESKTOP->doc()->getReprDoc();
                Inkscape::XML::Node *scriptRepr = xml_doc->createElement("svg:script");
                scriptRepr->setAttribute("xlink:href", obj->getId());

                xml_doc->root()->addChild(scriptRepr, NULL);
            }
            Inkscape::XML::Node *repr = obj->getRepr();
            if (repr){
                sp_repr_unparent(repr);

                // inform the document, so we can undo
                DocumentUndo::done(SP_ACTIVE_DOCUMENT, SP_VERB_EDIT_UNEMBED_SCRIPT, _("Unembed script"));
            }
        }
        current = g_slist_next(current);
    }

    populate_script_lists();
}

void DocumentProperties::populate_script_lists(){
    _ExternalScriptsListStore->clear();
    _EmbeddedScriptsListStore->clear();
    const GSList *current = SP_ACTIVE_DOCUMENT->getResourceList( "script" );
    if (current) _scripts_observer.set(SP_OBJECT(current->data)->parent);
    while ( current ) {
        SPObject* obj = SP_OBJECT(current->data);
        SPScript* script = (SPScript*) obj;
        if (script->xlinkhref)
        {
            Gtk::TreeModel::Row row = *(_ExternalScriptsListStore->append());
            row[_ExternalScriptsListColumns.filenameColumn] = script->xlinkhref;
        }
        else // Embedded scripts
        {
            Gtk::TreeModel::Row row = *(_EmbeddedScriptsListStore->append());
            row[_EmbeddedScriptsListColumns.idColumn] = obj->getId();
        }

        current = g_slist_next(current);
    }

    // Update the SVG root object interface
    SPObject *obj = SP_OBJECT(SP_ACTIVE_DOCUMENT->getRoot());
    if (_global_events){
        gtk_container_remove(GTK_CONTAINER(_global_events_container), _global_events);
    }
    _global_events = sp_attribute_table_new (obj, 10, int_labels, int_labels, true);
    gtk_widget_show_all (_global_events);
    gtk_container_add (GTK_CONTAINER (_global_events_container), _global_events);
}

void DocumentProperties::populate_object_list(){
    _ObjectScriptsListStore->clear();
    SPObject *obj = SP_OBJECT(SP_ACTIVE_DOCUMENT->getRoot());
    populate_object_list_aux(obj);
}

void DocumentProperties::populate_object_list_aux(SPObject *obj){
    if (obj == 0) return;
    //XML Tree being used directly here while it shouldn't be.
    Inkscape::XML::Node *repr = obj->getRepr();
    if (repr == 0) return;

    bool events_present = false;
    for (int i=0; i<10; i++) {
        if ( repr->attribute(int_labels[i]) ) {
            events_present = true;
            break;
        }
    }
    if (events_present) {
        if(obj!=SP_OBJECT(SP_ACTIVE_DOCUMENT->getRoot())) {
            Gtk::TreeModel::Row row = *(_ObjectScriptsListStore->append());
            row[_ObjectScriptsListColumns.idColumn] = obj->getId();
        }
    }

    SPObject *child = obj->children;
    for (; child; child = child->next) {
        populate_object_list_aux(child);
    }
}

/**
* Called for _updating_ the dialog (e.g. when a new grid was manually added in XML)
*/
void
DocumentProperties::update_gridspage()
{
    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);

    //remove all tabs
    while (_grids_notebook.get_n_pages() != 0) {
        _grids_notebook.remove_page(-1); // this also deletes the page.
    }

    //add tabs
    bool grids_present = false;
    for (GSList const * l = nv->grids; l != NULL; l = l->next) {
        Inkscape::CanvasGrid * grid = (Inkscape::CanvasGrid*) l->data;
        if (!grid->repr->attribute("id")) continue; // update_gridspage is called again when "id" is added
        Glib::ustring name(grid->repr->attribute("id"));
        const char *icon = NULL;
        switch (grid->getGridType()) {
            case GRID_RECTANGULAR:
                icon = "grid-rectangular";
                break;
            case GRID_AXONOMETRIC:
                icon = "grid-axonometric";
                break;
            default:
                break;
        }
        _grids_notebook.append_page(*grid->newWidget(), _createPageTabLabel(name, icon));
        grids_present = true;
    }
    _grids_notebook.show_all();

    if (grids_present)
        _grids_button_remove.set_sensitive(true);
    else
        _grids_button_remove.set_sensitive(false);
}

/**
 * Build grid page of dialog.
 */
void
DocumentProperties::build_gridspage()
{
    /// \todo FIXME: gray out snapping when grid is off.
    /// Dissenting view: you want snapping without grid.

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);
    (void)nv;

    _grids_label_crea.set_markup(_("<b>Creation</b>"));
    _grids_label_def.set_markup(_("<b>Defined grids</b>"));
    _grids_hbox_crea.pack_start(_grids_combo_gridtype, true, true);
    _grids_hbox_crea.pack_start(_grids_button_new, true, true);

    for (gint t = 0; t <= GRID_MAXTYPENR; t++) {
        _grids_combo_gridtype.append_text( CanvasGrid::getName( (GridType) t ) );
    }
    _grids_combo_gridtype.set_active_text( CanvasGrid::getName(GRID_RECTANGULAR) );

    _grids_space.set_size_request (SPACE_SIZE_X, SPACE_SIZE_Y);

    _grids_vbox.set_spacing(4);
    _grids_vbox.pack_start(_grids_label_crea, false, false);
    _grids_vbox.pack_start(_grids_hbox_crea, false, false);
    _grids_vbox.pack_start(_grids_space, false, false);
    _grids_vbox.pack_start(_grids_label_def, false, false);
    _grids_vbox.pack_start(_grids_notebook, false, false);
    _grids_vbox.pack_start(_grids_button_remove, false, false);

    update_gridspage();
}



/**
 * Update dialog widgets from desktop. Also call updateWidget routines of the grids.
 */
void
DocumentProperties::update()
{
    if (_wr.isUpdating()) return;

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);

    _wr.setUpdating (true);
    set_sensitive (true);

    //-----------------------------------------------------------page page
    _rcp_bg.setRgba32 (nv->pagecolor);
    _rcb_canb.setActive (nv->showborder);
    _rcb_bord.setActive (nv->borderlayer == SP_BORDER_LAYER_TOP);
    _rcp_bord.setRgba32 (nv->bordercolor);
    _rcb_shad.setActive (nv->showpageshadow);

    if (nv->doc_units)
        _rum_deflt.setUnit (nv->doc_units);

    double const doc_w_px = sp_desktop_document(dt)->getWidth();
    double const doc_h_px = sp_desktop_document(dt)->getHeight();
    _page_sizer.setDim (doc_w_px, doc_h_px);
    _page_sizer.updateFitMarginsUI(nv->getRepr());

    //-----------------------------------------------------------guide page

    _rcb_sgui.setActive (nv->showguides);
    _rcp_gui.setRgba32 (nv->guidecolor);
    _rcp_hgui.setRgba32 (nv->guidehicolor);
    _rcbsng.setActive(nv->snap_manager.snapprefs.getSnapModeGuide());

    //-----------------------------------------------------------snap page

    _rsu_sno.setValue (nv->snap_manager.snapprefs.getObjectTolerance());
    _rsu_sn.setValue (nv->snap_manager.snapprefs.getGridTolerance());
    _rsu_gusn.setValue (nv->snap_manager.snapprefs.getGuideTolerance());


    //-----------------------------------------------------------grids page

    update_gridspage();

    //------------------------------------------------Color Management page

#if ENABLE_LCMS
    populate_linked_profiles_box();
    populate_available_profiles();
#endif // ENABLE_LCMS

    _wr.setUpdating (false);

    //---------------------------------------------------------scripts page

    populate_script_lists();
    populate_object_list();
}

// TODO: copied from fill-and-stroke.cpp factor out into new ui/widget file?
Gtk::HBox&
DocumentProperties::_createPageTabLabel(const Glib::ustring& label, const char *label_image)
{
    Gtk::HBox *_tab_label_box = manage(new Gtk::HBox(false, 0));
    _tab_label_box->set_spacing(4);
    _tab_label_box->pack_start(*Glib::wrap(sp_icon_new(Inkscape::ICON_SIZE_DECORATION,
                                                       label_image)));

    Gtk::Label *_tab_label = manage(new Gtk::Label(label, true));
    _tab_label_box->pack_start(*_tab_label);
    _tab_label_box->show_all();

    return *_tab_label_box;
}

//--------------------------------------------------------------------

void
DocumentProperties::on_response (int id)
{
    if (id == Gtk::RESPONSE_DELETE_EVENT || id == Gtk::RESPONSE_CLOSE)
    {
        _rcp_bg.closeWindow();
        _rcp_bord.closeWindow();
        _rcp_gui.closeWindow();
        _rcp_hgui.closeWindow();
    }

    if (id == Gtk::RESPONSE_CLOSE)
        hide();
}

void
DocumentProperties::_handleDocumentReplaced(SPDesktop* desktop, SPDocument *document)
{
    Inkscape::XML::Node *repr = sp_desktop_namedview(desktop)->getRepr();
    repr->addListener(&_repr_events, this);
    Inkscape::XML::Node *root = document->getRoot()->getRepr();
    root->addListener(&_repr_events, this);
    update();
}

void
DocumentProperties::_handleActivateDesktop(Inkscape::Application *, SPDesktop *desktop)
{
    Inkscape::XML::Node *repr = sp_desktop_namedview(desktop)->getRepr();
    repr->addListener(&_repr_events, this);
    Inkscape::XML::Node *root = sp_desktop_document(desktop)->getRoot()->getRepr();
    root->addListener(&_repr_events, this);
    update();
}

void
DocumentProperties::_handleDeactivateDesktop(Inkscape::Application *, SPDesktop *desktop)
{
    Inkscape::XML::Node *repr = sp_desktop_namedview(desktop)->getRepr();
    repr->removeListenerByData(this);
    Inkscape::XML::Node *root = sp_desktop_document(desktop)->getRoot()->getRepr();
    root->removeListenerByData(this);
}

static void
on_child_added(Inkscape::XML::Node */*repr*/, Inkscape::XML::Node */*child*/, Inkscape::XML::Node */*ref*/, void *data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update_gridspage();
}

static void
on_child_removed(Inkscape::XML::Node */*repr*/, Inkscape::XML::Node */*child*/, Inkscape::XML::Node */*ref*/, void *data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update_gridspage();
}



/**
 * Called when XML node attribute changed; updates dialog widgets.
 */
static void
on_repr_attr_changed (Inkscape::XML::Node *, gchar const *, gchar const *, gchar const *, bool, gpointer data)
{
    if (DocumentProperties *dialog = static_cast<DocumentProperties *>(data))
        dialog->update();
}


/*########################################################################
# BUTTON CLICK HANDLERS    (callbacks)
########################################################################*/

void
DocumentProperties::onNewGrid()
{
    SPDesktop *dt = getDesktop();
    Inkscape::XML::Node *repr = sp_desktop_namedview(dt)->getRepr();
    SPDocument *doc = sp_desktop_document(dt);

    Glib::ustring typestring = _grids_combo_gridtype.get_active_text();
    CanvasGrid::writeNewGridToRepr(repr, doc, CanvasGrid::getGridTypeFromName(typestring.c_str()));

    // toggle grid showing to ON:
    dt->showGrids(true);
}


void
DocumentProperties::onRemoveGrid()
{
    gint pagenum = _grids_notebook.get_current_page();
    if (pagenum == -1) // no pages
      return;

    SPDesktop *dt = getDesktop();
    SPNamedView *nv = sp_desktop_namedview(dt);
    Inkscape::CanvasGrid * found_grid = NULL;
    int i = 0;
    for (GSList const * l = nv->grids; l != NULL; l = l->next, i++) {  // not a very nice fix, but works.
        Inkscape::CanvasGrid * grid = (Inkscape::CanvasGrid*) l->data;
        if (pagenum == i) {
            found_grid = grid;
            break; // break out of for-loop
        }
    }
    if (found_grid) {
        // delete the grid that corresponds with the selected tab
        // when the grid is deleted from SVG, the SPNamedview handler automatically deletes the object, so found_grid becomes an invalid pointer!
        found_grid->repr->parent()->removeChild(found_grid->repr);
        DocumentUndo::done(sp_desktop_document(dt), SP_VERB_DIALOG_NAMEDVIEW, _("Remove grid"));
    }
}


} // namespace Dialog
} // namespace UI
} // namespace Inkscape

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
