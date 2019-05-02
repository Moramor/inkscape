// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The main Inkscape application.
 *
 * Copyright (C) 2018 Tavmjong Bah
 *
 * The contents of this file may be used under the GNU General Public License Version 2 or later.
 *
 */

#include <iostream>

#include <glibmm/i18n.h>  // Internationalization

#ifdef HAVE_CONFIG_H
# include "config.h"      // Defines ENABLE_NLS
#endif

#include "inkscape-application.h"
#include "inkscape-window.h"

#include "inkscape.h"             // Inkscape::Application
#include "desktop.h"              // Access to window
#include "file.h"                 // sp_file_convert_dpi

#include "inkgc/gc-core.h"        // Garbage Collecting init

#include "io/file.h"              // File open (command line).
#include "io/resource.h"          // TEMPLATE
#include "io/resource-manager.h"  // Fix up references.

#include "object/sp-root.h"       // Inkscape version.

#include "ui/dialog/font-substitution.h"  // Warn user about font substitution.
#include "ui/widget/panel.h"      // Panel prep
#include "widgets/desktop-widget.h" // Close without saving dialog

#include "util/units.h"           // Redimension window

#include "actions/actions-base.h"      // Actions
#include "actions/actions-output.h"    // Actions
#include "actions/actions-selection.h" // Actions
#include "actions/actions-transform.h" // Actions

#ifdef WITH_DBUS
# include "extension/dbus/dbus-init.h"
#endif

#ifdef ENABLE_NLS
// Native Language Support - shouldn't this always be used?
#include "helper/gettext.h"   // gettext init
#endif // ENABLE_NLS

#include "io/resource.h"
using Inkscape::IO::Resource::UIS;

// This is a bit confusing as there are two ways to handle command line arguments and files
// depending on if the Gio::APPLICATION_HANDLES_OPEN and/or Gio::APPLICATION_HANDLES_COMMAND_LINE
// flags are set. If the open flag is set and the command line not, the all the remainng arguments
// after calling on_handle_local_options() are assumed to be filenames.

InkscapeApplication::InkscapeApplication()
    : _with_gui(true)
    , _batch_process(false)
    , _use_shell(false)
    , _active_document(nullptr)
    , _active_selection(nullptr)
    , _active_view(nullptr)
    , _pdf_page(0)
    , _pdf_poppler(false)
{}

// Add document to app.
void
InkscapeApplication::document_add(SPDocument* document)
{
    if (document) {
        auto it = _documents.find(document);
        if (it == _documents.end()) {
            _documents[document] = std::vector<InkscapeWindow*>();
        } else {
            // Should never happen.
            std::cerr << "InkscapeApplication::add_document: Document already opened!" << std::endl;
        }
    } else {
        // Should never happen!
        std::cerr << "InkscapeApplication::add_document: No document!" << std::endl;
    }
}

// New document, add it to app. TODO: This should really be open_document with option to strip template data.
SPDocument*
InkscapeApplication::document_new(const std::string &Template)
{
    // Open file
    SPDocument *document = ink_file_new(Template);
    if (document) {
        document_add(document);

        // Set viewBox if it doesn't exist.
        if (!document->getRoot()->viewBox_set) {
            document->setViewBox();
        }

    } else {
        std::cerr << "InkscapeApplication::new_document: failed to open new document!" << std::endl;
    }

    return document;
}


// Open a document, add it to app.
SPDocument*
InkscapeApplication::document_open(const Glib::RefPtr<Gio::File>& file)
{
    // Open file
    bool cancelled = false;
    SPDocument *document = ink_file_open(file, cancelled);

    if (document) {
        document->setVirgin(false); // Prevents replacing document in same window during file open.

        document_add (document);
    } else {
        std::cerr << "InkscapeApplication::open_document: Failed to open: " << file->get_parse_name() << std::endl;
    }

    return document;
}


/** Swap out one document for another in a window... maybe this should disappear.
 *  Does not delete old document!
 */
bool
InkscapeApplication::document_swap(InkscapeWindow* window, SPDocument* document)
{
    if (!document || !window) {
        std::cerr << "InkscapeAppliation::swap_document: Missing window or document!" << std::endl;
        return false;
    }

    SPDesktop* desktop = window->get_desktop();
    SPDocument* old_document = window->get_document();
    desktop->change_document(document);
    document->emitResizedSignal(document->getWidth().value("px"), document->getHeight().value("px"));

    // We need to move window from the old document to the new document.

    // Find old document
    auto it = _documents.find(old_document);
    if (it != _documents.end()) {

        // Remove window from document map.
        auto it2 = std::find(it->second.begin(), it->second.end(), window);
        if (it2 != it->second.end()) {
            it->second.erase(it2);
        } else {
            std::cerr << "InkscapeApplication::swap_document: Window not found!" << std::endl;
        }

    } else {
        std::cerr << "InkscapeApplication::swap_document: Document not in map!" << std::endl;
    }

    // Find new document
    it = _documents.find(document);
    if (it != _documents.end()) {
        it->second.push_back(window);
    } else {
        std::cerr << "InkscapeApplication::swap_document: Document not in map!" << std::endl;
    }

    // To be removed (add/delete once per window)!
    INKSCAPE.add_document(document);
    INKSCAPE.remove_document(old_document);

    // ActionContext should be removed once verbs are gone but we use it for now.
    Inkscape::ActionContext context = INKSCAPE.action_context_for_document(document);
    _active_document  = document;
    _active_selection = context.getSelection();
    _active_view      = context.getView();

    return true;
}

/** Revert document: open saved document and swap it for each window.
 */
