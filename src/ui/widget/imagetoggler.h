// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEEN_UI_DIALOG_IMAGETOGGLER_H
#define SEEN_UI_DIALOG_IMAGETOGGLER_H
/*
 * Authors:
 *   Jon A. Cruz
 *   Johan B. C. Engelen
 *
 * Copyright (C) 2006-2008 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <gtkmm/cellrendererpixbuf.h>
#include <gtkmm/widget.h>
#include <glibmm/property.h>

namespace Inkscape {
namespace UI {
namespace Widget {

class ImageToggler : public Gtk::CellRenderer {
public:
    ImageToggler( char const *on, char const *off);
    ~ImageToggler() override = default;;

    sigc::signal<void (const Glib::ustring&)> signal_toggled() { return _signal_toggled;}
    sigc::signal<void (GdkEvent const *)> signal_pre_toggle()  { return _signal_pre_toggle; }

    Glib::PropertyProxy<bool> property_active() { return _property_active.get_proxy(); }
    Glib::PropertyProxy<bool> property_activatable() { return _property_activatable.get_proxy(); }
    Glib::PropertyProxy<bool> property_gossamer() { return _property_gossamer.get_proxy(); }
    Glib::PropertyProxy<std::string> property_active_icon() { return _property_active_icon.get_proxy(); }
    Glib::PropertyProxy< Glib::RefPtr<Gdk::Pixbuf> > property_pixbuf_on();
    Glib::PropertyProxy< Glib::RefPtr<Gdk::Pixbuf> > property_pixbuf_off();

    void set_active(bool active = true);

protected:
    void render_vfunc( const Cairo::RefPtr<Cairo::Context>& cr,
                               Gtk::Widget& widget,
                               const Gdk::Rectangle& background_area,
                               const Gdk::Rectangle& cell_area,
                               Gtk::CellRendererState flags ) override;

    void get_preferred_width_vfunc(Gtk::Widget& widget,
                                           int& min_w,
                                           int& nat_w) const override;
    
    void get_preferred_height_vfunc(Gtk::Widget& widget,
                                            int& min_h,
                                            int& nat_h) const override;

    bool activate_vfunc(GdkEvent *event,
                                Gtk::Widget &widget,
                                const Glib::ustring &path,
                                const Gdk::Rectangle &background_area,
                                const Gdk::Rectangle &cell_area,
                                Gtk::CellRendererState flags) override;


private:
    int _size;
    Glib::ustring _pixOnName;
    Glib::ustring _pixOffName;
    bool _active = false;
    Glib::Property<bool> _property_active;
    Glib::Property<bool> _property_activatable;
    Glib::Property<bool> _property_gossamer;
    Glib::Property< Glib::RefPtr<Gdk::Pixbuf> > _property_pixbuf_on;
    Glib::Property< Glib::RefPtr<Gdk::Pixbuf> > _property_pixbuf_off;
    Glib::Property<std::string> _property_active_icon;
    std::map<const std::string, Glib::RefPtr<Gdk::Pixbuf>> _icon_cache;

    sigc::signal<void (const Glib::ustring&)> _signal_toggled;
    sigc::signal<void (GdkEvent const *)> _signal_pre_toggle;
};



} // namespace Widget
} // namespace UI
} // namespace Inkscape


#endif // SEEN_UI_DIALOG_IMAGETOGGLER_H

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
