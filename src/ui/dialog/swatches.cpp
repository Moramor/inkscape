// SPDX-License-Identifier: GPL-2.0-or-later
/* Authors:
 *   Jon A. Cruz
 *   John Bintz
 *   Abhishek Sharma
 *
 * Copyright (C) 2005 Jon A. Cruz
 * Copyright (C) 2008 John Bintz
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "swatches.h"

#include <map>
#include <algorithm>
#include <iomanip>
#include <set>

#include <gtkmm/checkmenuitem.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/radiomenuitem.h>
#include <gtkmm/radiomenuitem.h>
#include <gtkmm/separatormenuitem.h>

#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/timer.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "color-item.h"
#include "desktop-style.h"
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "gradient-chemistry.h"
#include "inkscape.h"
#include "message-context.h"
#include "path-prefix.h"
#include "verbs.h"

#include "display/cairo-utils.h"
#include "extension/db.h"
#include "helper/action.h"
#include "io/resource.h"
#include "io/sys.h"
#include "object/sp-defs.h"
#include "object/sp-gradient-reference.h"
#include "ui/dialog/dialog-container.h"
#include "ui/icon-names.h"
#include "ui/previewholder.h"
#include "ui/widget/color-palette.h"
#include "ui/widget/gradient-vector-selector.h"
#include "widgets/desktop-widget.h"
#include "widgets/ege-paint-def.h"

namespace Inkscape {
namespace UI {
namespace Dialog {


enum {
    SWATCHES_SETTINGS_SIZE = 0,
    SWATCHES_SETTINGS_MODE = 1,
    SWATCHES_SETTINGS_SHAPE = 2,
    SWATCHES_SETTINGS_WRAP = 3,
    SWATCHES_SETTINGS_BORDER = 4,
    SWATCHES_SETTINGS_PALETTE = 5
};

#define VBLOCK 16
#define PREVIEW_PIXBUF_WIDTH 128

std::list<SwatchPage*> userSwatchPages;
std::list<SwatchPage*> systemSwatchPages;
static std::map<SPDocument*, SwatchPage*> docPalettes;
static std::vector<DocTrack*> docTrackings;
static std::map<SwatchesPanel*, SPDocument*> docPerPanel;


class SwatchesPanelHook : public SwatchesPanel
{
public:
    static void convertGradient( GtkMenuItem *menuitem, gpointer userData );
    static void deleteGradient( GtkMenuItem *menuitem, gpointer userData );
};

static void handleClick( GtkWidget* /*widget*/, gpointer callback_data ) {
    ColorItem* item = reinterpret_cast<ColorItem*>(callback_data);
    if ( item ) {
        item->buttonClicked(false);
    }
}

static void handleSecondaryClick( GtkWidget* /*widget*/, gint /*arg1*/, gpointer callback_data ) {
    ColorItem* item = reinterpret_cast<ColorItem*>(callback_data);
    if ( item ) {
        item->buttonClicked(true);
    }
}

static GtkWidget* popupMenu = nullptr;
static GtkWidget *popupSubHolder = nullptr;
static GtkWidget *popupSub = nullptr;
static std::vector<Glib::ustring> popupItems;
static std::vector<GtkWidget*> popupExtras;
static ColorItem* bounceTarget = nullptr;
static SwatchesPanel* bouncePanel = nullptr;

static void redirClick( GtkMenuItem *menuitem, gpointer /*user_data*/ )
{
    if ( bounceTarget ) {
        handleClick( GTK_WIDGET(menuitem), bounceTarget );
    }
}

static void redirSecondaryClick( GtkMenuItem *menuitem, gpointer /*user_data*/ )
{
    if ( bounceTarget ) {
        handleSecondaryClick( GTK_WIDGET(menuitem), 0, bounceTarget );
    }
}

static void editGradientImpl( SPDesktop* desktop, SPGradient* gr )
{
    g_assert(desktop != nullptr);
    g_assert(desktop->doc() != nullptr);

    if ( gr ) {
        bool shown = false;
        {
            Inkscape::Selection *selection = desktop->getSelection();
            std::vector<SPItem*> const items(selection->items().begin(), selection->items().end());
            if (!items.empty()) {
                SPStyle query( desktop->doc() );
                int result = objects_query_fillstroke((items), &query, true);
                if ( (result == QUERY_STYLE_MULTIPLE_SAME) || (result == QUERY_STYLE_SINGLE) ) {
                    // could be pertinent
                    if (query.fill.isPaintserver()) {
                        SPPaintServer* server = query.getFillPaintServer();
                        if ( SP_IS_GRADIENT(server) ) {
                            SPGradient* grad = SP_GRADIENT(server);
                            if ( grad->isSwatch() && grad->getId() == gr->getId()) {
                                desktop->getContainer()->new_dialog("FillStroke");
                                shown = true;
                            }
                        }
                    }
                }
            }
        }

        if (!shown) {
            // Invoke the gradient tool
            auto verb = Inkscape::Verb::get(SP_VERB_CONTEXT_GRADIENT);
            if (verb) {
                auto action = verb->get_action(Inkscape::ActionContext(desktop));
                if (action) {
                    sp_action_perform(action, nullptr);
                }
            }
        }
    }
}