bool
InkscapeApplication::document_revert(SPDocument* document)
{
    // Find saved document.
    gchar const *path = document->getDocumentURI();
    if (!path) {
        std::cerr << "InkscapeApplication::revert_document: Document never saved, cannot revert." << std::endl;
        return false;
    }

    // Open saved document.
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(document->getDocumentURI());
    SPDocument* new_document = document_open (file);
    if (!new_document) {
        std::cerr << "InkscapeApplication::revert_document: Cannot open saved document!" << std::endl;
        return false;
    }

    // Allow overwriting current document.
    document->setVirgin(true);

    auto it = _documents.find(document);
    if (it != _documents.end()) {

        // Swap reverted document in all windows.
        for (auto it2 : it->second) {

            SPDesktop* desktop = it2->get_desktop();

            // Remember current zoom and view.
            double zoom = desktop->current_zoom();
            Geom::Point c = desktop->get_display_area().midpoint();

            bool reverted = document_swap (it2, new_document);

            if (reverted) {
                desktop->zoom_absolute_center_point (c, zoom);
            } else {
                std::cerr << "InkscapeApplication::revert_document: Revert failed!" << std::endl;
            }
        }

        document_close (document);

    } else {
        std::cerr << "InkscapeApplication::revert_document: Document not found!" << std::endl;
        return false;
    }

    return true;
}



/** Close a document, remove from app. No checking is done on modified status, etc.
 */
void
InkscapeApplication::document_close(SPDocument* document)
{
    if (document) {

        auto it = _documents.find(document);
        if (it != _documents.end()) {
            if (it->second.size() != 0) {
                std::cerr << "InkscapeApplication::close_document: Window vector not empty!" << std::endl;
            }
            _documents.erase(it);
        } else {
            std::cerr << "InkscapeApplication::close_document: Document not registered with application." << std::endl;
        }

        delete document;

    } else {
        std::cerr << "InkscapeApplication::close_document: No document!" << std::endl;
    }
}


/** Return number of windows with document.
 */
unsigned
InkscapeApplication::document_window_count(SPDocument* document)
{
    unsigned count = 0;

    auto it = _documents.find(document);
    if (it != _documents.end()) {
        count = it->second.size();
    } else {
        std::cerr << "InkscapeApplication::document_window_count: Document not in map!" << std::endl;
    }

    return count;
}

/** Fix up a document if necessary (Only fixes that require GUI).
 */
void
InkscapeApplication::document_fix(InkscapeWindow* window)
{
    // Most fixes are handled when document is opened in SPDocument::createDoc().
    // But some require the GUI to be present. These are handled here.

    if (_with_gui) {

        SPDocument* document = window->get_document();

        // Perform a fixup pass for hrefs.
        if ( Inkscape::ResourceManager::getManager().fixupBrokenLinks(document) ) {
            Glib::ustring msg = _("Broken links have been changed to point to existing files.");
            SPDesktop* desktop = window->get_desktop();
            if (desktop != nullptr) {
                desktop->showInfoDialog(msg);
            }
        }

        // Fix dpi (pre-92 files).
        if ( sp_version_inside_range( document->getRoot()->version.inkscape, 0, 1, 0, 92 ) ) {
            sp_file_convert_dpi(document);
        }

        // Check for font substitutions, requires text to have been rendered.
        Inkscape::UI::Dialog::FontSubstitution::getInstance().checkFontSubstitutions(document);
    }
}


// Take an already open document and create a new window, adding window to document map.
InkscapeWindow*
InkscapeApplication::window_open(SPDocument* document)
{
    InkscapeWindow* window = new InkscapeWindow(document);
    // TODO Add window to application. (Instead of in InkscapeWindow constructor.)

    SPDesktop* desktop = window->get_desktop();

    // To be removed (add once per window)!
    INKSCAPE.add_document(document);

    // ActionContext should be removed once verbs are gone but we use it for now.
    Inkscape::ActionContext context = INKSCAPE.action_context_for_document(document);
    _active_selection = context.getSelection();
    _active_view      = context.getView();
    _active_document  = document;

    auto it = _documents.find(document);
    if (it != _documents.end()) {
        it->second.push_back(window);
    } else {
        std::cerr << "InkscapeApplication::open_window: Document not in map!" << std::endl;
    }

    document_fix(window); // May need flag to prevent this from being called more than once.

    return window;
}


// Close a window. Does not delete document.
void
InkscapeApplication::window_close(InkscapeWindow* window)
{
    // std::cout << "InkscapeApplication::close_window" << std::endl;
    // dump();

    if (window) {

        SPDocument* document = window->get_document();
        if (document) {

            // To be removed (remove once per window)!
            bool last = INKSCAPE.remove_document(document);

            _active_selection = nullptr;
            _active_view      = nullptr;
            _active_document  = nullptr;

            // Remove window from document map.
            auto it = _documents.find(document);
            if (it != _documents.end()) {
                auto it2 = std::find(it->second.begin(), it->second.end(), window);
                if (it2 != it->second.end()) {
                    it->second.erase(it2);
                    delete window; // Results in call to SPDesktop::destroy()
                } else {
                    std::cerr << "ConcreteInkscapeApplication<T>::close_window: window not found!" << std::endl;
                }
            } else {
                std::cerr << "ConcreteInkscapeApplication<T>::close_window: document not in map!" << std::endl;
            }
        } else {
            std::cerr << "ConcreteInkscapeApplication<T>::close_window: No document!" << std::endl;
        }

    } else {
        std::cerr << "ConcreteInkscapeApplication<T>::close_window: No window!" << std::endl;
    }

    // dump();
}


