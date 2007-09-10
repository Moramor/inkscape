/**
 * \brief A desktop dock pane to dock dialogs, a custom wrapper around gdl-dock.
 *
 * Author:
 *   Gustav Broberg <broberg@kth.se>
 *
 * Copyright (C) 2007 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_UI_WIDGET_DOCK_H
#define INKSCAPE_UI_WIDGET_DOCK_H

#include <gtkmm/scrolledwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/paned.h>

#include <list>

#include "ui/widget/dock-item.h"

#include "libgdl/libgdl.h"

namespace Inkscape {
namespace UI {
namespace Widget {

class Dock {

public:

    Dock(Gtk::Orientation orientation=Gtk::ORIENTATION_VERTICAL);
    ~Dock();

    void addItem(DockItem& item, DockItem::Placement placement);

    Gtk::Widget& getWidget();     //< return the top widget
    Gtk::Paned *getParentPaned();

    Gtk::Paned *getPaned();

    bool isEmpty() const;     //< true iff none of the dock's items are in state != UNATTACHED
    bool hasIconifiedItems() const;

    Glib::SignalProxy0<void> signal_layout_changed();

    void hide();
    void show();

    /** Toggle size of dock between the previous dimensions and the ones sent as parameters */
    void toggleDockable(int width=0, int height=0);

protected:

    std::list<const DockItem *> _dock_items;   //< added dock items

    /** Interface widgets, will be packed like 
     * _scrolled_window -> (_dock_box -> (_paned -> (_dock -> _filler) | _dock_bar))
     */
    Gtk::Box *_dock_box;
    Gtk::Paned* _paned;
    GdlDock *_gdl_dock;
    GdlDockBar *_gdl_dock_bar;
    Gtk::VBox _filler;
    Gtk::ScrolledWindow *_scrolled_window;

    /** Internal signal handlers */
    void _onLayoutChanged();

    /** GdlDock signal proxy structures */
    static const Glib::SignalProxyInfo _signal_layout_changed_proxy;

    /** Standard widths */
    static const int _default_empty_width;
    static const int _default_dock_bar_width;
};

} // namespace Widget
} // namespace UI
} // namespace Inkscape

#endif //INKSCAPE_UI_DIALOG_BEHAVIOUR_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 