static void editGradient( GtkMenuItem */*menuitem*/, gpointer /*user_data*/ )
{
    if ( bounceTarget ) {
        SwatchesPanel* swp = bouncePanel;
        SPDesktop* desktop = swp ? swp->getDesktop() : nullptr;
        SPDocument *doc = desktop ? desktop->doc() : nullptr;
        if (doc) {
            std::string targetName(bounceTarget->def.descr);
            std::vector<SPObject *> gradients = doc->getResourceList("gradient");
            for (auto gradient : gradients) {
                SPGradient* grad = SP_GRADIENT(gradient);
                if ( targetName == grad->getId() ) {
                    editGradientImpl( desktop, grad );
                    break;
                }
            }
        }
    }
}

void SwatchesPanelHook::convertGradient( GtkMenuItem * /*menuitem*/, gpointer userData )
{
    if ( bounceTarget ) {
        SwatchesPanel* swp = bouncePanel;
        SPDesktop* desktop = swp ? swp->getDesktop() : nullptr;
        SPDocument *doc = desktop ? desktop->doc() : nullptr;
        gint index = GPOINTER_TO_INT(userData);
        if ( doc && (index >= 0) && (static_cast<guint>(index) < popupItems.size()) ) {
            Glib::ustring targetName = popupItems[index];
            std::vector<SPObject *> gradients = doc->getResourceList("gradient");
            for (auto gradient : gradients) {
                SPGradient* grad = SP_GRADIENT(gradient);

                if ( targetName == grad->getId() ) {
                    grad->setSwatch();
                    DocumentUndo::done(doc, _("Add gradient stop"), INKSCAPE_ICON("color-gradient"));
                    break;
                }
            }
        }
    }
}

void SwatchesPanelHook::deleteGradient( GtkMenuItem */*menuitem*/, gpointer /*userData*/ )
{
    if ( bounceTarget ) {
        SwatchesPanel* swp = bouncePanel;
        SPDesktop* desktop = swp ? swp->getDesktop() : nullptr;
        sp_gradient_unset_swatch(desktop, bounceTarget->def.descr);
    }
}

static SwatchesPanel* findContainingPanel( GtkWidget *widget )
{
    SwatchesPanel *swp = nullptr;

    std::map<GtkWidget*, SwatchesPanel*> rawObjects;
    for (std::map<SwatchesPanel*, SPDocument*>::iterator it = docPerPanel.begin(); it != docPerPanel.end(); ++it) {
        rawObjects[GTK_WIDGET(it->first->gobj())] = it->first;
    }

    for (GtkWidget* curr = widget; curr && !swp; curr = gtk_widget_get_parent(curr)) {
        if (rawObjects.find(curr) != rawObjects.end()) {
            swp = rawObjects[curr];
        }
    }

    return swp;
}

static void removeit( GtkWidget *widget, gpointer data )
{
    gtk_container_remove( GTK_CONTAINER(data), widget );
}