/** Update windows in response to:
 *  - New active window
 *  - Document change
 *  - Selection change
 */
void
InkscapeApplication::windows_update(SPDocument* document)
{
    // Find windows:
    auto it = _documents.find( document );
    if (it != _documents.end()) {
        std::vector<InkscapeWindow*> windows = it->second;
        // std::cout << "InkscapeApplication::update_windows: windows size: " << windows.size() << std::endl;
        // Loop over InkscapeWindows.
        // Loop over DialogWindows. TBD
    } else {
        // std::cout << "InkscapeApplication::update_windows: no windows found" << std::endl;
    }
}

/** Debug function
 */
void
InkscapeApplication::dump()
{
    std::cout << "InkscapeApplication::dump()" << std::endl;
    std::cout << "  Documents: " << _documents.size() << std::endl;
    for (auto i : _documents) {
        std::cout << "    Document: " << (i.first->getDocumentName()?i.first->getDocumentName():"unnamed") << std::endl;
        for (auto j : i.second) {
            std::cout << "      Window: " << j->get_title() << std::endl;
        }
    }
}


template<class T>
ConcreteInkscapeApplication<T>&
ConcreteInkscapeApplication<T>::get_instance()
{
    static ConcreteInkscapeApplication<T> instance;
    return instance;
}

template<class T>
ConcreteInkscapeApplication<T>::ConcreteInkscapeApplication()
    : T("org.inkscape.application.with_gui",
                       Gio::APPLICATION_HANDLES_OPEN | // Use default file opening.
                       Gio::APPLICATION_CAN_OVERRIDE_APP_ID ) // Allows different instances of
                                                              // Inkscape to run at same time using
                                                              // --gapplication-app-id (useful for
                                                              // debugging different versions of
                                                              // Inkscape).
    , InkscapeApplication()
{

    // ==================== Initializations =====================
    // Garbage Collector
    Inkscape::GC::init();

#ifdef ENABLE_NLS
    // Native Language Support (shouldn't this always be used?).
    Inkscape::initialize_gettext();
#endif

    // Don't set application name for now. We don't use it anywhere but
    // it overrides the name used for adding recently opened files and breaks the Gtk::RecentFilter
    // Glib::set_application_name(N_("Inkscape - A Vector Drawing Program"));  // After gettext() init.

    // ======================== Actions =========================
    add_actions_base(this);      // actions that are GUI independent
    add_actions_output(this);    // actions for file export
    add_actions_selection(this); // actions for object selection
    add_actions_transform(this); // actions for transforming selected objects

    // ====================== Command Line ======================

    // Will automatically handle character conversions.
    // Note: OPTION_TYPE_FILENAME => std::string, OPTION_TYPE_STRING => Glib::ustring.

    // Actions
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "actions",             'a', N_("Actions (with optional arguments), semi-colon separated."),     N_("ACTION(:ARG)[;ACTION(:ARG)]*"));
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "action-list",        '\0', N_("Actions: List available actions."),                                                  "");

    // Query
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "version",             'V', N_("Print: Inkscape version."),                                                          "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "extension-directory", 'x', N_("Print: Extensions directory."),                                                      "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "verb-list",          '\0', N_("Print: List verbs."),                                                                "");

    // Interface
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "with-gui",            'g', N_("GUI: With graphical interface."),                                                    "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "without-gui",         'G', N_("GUI: Console only."),                                                                "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "batch-process",      '\0', N_("GUI: Close window after processing actions (needed as some verbs require GUI)."),    "");

    // Open/Import
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "pdf-page",           '\0', N_("Open: PDF page to import"),         N_("PAGE"));
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "pdf-poppler",        '\0', N_("Use poppler when importing via commandline"),                                        "");
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "convert-dpi-method", '\0', N_("Open: Method used to convert pre-0.92 document dpi, if needed: [none|scale-viewbox|scale-document]."), "[...]");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "no-convert-text-baseline-spacing", 0, N_("Open: Do not fix pre-0.92 document's text baseline spacing on opening."), "");

    // Query - Geometry
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "query-id",            'I', N_("Query: ID(s) of object(s) to be queried."),                N_("OBJECT-ID[,OBJECT-ID]*"));
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "query-all",           'S', N_("Query: Print bounding boxes of all objects."),                                       "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "query-x",             'X', N_("Query: X coordinate of drawing or object (if specified by --query-id)."),            "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "query-y",             'Y', N_("Query: Y coordinate of drawing or object (if specified by --query-id)."),            "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "query-width",         'W', N_("Query: Width of drawing or object (if specified by --query-id)."),                   "");
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "query-height",        'H', N_("Query: Height of drawing or object (if specified by --query-id)."),                   "");

    // Processing
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "vacuum-defs",        '\0', N_("Process: Remove unused definitions from the <defs> section(s) of document."),        "");
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "select",             '\0', N_("Process: Select objects: comma separated list of IDs."),   N_("OBJECT-ID[,OBJECT-ID]*"));
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "verb",               '\0', N_("Process: Verb(s) to call when Inkscape opens."),               N_("VERB-ID[;VERB-ID]*"));
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "shell",              '\0', N_("Process: Start Inkscape in interactive shell mode."),                                 "");

    // Export - File and File Type
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "export-type",        '\0', N_("Export: File type:[svg,png,ps,psf,tex,emf,wmf,xaml]"),                          "[...]");
    this->add_main_option_entry(T::OPTION_TYPE_FILENAME, "export-file",         'o', N_("Export: File name"),                                              N_("EXPORT-FILENAME"));
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-overwrite",   '\0', N_("Export: Overwrite input file."),                                                     ""); // BSP

    //                                                                                                                                          B = PNG, S = SVG, P = PS/EPS/PDF
    // Export - Geometry
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "export-area",         'a', N_("Export: Area to export in SVG user units."),                          N_("x0:y0:x1:y1")); // BSP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-area-drawing", 'D', N_("Export: Area to export is drawing (not page)."),                                     ""); // BSP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-area-page",    'C', N_("Export: Area to export is page."),                                                   ""); // BSP
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "export-margin",      '\0', N_("Export: Margin around export area: units of page size for SVG, mm for PS/EPS/PDF."), ""); // xSP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-area-snap",   '\0', N_("Export: Snap the bitmap export area outwards to the nearest integer values."),       ""); // Bxx
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "export-width",        'w', N_("Export: Bitmap width in pixels (overrides --export-dpi)."),                 N_("WIDTH")); // Bxx
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "export-height",       'h', N_("Export: Bitmap height in pixels (overrides --export-dpi)."),               N_("HEIGHT")); // Bxx

    // Export - Options
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "export-id",           'i', N_("Export: ID(s) of object(s) to export."),                   N_("OBJECT-ID[;OBJECT-ID]*")); // BSP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-id-only",      'j', N_("Export: Hide all objects except object with ID selected by export-id."),             ""); // BSx
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-plain-svg",    'l', N_("Export: Remove items in the Inkscape namespace."),                                   ""); // xSx
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "export-dpi",          'd', N_("Export: Resolution for rasterization bitmaps and filters (default is 96)."),  N_("DPI")); // BxP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-ignore-filters", '\0', N_("Export: Render objects without filters instead of rasterizing. (PS/EPS/PDF)"),    ""); // xxP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-text-to-path", 'T', N_("Export: Convert text to paths. (PS/EPS/PDF/SVG)."),                                  ""); // xxP
    this->add_main_option_entry(T::OPTION_TYPE_INT,      "export-ps-level",    '\0', N_("Export: Postscript level (2 or 3). Default is 3."),                      N_("PS-Level")); // xxP
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "export-pdf-level",   '\0', N_("Export: PDF level (1.4 or 1.5)"),                                       N_("PDF-Level")); // xxP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-latex",       '\0', N_("Export: Export text separately to LaTeX file (PS/EPS/PDF). Include via \\input{file.tex}"), ""); // xxP
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "export-use-hints",    't', N_("Export: Use stored filename and DPI hints when exporting object selected by --export-id."), ""); // Bxx
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "export-background",   'b', N_("Export: Background color for exported bitmaps (any SVG color string)."),    N_("COLOR")); // Bxx
    this->add_main_option_entry(T::OPTION_TYPE_DOUBLE,   "export-background-opacity", 'y', N_("Export: Background opacity for exported bitmaps (either 0.0 to 1.0 or 1 to 255)."), N_("VALUE")); // Bxx

