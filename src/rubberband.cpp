#define __RUBBERBAND_C__

/**
 * \file src/rubberband.cpp
 * \brief Rubberbanding selector
 *
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "display/sodipodi-ctrlrect.h"
#include "desktop.h"
#include "inkscape.h"
#include "desktop-handles.h"
#include "rubberband.h"
#include "display/canvas-bpath.h"
#include "display/curve.h"
#include "libnr/nr-point.h"

Inkscape::Rubberband *Inkscape::Rubberband::_instance = NULL;

Inkscape::Rubberband::Rubberband()
    : _desktop(SP_ACTIVE_DESKTOP), _rect(NULL), _touchpath(NULL), _started(false)
{
    _points.clear();
    _mode = RUBBERBAND_MODE_RECT;
    _touchpath_curve = sp_curve_new_sized(2000);
}

void Inkscape::Rubberband::delete_canvas_items()
{
    if (_rect) {
        GtkObject *temp = _rect;
        _rect = NULL;
        gtk_object_destroy(temp);
    }
    if (_touchpath) {
        GtkObject *temp = _touchpath;
        _touchpath = NULL;
        gtk_object_destroy(temp);
    }
}


void Inkscape::Rubberband::start(SPDesktop *d, NR::Point const &p)
{
    _points.clear();
    sp_curve_reset(_touchpath_curve);
    delete_canvas_items();
    _desktop = d;
    _start = p;
    _started = true;
    _points.push_back(_desktop->d2w(p));
    sp_curve_moveto(_touchpath_curve, p);

    sp_canvas_force_full_redraw_after_interruptions(_desktop->canvas, 5);
}

void Inkscape::Rubberband::stop()
{
    _started = false;
    _mode = RUBBERBAND_MODE_RECT; // restore the default

    _points.clear();
    sp_curve_reset(_touchpath_curve);

    delete_canvas_items();

    if (_desktop)
        sp_canvas_end_forced_full_redraws(_desktop->canvas);
}

void Inkscape::Rubberband::move(NR::Point const &p)
{
    if (!_started) 
        return;

    _end = p;
    _desktop->scroll_to_point(&p);
    sp_curve_lineto (_touchpath_curve, p);

    NR::Point next = _desktop->d2w(p);
    // we want the points to be at most 0.5 screen pixels apart,
    // so that we don't lose anything small;
    // if they are farther apart, we interpolate more points
    if (_points.size() > 0 && NR::L2(next-_points.back()) > 0.5) {
        NR::Point prev = _points.back();
        int subdiv = 2 * (int) round(NR::L2(next-prev) + 0.5);
        for (int i = 1; i <= subdiv; i ++) {
            _points.push_back(prev + ((double)i/subdiv) * (next - prev));
        }
    } else {
        _points.push_back(next);
    }

    if (_mode == RUBBERBAND_MODE_RECT) {
        if (_rect == NULL) {
            _rect = static_cast<CtrlRect *>(sp_canvas_item_new(sp_desktop_controls(_desktop), SP_TYPE_CTRLRECT, NULL));
        }
        _rect->setRectangle(NR::Rect(_start, _end));

        sp_canvas_item_show(_rect);
        if (_touchpath)
            sp_canvas_item_hide(_touchpath);

    } else if (_mode == RUBBERBAND_MODE_TOUCHPATH) {
        if (_touchpath == NULL) {
            _touchpath = sp_canvas_bpath_new(sp_desktop_sketch(_desktop), NULL);
            sp_canvas_bpath_set_stroke(SP_CANVAS_BPATH(_touchpath), 0xff0000ff, 1.0, SP_STROKE_LINEJOIN_MITER, SP_STROKE_LINECAP_BUTT);
            sp_canvas_bpath_set_fill(SP_CANVAS_BPATH(_touchpath), 0, SP_WIND_RULE_NONZERO);
        }
        sp_canvas_bpath_set_bpath(SP_CANVAS_BPATH(_touchpath), _touchpath_curve);

        sp_canvas_item_show(_touchpath);
        if (_rect)
            sp_canvas_item_hide(_rect);
    }
}

void Inkscape::Rubberband::setMode(int mode) 
{
    _mode = mode;
}

NR::Maybe<NR::Rect> Inkscape::Rubberband::getRectangle() const
{
    if (!_started) {
        return NR::Nothing();
    }

    return NR::Rect(_start, _end);
}

Inkscape::Rubberband *Inkscape::Rubberband::get()
{
    if (_instance == NULL) {
        _instance = new Inkscape::Rubberband;
    }

    return _instance;
}

bool Inkscape::Rubberband::is_started()
{
    return _started;
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