/* extern'ed from color-item.cpp */
bool colorItemHandleButtonPress(GdkEventButton* event, UI::Widget::Preview *preview, gpointer user_data)
{
    gboolean handled = FALSE;

    if ( event && (event->button == 3) && (event->type == GDK_BUTTON_PRESS) ) {
        SwatchesPanel* swp = findContainingPanel( GTK_WIDGET(preview->gobj()) );

        if ( !popupMenu ) {
            popupMenu = gtk_menu_new();
            GtkWidget* child = nullptr;

            //TRANSLATORS: An item in context menu on a colour in the swatches
            child = gtk_menu_item_new_with_label(_("Set fill"));
            g_signal_connect( G_OBJECT(child),
                              "activate",
                              G_CALLBACK(redirClick),
                              user_data);
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);

            //TRANSLATORS: An item in context menu on a colour in the swatches
            child = gtk_menu_item_new_with_label(_("Set stroke"));

            g_signal_connect( G_OBJECT(child),
                              "activate",
                              G_CALLBACK(redirSecondaryClick),
                              user_data);
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);

            child = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);
            popupExtras.push_back(child);

            child = gtk_menu_item_new_with_label(_("Delete"));
            g_signal_connect( G_OBJECT(child),
                              "activate",
                              G_CALLBACK(SwatchesPanelHook::deleteGradient),
                              user_data );
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);
            popupExtras.push_back(child);
            gtk_widget_set_sensitive( child, FALSE );

            child = gtk_menu_item_new_with_label(_("Edit..."));
            g_signal_connect( G_OBJECT(child),
                              "activate",
                              G_CALLBACK(editGradient),
                              user_data );
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);
            popupExtras.push_back(child);

            child = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);
            popupExtras.push_back(child);

            child = gtk_menu_item_new_with_label(_("Convert"));
            gtk_menu_shell_append(GTK_MENU_SHELL(popupMenu), child);
            //popupExtras.push_back(child);
            //gtk_widget_set_sensitive( child, FALSE );
            {
                popupSubHolder = child;
                popupSub = gtk_menu_new();
                gtk_menu_item_set_submenu( GTK_MENU_ITEM(child), popupSub );
            }

            gtk_widget_show_all(popupMenu);
        }

        if ( user_data ) {
            ColorItem* item = reinterpret_cast<ColorItem*>(user_data);
            bool show = swp && (swp->getSelectedIndex() == 0);
            for (auto & popupExtra : popupExtras) {
                gtk_widget_set_sensitive(popupExtra, show);
            }

            bounceTarget = item;
            bouncePanel = swp;
            popupItems.clear();
            if ( popupMenu ) {
                gtk_container_foreach(GTK_CONTAINER(popupSub), removeit, popupSub);
                bool processed = false;
                auto *wdgt = preview->get_ancestor(SPDesktopWidget::get_type());
                if ( wdgt ) {
                    SPDesktopWidget *dtw = SP_DESKTOP_WIDGET(wdgt);
                    if ( dtw && dtw->desktop ) {
                        // Pick up all gradients with vectors
                        std::vector<SPObject *> gradients = (dtw->desktop->doc())->getResourceList("gradient");
                        gint index = 0;
                        for (auto gradient : gradients) {
                            SPGradient* grad = SP_GRADIENT(gradient);
                            if ( grad->hasStops() && !grad->isSwatch() ) {
                                //gl = g_slist_prepend(gl, curr->data);
                                processed = true;
                                GtkWidget *child = gtk_menu_item_new_with_label(grad->getId());
                                gtk_menu_shell_append(GTK_MENU_SHELL(popupSub), child);

                                popupItems.emplace_back(grad->getId());
                                g_signal_connect( G_OBJECT(child),
                                                  "activate",
                                                  G_CALLBACK(SwatchesPanelHook::convertGradient),
                                                  GINT_TO_POINTER(index) );
                                index++;
                            }
                        }

                        gtk_widget_show_all(popupSub);
                    }
                }
                gtk_widget_set_sensitive( popupSubHolder, processed );
                gtk_menu_popup_at_pointer(GTK_MENU(popupMenu), reinterpret_cast<GdkEvent *>(event));
                handled = TRUE;
            }
        }
    }

    return handled;
}


static char* trim( char* str ) {
    char* ret = str;
    while ( *str && (*str == ' ' || *str == '\t') ) {
        str++;
    }
    ret = str;
    while ( *str ) {
        str++;
    }
    str--;
    while ( str >= ret && (( *str == ' ' || *str == '\t' ) || *str == '\r' || *str == '\n') ) {
        *str-- = 0;
    }
    return ret;
}

static void skipWhitespace( char*& str ) {
    while ( *str == ' ' || *str == '\t' ) {
        str++;
    }
}

static bool parseNum( char*& str, int& val ) {
    val = 0;
    while ( '0' <= *str && *str <= '9' ) {
        val = val * 10 + (*str - '0');
        str++;
    }
    bool retval = !(*str == 0 || *str == ' ' || *str == '\t' || *str == '\r' || *str == '\n');
    return retval;
}