#ifdef WITH_YAML
    this->add_main_option_entry(T::OPTION_TYPE_FILENAME, "xverbs",             '\0', N_("Process: xverb command file."),                                   N_("XVERBS-FILENAME"));
#endif // WITH_YAML

#ifdef WITH_DBUS
    this->add_main_option_entry(T::OPTION_TYPE_BOOL,     "dbus-listen",        '\0', N_("D-Bus: Enter a listening loop for D-Bus messages in console mode."),                 "");
    this->add_main_option_entry(T::OPTION_TYPE_STRING,   "dbus-name",          '\0', N_("D-Bus: Specify the D-Bus name (default is 'org.inkscape')."),            N_("BUS-NAME"));
#endif // WITH_DBUS
    
    Gio::Application::signal_handle_local_options().connect(sigc::mem_fun(*this, &InkscapeApplication::on_handle_local_options));

    // This is normally called for us... but after the "handle_local_options" signal is emitted. If
    // we want to rely on actions for handling options, we need to call it here. This appears to
    // have no unwanted side-effect. It will also trigger the call to on_startup().
    T::register_application();
}

template<class T>
void
ConcreteInkscapeApplication<T>::on_startup()
{
    T::on_startup();
}

// Here are things that should be in on_startup() but cannot be as we don't set _with_gui until
// on_handle_local_options() is called.
template<>
void
ConcreteInkscapeApplication<Gio::Application>::on_startup2()
{
    Inkscape::Application::create(nullptr, false);
}

template<>
void
ConcreteInkscapeApplication<Gtk::Application>::on_startup2()
{
    // This should be completely rewritten.
    Inkscape::Application::create(nullptr, _with_gui); // argv appears to not be used.

    if (!_with_gui) {
        return;
    }

    // ======================= Actions (GUI) ======================
    add_action("new",    sigc::mem_fun(*this, &InkscapeApplication::on_new   ));
    add_action("quit",   sigc::mem_fun(*this, &InkscapeApplication::on_quit  ));

    // ========================= GUI Init =========================
    Gtk::Window::set_default_icon_name("org.inkscape.Inkscape");
    Inkscape::UI::Widget::Panel::prep();

    // ========================= Builder ==========================
    // App menus deprecated in 3.32. This whole block of code should be
    // removed after confirming this code isn't required.
    _builder = Gtk::Builder::create();

    Glib::ustring app_builder_file = get_filename(UIS, "inkscape-application.xml");

    try
    {
        _builder->add_from_file(app_builder_file);
    }
    catch (const Glib::Error& ex)
    {
        std::cerr << "InkscapeApplication: " << app_builder_file << " file not read! " << ex.what() << std::endl;
    }

    auto object = _builder->get_object("menu-application");
    auto menu = Glib::RefPtr<Gio::Menu>::cast_dynamic(object);
    if (!menu) {
        std::cerr << "InkscapeApplication: failed to load application menu!" << std::endl;
    } else {
        // set_app_menu(menu);
    }
}

/** We should not create a window if T is Gio::Applicaton.
*/
template<class T>
SPDesktop*
ConcreteInkscapeApplication<T>::create_window(const Glib::RefPtr<Gio::File>& file,
                                              bool add_to_recent,
                                              bool replace_empty)
{
    std::cerr << "ConcreteInkscapeApplication<T>::create_window: Should not be called!";
    return nullptr;
}


/** Create a window given a Gio::File. This is what most functions should call.
    The booleans are only false when opening a help file.
*/
template<>
SPDesktop*
ConcreteInkscapeApplication<Gtk::Application>::create_window(const Glib::RefPtr<Gio::File>& file,
                                                             bool add_to_recent,
                                                             bool replace_empty)
{
    SPDesktop* desktop = nullptr;

    if (file) {
        SPDocument* document = document_open (file);
        if (document) {

            if (add_to_recent) {
                auto recentmanager = Gtk::RecentManager::get_default();
                recentmanager->add_item (file->get_uri());
            }

            // TODO Remove this code... handle document replacement elsewhere.
            SPDocument* old_document = _active_document;
            if (replace_empty && old_document && old_document->getVirgin()) {
                // virgin == true => an empty document (template).

                // Is there a better place for this? It requires GUI.
                document->ensureUpToDate(); // TODO this will trigger broken line warnings, etc.

                InkscapeWindow* window = dynamic_cast<InkscapeWindow*>(get_active_window());
                if (window) {
                    document_swap (window, document);

                    // Delete old document if no longer attached to any window.
                    auto it = _documents.find (old_document);
                    if (it != _documents.end()) {
                        if (it->second.size() == 0) {
                            document_close (old_document);
                        }
                    }

                    document->emitResizedSignal(document->getWidth().value("px"), document->getHeight().value("px"));
                    desktop = window->get_desktop();
                } else {
                    std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to find active window!" << std::endl;
                }
            } else {
                InkscapeWindow* window = window_open (document);
                desktop = window->get_desktop();
            }


        } else {
            std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to load: "
                      << file->get_parse_name() << std::endl;
        }

    } else {
        std::string Template =
            Inkscape::IO::Resource::get_filename(Inkscape::IO::Resource::TEMPLATES, "default.svg", true);
        SPDocument* document = document_new (Template);
        if (document) {
            InkscapeWindow* window = window_open (document);
            desktop = window->get_desktop();
        } else {
            std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to open default template! " << Template << std::endl;
        }
    }

    if (desktop) {
        _active_document = desktop->getDocument();
#ifdef WITH_DBUS
        Inkscape::Extension::Dbus::dbus_init_desktop_interface(desktop);
#endif
    } else {
        std::cerr << "ConcreteInkscapeApplication<T>::create_window: Failed to create desktop!" << std::endl;
    }

    return (desktop); // Temp: Need to track desktop for shell mode.
}

/** No need to destroy window if T is Gio::Application.
 */
template<class T>
bool
ConcreteInkscapeApplication<T>::destroy_window(InkscapeWindow* window)
{
    std::cerr << "ConcreteInkscapeApplication<T>::destroy_window: Should not be called!";
    return false;
}

/** Destroy a window. Aborts if document needs saving.
 *  Returns true if window destroyed.
 */
template<>
bool
ConcreteInkscapeApplication<Gtk::Application>::destroy_window(InkscapeWindow* window)
{
    SPDocument* document = window->get_document();

    // Remove document if no windows left.
    if (document) {
        auto it = _documents.find(document);
        if (it != _documents.end()) {

            // If only one window for document:
            if (it->second.size() == 1) {
                // Check if document needs saving.
                bool abort = window->get_desktop_widget()->shutdown();
                if (abort) {
                    return false;
                }
            }

            window_close(window);

            if (it->second.size() == 0) {
                document_close (document);
            }

        } else {
            std::cerr << "ConcreteInkscapeApplication<Gtk::Application>::destroy_window: Could not find document!" << std::endl;
        }
    }

    // Debug
    // auto windows = get_windows();
    // std::cout << "destroy_windows: app windows size: " << windows.size() << std::endl;

    return true;
}

/* Close all windows and exit.
**/
template<class T>
void
ConcreteInkscapeApplication<T>::destroy_all()
{
    std::cerr << "ConcreteInkscapeApplication<T>::destroy_all: Should not be called!";
}

template<>
void
ConcreteInkscapeApplication<Gtk::Application>::destroy_all()
{
    while (_documents.size() != 0) {
        auto it = _documents.begin();
        while (it->second.size() != 0) {
            auto it2 = it->second.begin();
            if (!destroy_window (*it2)) return; // If destroy aborted, we need to stop exit.
        }
    }
}


// Open document window with default document. Either this or on_open() is called.
template<class T>
void
ConcreteInkscapeApplication<T>::on_activate()
{
    on_startup2();

    if (_with_gui) {
        if (_use_shell) {
            shell(); // Shell will create its own windows.
        } else {
            create_window();
        }
    } else {
        std::cerr << "InkscapeApplication::on_activate:  Without GUI" << std::endl;
        if (_use_shell) {
            shell2();
        }
        // Create blank document?
    }
}