static
void _loadPaletteFile(Glib::ustring path, gboolean user/*=FALSE*/)
{
    auto filename = Glib::path_get_basename(path.raw());
    char block[1024];
    FILE *f = Inkscape::IO::fopen_utf8name(path.c_str(), "r");
    if ( f ) {
        char* result = fgets( block, sizeof(block), f );
        if ( result ) {
            if ( strncmp( "GIMP Palette", block, 12 ) == 0 ) {
                bool inHeader = true;
                bool hasErr = false;

                SwatchPage *onceMore = new SwatchPage();
                onceMore->_name = filename.c_str();

                do {
                    result = fgets( block, sizeof(block), f );
                    block[sizeof(block) - 1] = 0;
                    if ( result ) {
                        if ( block[0] == '#' ) {
                            // ignore comment
                        } else {
                            char *ptr = block;
                            // very simple check for header versus entry
                            while ( *ptr == ' ' || *ptr == '\t' ) {
                                ptr++;
                            }
                            if ( (*ptr == 0) || (*ptr == '\r') || (*ptr == '\n') ) {
                                // blank line. skip it.
                            } else if ( '0' <= *ptr && *ptr <= '9' ) {
                                // should be an entry link
                                inHeader = false;
                                ptr = block;
                                Glib::ustring name("");
                                skipWhitespace(ptr);
                                if ( *ptr ) {
                                    int r = 0;
                                    int g = 0;
                                    int b = 0;
                                    hasErr = parseNum(ptr, r);
                                    if ( !hasErr ) {
                                        skipWhitespace(ptr);
                                        hasErr = parseNum(ptr, g);
                                    }
                                    if ( !hasErr ) {
                                        skipWhitespace(ptr);
                                        hasErr = parseNum(ptr, b);
                                    }
                                    if ( !hasErr && *ptr ) {
                                        char* n = trim(ptr);
                                        if (n != nullptr && *n) {
                                            name = g_dpgettext2(nullptr, "Palette", n);
                                        }
                                        if (name == "") {
                                            name = Glib::ustring::compose("#%1%2%3",
                                                       Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), r),
                                                       Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), g),
                                                       Glib::ustring::format(std::hex, std::setw(2), std::setfill(L'0'), b)
                                                   ).uppercase();
                                        }
                                    }
                                    if ( !hasErr ) {
                                        // Add the entry now
                                        Glib::ustring nameStr(name);
                                        ColorItem* item = new ColorItem( r, g, b, nameStr );
                                        onceMore->_colors.push_back(item);
                                    }
                                } else {
                                    hasErr = true;
                                }
                            } else {
                                if ( !inHeader ) {
                                    // Hmmm... probably bad. Not quite the format we want?
                                    hasErr = true;
                                } else {
                                    char* sep = strchr(result, ':');
                                    if ( sep ) {
                                        *sep = 0;
                                        char* val = trim(sep + 1);
                                        char* name = trim(result);
                                        if ( *name ) {
                                            if ( strcmp( "Name", name ) == 0 )
                                            {
                                                onceMore->_name = val;
                                            }
                                            else if ( strcmp( "Columns", name ) == 0 )
                                            {
                                                gchar* endPtr = nullptr;
                                                guint64 numVal = g_ascii_strtoull( val, &endPtr, 10 );
                                                if ( (numVal == G_MAXUINT64) && (ERANGE == errno) ) {
                                                    // overflow
                                                } else if ( (numVal == 0) && (endPtr == val) ) {
                                                    // failed conversion
                                                } else {
                                                    onceMore->_prefWidth = numVal;
                                                }
                                            }
                                        } else {
                                            // error
                                            hasErr = true;
                                        }
                                    } else {
                                        // error
                                        hasErr = true;
                                    }
                                }
                            }
                        }
                    }
                } while ( result && !hasErr );
                if ( !hasErr ) {
                    if (user)
                        userSwatchPages.push_back(onceMore);
                    else
                        systemSwatchPages.push_back(onceMore);
                } else {
                    delete onceMore;
                }
            }
        }

        fclose(f);
    }
}

static bool
compare_swatch_names(SwatchPage const *a, SwatchPage const *b) {

    return g_utf8_collate(a->_name.c_str(), b->_name.c_str()) < 0;
}

static void load_palettes()
{
    static bool init_done = false;

    if (init_done) {
        return;
    }
    init_done = true;

    for (auto &filename: Inkscape::IO::Resource::get_filenames(Inkscape::IO::Resource::PALETTES, {".gpl"})) {
        bool userPalette = Inkscape::IO::file_is_writable(filename.c_str());
        _loadPaletteFile(filename, userPalette);
    }

    // Sort the list of swatches by name, grouped by user/system
    userSwatchPages.sort(compare_swatch_names);
    systemSwatchPages.sort(compare_swatch_names);
}

SwatchesPanel& SwatchesPanel::getInstance()
{
    return *new SwatchesPanel();
}


class DocTrack
{
public:
    DocTrack(SPDocument *doc, sigc::connection &gradientRsrcChanged, sigc::connection &defsChanged, sigc::connection &defsModified) :
        doc(doc->doRef()),
        updatePending(false),
        lastGradientUpdate(0.0),
        gradientRsrcChanged(gradientRsrcChanged),
        defsChanged(defsChanged),
        defsModified(defsModified)
    {
        if ( !timer ) {
            timer = new Glib::Timer();
            refreshTimer = Glib::signal_timeout().connect( sigc::ptr_fun(handleTimerCB), 33 );
        }
        timerRefCount++;
    }