// Open document window for each file. Either this or on_activate() is called.
// type_vec_files == std::vector<Glib::RefPtr<Gio::File> >
template<class T>
void
ConcreteInkscapeApplication<T>::on_open(const Gio::Application::type_vec_files& files, const Glib::ustring& hint)
{
    on_startup2();
    if(_pdf_poppler)
        INKSCAPE.set_pdf_poppler(_pdf_poppler);
    if(_pdf_page)
        INKSCAPE.set_pdf_page(_pdf_page);

    for (auto file : files) {
        // Open file
        SPDocument *document = document_open (file);
        if (!document) continue;

        // Add to Inkscape::Application...
        INKSCAPE.add_document(document);
        // ActionContext should be removed once verbs are gone but we use it for now.
        Inkscape::ActionContext context = INKSCAPE.action_context_for_document(document);
        _active_document  = document;
        _active_selection = context.getSelection();
        _active_view      = context.getView();

        document->ensureUpToDate(); // Or queries don't work!

        // process_file(file);
        for (auto action: _command_line_actions) {
            Gio::Application::activate_action( action.first, action.second );
        }

        if (_use_shell) {
            shell2();
        } else {
            // Save... can't use action yet.
            _file_export.do_export(document, file->get_path());
        }

        _active_document = nullptr;
        _active_selection = nullptr;
        _active_view = nullptr;

        // Close file
        INKSCAPE.remove_document(document);

        document_close (document);
    }
}

// Open document window for each file. Either this or on_activate() is called.
// type_vec_files == std::vector<Glib::RefPtr<Gio::File> >
template<>
void
ConcreteInkscapeApplication<Gtk::Application>::on_open(const Gio::Application::type_vec_files& files, const Glib::ustring& hint)
{
    on_startup2();
    if(_pdf_poppler)
        INKSCAPE.set_pdf_poppler(_pdf_poppler);
    if(_pdf_page)
        INKSCAPE.set_pdf_page(_pdf_page);

    for (auto file : files) {
        if (_with_gui) {
            // Create a window for each file.
            SPDesktop* desktop = create_window(file);

            // Process each file.
            for (auto action: _command_line_actions) {
		    Gio::Application::activate_action( action.first, action.second );
            }

            // Close window after we're done with file. This may not be the best way...
            // but we need to rewrite most of the window handling code so do this for now.
            if (_batch_process) {
                std::vector<Gtk::Window*> windows = get_windows();
                remove_window(*windows[0]);  // There should be only one window (added in InkscapeWindow constructor).
                                             // Eventually create_window() should return a pointer to the window, not the desktop.
            }

        } else {

            // Open file
            SPDocument *document = document_open (file);
            if (!document) continue;

            // Add to Inkscape::Application...
            INKSCAPE.add_document(document);
            // ActionContext should be removed once verbs are gone but we use it for now.
            Inkscape::ActionContext context = INKSCAPE.action_context_for_document(document);
            _active_document  = document;
            _active_selection = context.getSelection();
            _active_view      = context.getView();

            document->ensureUpToDate(); // Or queries don't work!

            // process_file(file);
            for (auto action: _command_line_actions) {
		    Gio::Application::activate_action( action.first, action.second );
            }

            if (_use_shell) {
                shell2();
            } else {
                // Save... can't use action yet.
                _file_export.do_export(document, file->get_path());
            }

            _active_document = nullptr;
            _active_selection = nullptr;
            _active_view = nullptr;

            // Close file
            INKSCAPE.remove_document(document);

            document_close (document);
        }
    }
}

template<class T>
void
ConcreteInkscapeApplication<T>::parse_actions(const Glib::ustring& input, action_vector_t& action_vector)
{
    // Split action list
    std::vector<Glib::ustring> tokens = Glib::Regex::split_simple("\\s*;\\s*", input);
    for (auto token : tokens) {
        std::vector<Glib::ustring> tokens2 = Glib::Regex::split_simple("\\s*:\\s*", token);
        std::string action;
        std::string value;
        if (tokens2.size() > 0) {
            action = tokens2[0];
        }
        if (tokens2.size() > 1) {
            value = tokens2[1];
        }

        Glib::RefPtr<Gio::Action> action_ptr = Gio::Application::lookup_action(action);
        if (action_ptr) {
            // Doesn't seem to be a way to test this using the C++ binding without Glib-CRITICAL errors.
            const  GVariantType* gtype = g_action_get_parameter_type(action_ptr->gobj());
            if (gtype) {
                // With value.
                Glib::VariantType type = action_ptr->get_parameter_type();
                if (type.get_string() == "b") {
                    bool b = false;
                    if (value == "0" || value == "true" || value.empty()) {
                        b = true;
                    } else if (value =="1" || value == "false") {
                        b = false;
                    } else {
                        std::cerr << "InkscapeApplication::parse_actions: Invalid boolean value: " << action << ":" << value << std::endl;
                    }
                    action_vector.push_back(
                        std::make_pair( action, Glib::Variant<bool>::create(b)));
                } else if (type.get_string() == "i") {
                    action_vector.push_back(
                        std::make_pair( action, Glib::Variant<int>::create(std::stoi(value))));
                } else if (type.get_string() == "d") {
                    action_vector.push_back(
                        std::make_pair( action, Glib::Variant<double>::create(std::stod(value))));
                } else if (type.get_string() == "s") {
                    action_vector.push_back(
                        std::make_pair( action, Glib::Variant<Glib::ustring>::create(value) ));
                } else {
                    std::cerr << "InkscapeApplication::parse_actions: unhandled action value: "
                              << action << ": " << type.get_string() << std::endl;
                }
            } else {
                // Stateless (i.e. no value).
                action_vector.push_back( std::make_pair( action, Glib::VariantBase() ) );
            }
        } else {
            // Assume a verb
            // std::cerr << "InkscapeApplication::parse_actions: '"
            //           << action << "' is not a valid action! Assuming verb!" << std::endl;
            action_vector.push_back(
                std::make_pair("verb", Glib::Variant<Glib::ustring>::create(action)));
        }
    }
}

// Interactively trigger actions. This is a travesty! Due to most verbs requiring a desktop we must
// create one even in shell mode!
template<class T>
void
ConcreteInkscapeApplication<T>::shell()
{
    std::cout << "Inkscape interactive shell mode. Type 'quit' to quit." << std::endl;
    std::cout << " Input of the form:" << std::endl;
    std::cout << "> filename action1:arg1; action2:arg2; verb1; verb2; ..." << std::endl;

    std::string input;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, input);

        if (input == "quit") break;

        // Get filename which must be first and separated by space (and not contain ':' or ';').
        // Regex works on Glib::ustrings and will give bad results if one tries to use std::string.
        Glib::ustring input_u = input;
        Glib::ustring filename;
        Glib::RefPtr<Glib::Regex> regex = Glib::Regex::create("^\\s*([^:;\\s]+)\\s+(.*)");
        Glib::MatchInfo match_info;
        regex->match(input_u, match_info);
        if (match_info.matches()) {
            filename = match_info.fetch(1);
            input_u = match_info.fetch(2);
        } else {
            std::cerr << "InkscapeApplication::shell: Failed to find file in |"
                      << input << "|" << std::endl;
        }

        // Create desktop.
        SPDesktop* desktop = nullptr;
        if (!filename.empty()) {
            Glib::RefPtr<Gio::File> file = Gio::File::create_for_path(filename);
            desktop = create_window(file);
        }

        // Find and execute actions (verbs).
        action_vector_t action_vector;
        parse_actions(input_u, action_vector);
        for (auto action: action_vector) {
		Gio::Application::activate_action( action.first, action.second );
        }

        if (desktop) {
            desktop->destroy();
        }
    }

    T::quit(); // Must quit or segfault. (Might be fixed by using desktop->getToplevel()->hide() above.);
}

// Once we don't need to create a window just to process verbs!
template<class T>
void
ConcreteInkscapeApplication<T>::shell2()
{
    std::cout << "Inkscape interactive shell mode. Type 'quit' to quit." << std::endl;
    std::cout << " Input of the form:" << std::endl;
    std::cout << "> action1:arg1; action2;arg2; verb1; verb2; ..." << std::endl;
    std::cout << "Only verbs that don't require a desktop may be used." << std::endl;

    std::string input;
    while (true) {
        std::cout << "> ";
        std::cin >> input;
        if (input == "quit") break;
        action_vector_t action_vector;
        parse_actions(input, action_vector);
        for (auto action: action_vector) {
            T::activate_action( action.first, action.second );
        }
    }
}


// ========================= Callbacks ==========================

/*
 * Handle command line options.
 *
 * Options are processed in the order they appear in this function.
 * We process in order: Print -> GUI -> Open -> Query -> Process -> Export.
 * For each file without GUI: Open -> Query -> Process -> Export 
 * More flexible processing can be done via actions or xverbs.
 */