    ~DocTrack()
    {
        timerRefCount--;
        if ( timerRefCount <= 0 ) {
            refreshTimer.disconnect();
            timerRefCount = 0;
            if ( timer ) {
                timer->stop();
                delete timer;
                timer = nullptr;
            }
        }
        if (doc) {
            gradientRsrcChanged.disconnect();
            defsChanged.disconnect();
            defsModified.disconnect();
        }
    }

    static bool handleTimerCB();

    /**
     * Checks if update should be queued or executed immediately.
     *
     * @return true if the update was queued and should not be immediately executed.
     */
    static bool queueUpdateIfNeeded(SPDocument *doc);

    static Glib::Timer *timer;
    static int timerRefCount;
    static sigc::connection refreshTimer;

    std::unique_ptr<SPDocument> doc;
    bool updatePending;
    double lastGradientUpdate;
    sigc::connection gradientRsrcChanged;
    sigc::connection defsChanged;
    sigc::connection defsModified;

private:
    DocTrack(DocTrack const &) = delete; // no copy
    DocTrack &operator=(DocTrack const &) = delete; // no assign
};

Glib::Timer *DocTrack::timer = nullptr;
int DocTrack::timerRefCount = 0;
sigc::connection DocTrack::refreshTimer;

static const double DOC_UPDATE_THREASHOLD  = 0.090;

bool DocTrack::handleTimerCB()
{
    double now = timer->elapsed();

    std::vector<DocTrack *> needCallback;
    for (auto track : docTrackings) {
        if ( track->updatePending && ( (now - track->lastGradientUpdate) >= DOC_UPDATE_THREASHOLD) ) {
            needCallback.push_back(track);
        }
    }

    for (auto track : needCallback) {
        if ( std::find(docTrackings.begin(), docTrackings.end(), track) != docTrackings.end() ) { // Just in case one gets deleted while we are looping
            // Note: calling handleDefsModified will call queueUpdateIfNeeded and thus update the time and flag.
            SwatchesPanel::handleDefsModified(track->doc.get());
        }
    }

    return true;
}

bool DocTrack::queueUpdateIfNeeded( SPDocument *doc )
{
    bool deferProcessing = false;
    for (auto track : docTrackings) {
        if ( track->doc.get() == doc ) {
            double now = timer->elapsed();
            double elapsed = now - track->lastGradientUpdate;

            if ( elapsed < DOC_UPDATE_THREASHOLD ) {
                deferProcessing = true;
                track->updatePending = true;
            } else {
                track->lastGradientUpdate = now;
                track->updatePending = false;
            }

            break;
        }
    }
    return deferProcessing;
}

void SwatchesPanel::_trackDocument( SwatchesPanel *panel, SPDocument *document )
{
    SPDocument *oldDoc = nullptr;
    if (docPerPanel.find(panel) != docPerPanel.end()) {
        oldDoc = docPerPanel[panel];
        if (!oldDoc) {
            docPerPanel.erase(panel); // Should not be needed, but clean up just in case.
        }
    }
    if (oldDoc != document) {
        if (oldDoc) {
            docPerPanel[panel] = nullptr;
            bool found = false;
            for (std::map<SwatchesPanel*, SPDocument*>::iterator it = docPerPanel.begin(); (it != docPerPanel.end()) && !found; ++it) {
                found = (it->second == document);
            }
            if (!found) {
                for (std::vector<DocTrack*>::iterator it = docTrackings.begin(); it != docTrackings.end(); ++it){
                    if ((*it)->doc.get() == oldDoc) {
                        delete *it;
                        docTrackings.erase(it);
                        break;
                    }
                }
            }
        }

        if (document) {
            bool found = false;
            for (std::map<SwatchesPanel*, SPDocument*>::iterator it = docPerPanel.begin(); (it != docPerPanel.end()) && !found; ++it) {
                found = (it->second == document);
            }
            docPerPanel[panel] = document;
            if (!found) {
                sigc::connection conn1 = document->connectResourcesChanged( "gradient", sigc::bind(sigc::ptr_fun(&SwatchesPanel::handleGradientsChange), document) );
                sigc::connection conn2 = document->getDefs()->connectRelease( sigc::hide(sigc::bind(sigc::ptr_fun(&SwatchesPanel::handleDefsModified), document)) );
                sigc::connection conn3 = document->getDefs()->connectModified( sigc::hide(sigc::hide(sigc::bind(sigc::ptr_fun(&SwatchesPanel::handleDefsModified), document))) );

                DocTrack *dt = new DocTrack(document, conn1, conn2, conn3);
                docTrackings.push_back(dt);

                if (docPalettes.find(document) == docPalettes.end()) {
                    SwatchPage *docPalette = new SwatchPage();
                    docPalette->_name = "Auto";
                    docPalettes[document] = docPalette;
                }
            }
            // Always update the palettes if there's a document.
            panel->updatePalettes();
        }
    }
}


/**
 * Constructor
 */
SwatchesPanel::SwatchesPanel(gchar const *prefsPath)
    : DialogBase(prefsPath, "Swatches")
    , _menu(nullptr)
    , _holder(nullptr)
    , _clear(nullptr)
    , _remove(nullptr)
    , _currentIndex(0)
{
    _palette = Gtk::manage(new Inkscape::UI::Widget::ColorPalette());
    pack_start(*_palette);

    if (_prefs_path == "/dialogs/swatches") {
        _palette->set_compact(false);
    } else {
        _palette->set_compact(true);
    }

    load_palettes();

    _clear = new ColorItem( ege::PaintDef::CLEAR );
    _remove = new ColorItem( ege::PaintDef::NONE );

    if (docPalettes.empty()) {
        SwatchPage *docPalette = new SwatchPage();

        docPalette->_name = "Empty";
        docPalettes[nullptr] = docPalette;
    }

    if ( !systemSwatchPages.empty() || !userSwatchPages.empty()) {
        SwatchPage* first = nullptr;
        int index = 0;
        Glib::ustring targetName;
        if ( !_prefs_path.empty() ) {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            targetName = prefs->getString(_prefs_path + "/palette");
            if (!targetName.empty()) {
                if (targetName == "Empty") {
                    first = docPalettes[nullptr];
                } else {
                    std::vector<SwatchPage*> pages = _getSwatchSets();
                    for (auto & page : pages) {
                        if ( page->_name == targetName ) {
                            first = page;
                            break;
                        }
                        index++;
                    }
                }
            }
        }

        if ( !first ) {
            first = docPalettes[nullptr];
            _currentIndex = 0;
        } else {
            _currentIndex = index;
        }

        // restore palette settings
        Inkscape::Preferences* prefs = Inkscape::Preferences::get();
        _palette->set_tile_size(prefs->getInt(_prefs_path + "/tile_size", 16));
        _palette->set_aspect(prefs->getDoubleLimited(_prefs_path + "/tile_aspect", 0.0, -2, 2));
        _palette->set_tile_border(prefs->getInt(_prefs_path + "/tile_border", 1));
        _palette->set_rows(prefs->getInt(_prefs_path + "/rows", 1));
        _palette->enable_stretch(prefs->getBool(_prefs_path + "/tile_stretch", false));
        // save settings when they change
        _palette->get_settings_changed_signal().connect([=](){
            prefs->setInt(_prefs_path + "/tile_size", _palette->get_tile_size());
            prefs->setDouble(_prefs_path + "/tile_aspect", _palette->get_aspect());
            prefs->setInt(_prefs_path + "/tile_border", _palette->get_tile_border());
            prefs->setInt(_prefs_path + "/rows", _palette->get_rows());
            prefs->setBool(_prefs_path + "/tile_stretch", _palette->is_stretch_enabled());
        });

        // switch swatch palettes
        _palette->get_palette_selected_signal().connect([=](Glib::ustring name) {
            std::vector<SwatchPage*> pages = _getSwatchSets();
            auto it = std::find_if(pages.begin(), pages.end(), [&](auto el){ return el->_name == name; });
            if (it != pages.end()) {
                auto index = static_cast<int>(it - pages.begin());
                if (_currentIndex != index) {
                    _currentIndex = index;
                    Inkscape::Preferences* prefs = Inkscape::Preferences::get();
                    prefs->setString(_prefs_path + "/palette", pages[_currentIndex]->_name);
                    _rebuild();
                }
            }
        });
    }
}

SwatchesPanel::~SwatchesPanel()
{
    _trackDocument( this, nullptr );
    for (auto & docTracking : docTrackings){
        delete docTracking;
    }
    docTrackings.clear();

    if ( _clear ) {
        delete _clear;
    }
    if ( _remove ) {
        delete _remove;
    }
}

/**
 * Process the list of available palettes and update the list
 * in the _palette widget. The widget will take care of cleaning.
 */
void SwatchesPanel::updatePalettes()
{
    std::vector<SwatchPage*> swatchSets = _getSwatchSets();

    std::vector<Inkscape::UI::Widget::ColorPalette::palette_t> palettes;
    palettes.reserve(swatchSets.size());
    for (auto curr : swatchSets) {
        Inkscape::UI::Widget::ColorPalette::palette_t palette;
        palette.name = curr->_name;
        for (const auto& color : curr->_colors) {
            if (color.def.getType() == ege::PaintDef::RGB) {
                auto& c = color.def;
                palette.colors.push_back(
                    Inkscape::UI::Widget::ColorPalette::rgb_t { c.getR() / 255.0, c.getG() / 255.0, c.getB() / 255.0 });
            }
        }
        palettes.push_back(palette);
    }

    // pass list of available palettes
    _palette->set_palettes(palettes);
    _rebuild();
}

void SwatchesPanel::_updateSettings(int settings, int value)
{
}

void SwatchesPanel::documentReplaced()
{
    _trackDocument(this, getDocument());
    if (auto document = getDocument()) {
        handleGradientsChange(document);
    }
}

static void recalcSwatchContents(SPDocument* doc,
                boost::ptr_vector<ColorItem> &tmpColors,
                std::map<ColorItem*, cairo_pattern_t*> &previewMappings,
                std::map<ColorItem*, SPGradient*> &gradMappings)
{
    std::vector<SPGradient*> newList;
    std::vector<SPObject *> gradients = doc->getResourceList("gradient");
    for (auto gradient : gradients) {
        SPGradient* grad = SP_GRADIENT(gradient);
        if ( grad->isSwatch() ) {
            newList.push_back(SP_GRADIENT(gradient));
        }
    }

    if ( !newList.empty() ) {
        std::reverse(newList.begin(), newList.end());
        for (auto grad : newList)
        {
            cairo_surface_t *preview = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                PREVIEW_PIXBUF_WIDTH, VBLOCK);
            cairo_t *ct = cairo_create(preview);

            Glib::ustring name( grad->getId() );
            ColorItem* item = new ColorItem( 0, 0, 0, name );

            cairo_pattern_t *check = ink_cairo_pattern_create_checkerboard();
            cairo_pattern_t *gradient = grad->create_preview_pattern(PREVIEW_PIXBUF_WIDTH);
            cairo_set_source(ct, check);
            cairo_paint(ct);
            cairo_set_source(ct, gradient);
            cairo_paint(ct);

            cairo_destroy(ct);
            cairo_pattern_destroy(gradient);
            cairo_pattern_destroy(check);

            cairo_pattern_t *prevpat = cairo_pattern_create_for_surface(preview);
            cairo_surface_destroy(preview);

            previewMappings[item] = prevpat;

            tmpColors.push_back(item);
            gradMappings[item] = grad;
        }
    }
}

void SwatchesPanel::handleGradientsChange(SPDocument *document)
{
    SwatchPage *docPalette = (docPalettes.find(document) != docPalettes.end()) ? docPalettes[document] : nullptr;
    if (docPalette) {
        boost::ptr_vector<ColorItem> tmpColors;
        std::map<ColorItem*, cairo_pattern_t*> tmpPrevs;
        std::map<ColorItem*, SPGradient*> tmpGrads;
        recalcSwatchContents(document, tmpColors, tmpPrevs, tmpGrads);

        for (auto & tmpPrev : tmpPrevs) {
            tmpPrev.first->setPattern(tmpPrev.second);
            cairo_pattern_destroy(tmpPrev.second);
        }

        for (auto & tmpGrad : tmpGrads) {
            tmpGrad.first->setGradient(tmpGrad.second);
        }

        docPalette->_colors.swap(tmpColors);

        _rebuildDocumentSwatch(docPalette, document);
    }
}

/**
 * Figure out which SwatchesPanel instances are affected and update them.
 */
void SwatchesPanel::_rebuildDocumentSwatch(SwatchPage *docPalette, SPDocument *document)
{
    for (auto & it : docPerPanel) {
        if (it.second == document) {
            SwatchesPanel* swp = it.first;
            std::vector<SwatchPage*> pages = swp->_getSwatchSets();
            SwatchPage* curr = pages[swp->_currentIndex];
            if (curr == docPalette) {
                swp->_rebuild();
            }
        }
    }
}

void SwatchesPanel::handleDefsModified(SPDocument *document)
{
    SwatchPage *docPalette = (docPalettes.find(document) != docPalettes.end()) ? docPalettes[document] : nullptr;
    if (docPalette && !DocTrack::queueUpdateIfNeeded(document) ) {
        boost::ptr_vector<ColorItem> tmpColors;
        std::map<ColorItem*, cairo_pattern_t*> tmpPrevs;
        std::map<ColorItem*, SPGradient*> tmpGrads;
        recalcSwatchContents(document, tmpColors, tmpPrevs, tmpGrads);

        if ( tmpColors.size() != docPalette->_colors.size() ) {
            handleGradientsChange(document);
        } else {
            int cap = std::min(docPalette->_colors.size(), tmpColors.size());
            for (int i = 0; i < cap; i++) {
                ColorItem *newColor = &tmpColors[i];
                ColorItem *oldColor = &docPalette->_colors[i];
                if ( (newColor->def.getType() != oldColor->def.getType()) ||
                     (newColor->def.getR() != oldColor->def.getR()) ||
                     (newColor->def.getG() != oldColor->def.getG()) ||
                     (newColor->def.getB() != oldColor->def.getB()) ) {
                    oldColor->def.setRGB(newColor->def.getR(), newColor->def.getG(), newColor->def.getB());
                }
                if (tmpGrads.find(newColor) != tmpGrads.end()) {
                    oldColor->setGradient(tmpGrads[newColor]);
                }
                if ( tmpPrevs.find(newColor) != tmpPrevs.end() ) {
                    oldColor->setPattern(tmpPrevs[newColor]);
                }
            }
        }

        for (auto & tmpPrev : tmpPrevs) {
            cairo_pattern_destroy(tmpPrev.second);
        }
        _rebuildDocumentSwatch(docPalette, document);
    }
}


std::vector<SwatchPage*> SwatchesPanel::_getSwatchSets() const
{
    std::vector<SwatchPage*> tmp;
    if (auto document = getDocument()) {
        if (docPalettes.find(document) != docPalettes.end()) {
            tmp.push_back(docPalettes[document]);
        }
    }
    tmp.insert(tmp.end(), userSwatchPages.begin(), userSwatchPages.end());
    tmp.insert(tmp.end(), systemSwatchPages.begin(), systemSwatchPages.end());
    return tmp;
}

void SwatchesPanel::_updateFromSelection()
{
    auto document = getDocument();
    if (!document)
        return;

    SwatchPage *docPalette = (docPalettes.find(document) != docPalettes.end()) ? docPalettes[document] : nullptr;
    if ( docPalette ) {
        std::string fillId;
        std::string strokeId;

        SPStyle tmpStyle(document);
        int result = sp_desktop_query_style(getDesktop(), &tmpStyle, QUERY_STYLE_PROPERTY_FILL );
        switch (result) {
            case QUERY_STYLE_SINGLE:
            case QUERY_STYLE_MULTIPLE_AVERAGED:
            case QUERY_STYLE_MULTIPLE_SAME:
            {
                if (tmpStyle.fill.set && tmpStyle.fill.isPaintserver()) {
                    SPPaintServer* server = tmpStyle.getFillPaintServer();
                    if ( SP_IS_GRADIENT(server) ) {
                        SPGradient* target = nullptr;
                        SPGradient* grad = SP_GRADIENT(server);

                        if ( grad->isSwatch() ) {
                            target = grad;
                        } else if ( grad->ref ) {
                            SPGradient *tmp = grad->ref->getObject();
                            if ( tmp && tmp->isSwatch() ) {
                                target = tmp;
                            }
                        }
                        if ( target ) {
                            //XML Tree being used directly here while it shouldn't be
                            gchar const* id = target->getRepr()->attribute("id");
                            if ( id ) {
                                fillId = id;
                            }
                        }
                    }
                }
                break;
            }
        }

        result = sp_desktop_query_style(getDesktop(), &tmpStyle, QUERY_STYLE_PROPERTY_STROKE);
        switch (result) {
            case QUERY_STYLE_SINGLE:
            case QUERY_STYLE_MULTIPLE_AVERAGED:
            case QUERY_STYLE_MULTIPLE_SAME:
            {
                if (tmpStyle.stroke.set && tmpStyle.stroke.isPaintserver()) {
                    SPPaintServer* server = tmpStyle.getStrokePaintServer();
                    if ( SP_IS_GRADIENT(server) ) {
                        SPGradient* target = nullptr;
                        SPGradient* grad = SP_GRADIENT(server);
                        if ( grad->isSwatch() ) {
                            target = grad;
                        } else if ( grad->ref ) {
                            SPGradient *tmp = grad->ref->getObject();
                            if ( tmp && tmp->isSwatch() ) {
                                target = tmp;
                            }
                        }
                        if ( target ) {
                            //XML Tree being used directly here while it shouldn't be
                            gchar const* id = target->getRepr()->attribute("id");
                            if ( id ) {
                                strokeId = id;
                            }
                        }
                    }
                }
                break;
            }
        }

        for (auto & _color : docPalette->_colors) {
            ColorItem* item = &_color;
            bool isFill = (fillId == item->def.descr);
            bool isStroke = (strokeId == item->def.descr);
            item->setState( isFill, isStroke );
        }
    }
}

void SwatchesPanel::_rebuild()
{
    std::vector<SwatchPage*> pages = _getSwatchSets();
    SwatchPage* curr = pages[_currentIndex];

    std::vector<Widget*> palette;
    palette.reserve(curr->_colors.size() + 1);
    palette.push_back(_remove->createWidget());
    for (auto & _color : curr->_colors) {
        palette.push_back(_color.createWidget());
    }
    _palette->set_colors(palette);
    _palette->set_selected(curr->_name);
}

} //namespace Dialog
} //namespace UI
} //namespace Inkscape


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