template<class T>
int
ConcreteInkscapeApplication<T>::on_handle_local_options(const Glib::RefPtr<Glib::VariantDict>& options)
{
    if (!options) {
        std::cerr << "InkscapeApplication::on_handle_local_options: options is null!" << std::endl;
        return -1; // Keep going
    }

    // ===================== QUERY =====================
    // These are processed first as they result in immediate program termination.
    if (options->contains("version")) {
        T::activate_action("inkscape-version");
        return EXIT_SUCCESS;
    }

    if (options->contains("extension-directory")) {
        T::activate_action("extension-directory");
        return EXIT_SUCCESS;
    }

    if (options->contains("verb-list")) {
        T::activate_action("verb-list");
        return EXIT_SUCCESS;
    }

    if (options->contains("action-list")) {
        std::vector<Glib::ustring> actions = T::list_actions();
        std::sort(actions.begin(), actions.end());
        for (auto action : actions) {
            std::cout << action << std::endl;
        }
        return EXIT_SUCCESS;
    }

    // For options without arguments.
    auto base = Glib::VariantBase();

    // ================== GUI and Shell ================
    if (options->contains("without-gui"))    _with_gui = false;
    if (options->contains("with-gui"))       _with_gui = true;
    if (options->contains("batch-process"))  _batch_process = true;
    if (options->contains("shell"))          _use_shell = true;

    // Some options should preclude using gui!
    if (options->contains("query-id")         ||
        options->contains("query-x")          ||
        options->contains("query-all")        ||
        options->contains("query-y")          ||
        options->contains("query-width")      ||
        options->contains("query-height")     ||
        options->contains("export-file")      ||
        options->contains("export-type")      ||
        options->contains("export-overwrite") ||
        options->contains("export-id")        ||
        options->contains("export-plain-svg") ||
        options->contains("export-text-to_path")
        ) {
        _with_gui = false;
    }

    // ==================== ACTIONS ====================
    // Actions as an argument string: e.g.: --actions="query-id:rect1;query-x".
    // Actions will be processed in order that they are given in argument.
    Glib::ustring actions;
    if (options->contains("actions")) {
        options->lookup_value("actions", actions);
        parse_actions(actions, _command_line_actions);
    }


    // ================= OPEN/IMPORT ===================

    if (options->contains("pdf-poppler")) {
        _pdf_poppler = true;
    }
    if (options->contains("pdf-page")) {   // Maybe useful for other file types?
        int page = 0;
        options->lookup_value("pdf-page", page);
        _pdf_page = page;
    }

    if (options->contains("convert-dpi-method")) {
        Glib::ustring method;
        options->lookup_value("convert-dpi-method", method);
        if (!method.empty()) {
            _command_line_actions.push_back(
                std::make_pair("convert-dpi-method", Glib::Variant<Glib::ustring>::create(method)));
        }
    }

    if (options->contains("no-convert-text-baseline-spacing")) _command_line_actions.push_back(std::make_pair("no-convert-baseline", base));


    // ===================== QUERY =====================

    // 'query-id' should be processed first! Can be a comma separated list.
    if (options->contains("query-id")) {
        Glib::ustring query_id;
        options->lookup_value("query-id", query_id);
        if (!query_id.empty()) {
            _command_line_actions.push_back(
                std::make_pair("select-via-id", Glib::Variant<Glib::ustring>::create(query_id)));
        }
    }

    if (options->contains("query-all"))    _command_line_actions.push_back(std::make_pair("query-all",   base));
    if (options->contains("query-x"))      _command_line_actions.push_back(std::make_pair("query-x",     base));
    if (options->contains("query-y"))      _command_line_actions.push_back(std::make_pair("query-y",     base));
    if (options->contains("query-width"))  _command_line_actions.push_back(std::make_pair("query-width", base));
    if (options->contains("query-height")) _command_line_actions.push_back(std::make_pair("query-height",base));


    // =================== PROCESS =====================

    // Note: this won't work with --verb="FileSave,FileClose" unless some additional verb changes the file. FIXME
    // One can use --verb="FileVacuum,FileSave,FileClose".
    if (options->contains("vacuum-defs"))  _command_line_actions.push_back(std::make_pair("vacuum-defs", base));

    if (options->contains("select")) {
        Glib::ustring select;
        options->lookup_value("select", select);
        if (!select.empty()) {
            _command_line_actions.push_back(
                std::make_pair("select", Glib::Variant<Glib::ustring>::create(select)));
        }
    }

    if (options->contains("verb")) {
        Glib::ustring verb;
        options->lookup_value("verb", verb);
        if (!verb.empty()) {
            _command_line_actions.push_back(
                std::make_pair("verb", Glib::Variant<Glib::ustring>::create(verb)));
        }
    }


    // ==================== EXPORT =====================
    if (options->contains("export-file")) {
        options->lookup_value("export-file",      _file_export.export_filename);
    }

    if (options->contains("export-type")) {
        options->lookup_value("export-type",      _file_export.export_type);
    }

    if (options->contains("export-overwrite"))    _file_export.export_overwrite    = true;

    // Export - Geometry
    if (options->contains("export-area")) {
        options->lookup_value("export-area",      _file_export.export_area);
    }

    if (options->contains("export-area-drawing")) _file_export.export_area_drawing = true;
    if (options->contains("export-area-page"))    _file_export.export_area_page    = true;

    if (options->contains("export-margin")) {
        options->lookup_value("export-margin",    _file_export.export_margin);
    }

    if (options->contains("export-area-snap"))    _file_export.export_area_snap    = true;

    if (options->contains("export-width")) {
        options->lookup_value("export-width",     _file_export.export_width);
    }

    if (options->contains("export-height")) {
        options->lookup_value("export-height",    _file_export.export_height);
    }

    // Export - Options
    if (options->contains("export-id")) {
        options->lookup_value("export-id",        _file_export.export_id);
    }

    if (options->contains("export-id-only"))      _file_export.export_id_only     = true;
    if (options->contains("export-plain-svg"))    _file_export.export_plain_svg      = true;

    if (options->contains("export-dpi")) {
        options->lookup_value("export-dpi",       _file_export.export_dpi);
    }

    if (options->contains("export-ignore-filters")) _file_export.export_ignore_filters = true;
    if (options->contains("export-text-to-path"))   _file_export.export_text_to_path   = true;

    if (options->contains("export-ps-level")) {
        options->lookup_value("export-ps-level",  _file_export.export_ps_level);
    }

    if (options->contains("export-pdf-level")) {
        options->lookup_value("export-pdf-level", _file_export.export_pdf_level);
    }

    if (options->contains("export-latex"))        _file_export.export_latex       = true;
    if (options->contains("export-use-hints"))    _file_export.export_use_hints   = true;

    if (options->contains("export-background")) {
        options->lookup_value("export-background",_file_export.export_background);
    }

    if (options->contains("export-background-opacity")) {
        options->lookup_value("export-background-opacity", _file_export.export_background_opacity);
    }


    // ==================== D-BUS ======================

#ifdef WITH_DBUS
    // Before initializing extensions, we must set the DBus bus name if required
    if (options->contains("dbus-listen")) {
        std::string dbus_name;
        options->lookup_value("dbus-name", dbus_name);
        if (!dbus_name.empty()) {
            Inkscape::Extension::Dbus::dbus_set_bus_name(dbus_name.c_str());
        }
    }
#endif

    return -1; // Keep going
}

//   ========================  Actions  =========================

template<class T>
void
ConcreteInkscapeApplication<T>::on_new()
{
    create_window();
}

template<class T> void ConcreteInkscapeApplication<T>::on_quit(){ T::quit(); }

template<>
void
ConcreteInkscapeApplication<Gtk::Application>::on_quit()
{
    // Delete all windows (quit() doesn't do this).
    std::vector<Gtk::Window*> windows = get_windows();
    for (auto window: windows) {
        // Do something
    }

    quit();
}

template class ConcreteInkscapeApplication<Gio::Application>;
template class ConcreteInkscapeApplication<Gtk::Application>;

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :