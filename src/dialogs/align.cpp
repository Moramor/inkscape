#define __SP_QUICK_ALIGN_C__

/**
 * \brief  Object align dialog
 *
 * Authors:
 *   Aubanel MONNIER <aubi@libertysurf.fr>
 *   Frank Felfe <innerspace@iname.com>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 1999-2002 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */


#include <config.h>
#include <glibmm/i18n.h>

#include "dialogs/align.h"
#include "dialogs/docker.h"
#include "dialogs/dockable.h"

#include "dialogs/unclump.h"

#include <gtkmm/buttonbox.h>
#include <gtkmm/button.h>
#include <gtkmm/table.h>
#include <gtkmm/image.h>
#include <gtkmm/frame.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/tooltips.h>

#include <sigc++/sigc++.h>

#include <list>
#include <map>
#include <vector>

#include "libnr/nr-macros.h"
#include "libnr/nr-point-fns.h"
#include "libnr/nr-rect.h"

#include "widgets/icon.h"
#include "sp-item.h"
#include "inkscape.h"
#include "document.h"
#include "selection.h"
#include "desktop-handles.h"
#include "desktop.h"
#include "sp-item-transform.h"

#include "node-context.h" //For node align/distribute function
#include "nodepath.h" //For node align/distribute function

#include "tools-switch.h"

/*
 * handler functions for quick align dialog
 *
 * todo: dialog with more aligns
 * - more aligns (30 % left from center ...)
 * - aligns for nodes
 *
 */

class Action {
public :
    Action(const Glib::ustring &id,
           const Glib::ustring &tiptext,
           guint row, guint column,
           Gtk::Table &parent,
           Gtk::Tooltips &tooltips):
        _id(id),
        _parent(parent)
    {
        Gtk::Widget*  pIcon = Gtk::manage( sp_icon_get_icon( _id, GTK_ICON_SIZE_LARGE_TOOLBAR) );
        Gtk::Button * pButton = Gtk::manage(new Gtk::Button());
        pButton->set_relief(Gtk::RELIEF_NONE);
        pIcon->show();
        pButton->add(*pIcon);
        pButton->show();

        pButton->signal_clicked()
            .connect(sigc::mem_fun(*this, &Action::on_button_click));
        tooltips.set_tip(*pButton, tiptext);
        parent.attach(*pButton, column, column+1, row, row+1, Gtk::FILL, Gtk::FILL);
    }
    virtual ~Action(){}
private :

    virtual void on_button_click(){}

    Glib::ustring _id;
    Gtk::Table &_parent;
};



class ActionAlign : public Action {
public :
    struct Coeffs {
       double mx0, mx1, my0, my1;
	double sx0, sx1, sy0, sy1;
    };
    ActionAlign(const Glib::ustring &id,
                const Glib::ustring &tiptext,
                guint row, guint column,
                DialogAlign &dialog,
                guint coeffIndex):
        Action(id, tiptext, row, column,
               dialog.align_table(), dialog.tooltips()),
        _index(coeffIndex),
        _dialog(dialog)
    {}

private :

    virtual void on_button_click() {
        //Retreive selected objects
        SPDesktop *desktop = SP_ACTIVE_DESKTOP;
        if (!desktop) return;

        Inkscape::Selection *selection = SP_DT_SELECTION(desktop);
        if (!selection) return;

        std::list<SPItem *> selected;
        selection->list(selected);
        if (selected.empty()) return;

        NR::Point mp; //Anchor point
        DialogAlign::AlignTarget target = _dialog.getAlignTarget();
        const Coeffs &a= _allCoeffs[_index];
        switch (target)
        {
        case DialogAlign::LAST:
        case DialogAlign::FIRST:
        case DialogAlign::BIGGEST:
        case DialogAlign::SMALLEST:
        {
            //Check 2 or more selected objects
            std::list<SPItem *>::iterator second(selected.begin());
            ++second;
            if (second == selected.end())
                return;
            //Find the master (anchor on which the other objects are aligned)
            std::list<SPItem *>::iterator master(
                _dialog.find_master (
                    selected,
                    (a.mx0 != 0.0) ||
                    (a.mx1 != 0.0) )
                );
            //remove the master from the selection
            SPItem * thing = *master;
            selected.erase(master);
            //Compute the anchor point
            NR::Rect b = sp_item_bbox_desktop (thing);
            mp = NR::Point(a.mx0 * b.min()[NR::X] + a.mx1 * b.max()[NR::X],
                           a.my0 * b.min()[NR::Y] + a.my1 * b.max()[NR::Y]);
            break;
        }

        case DialogAlign::PAGE:
            mp = NR::Point(a.mx1 * sp_document_width(SP_DT_DOCUMENT(desktop)),
                           a.my1 * sp_document_height(SP_DT_DOCUMENT(desktop)));
            break;

        case DialogAlign::DRAWING:
        {
            NR::Rect b = sp_item_bbox_desktop
                ( (SPItem *) sp_document_root (SP_DT_DOCUMENT (desktop)) );
            mp = NR::Point(a.mx0 * b.min()[NR::X] + a.mx1 * b.max()[NR::X],
                           a.my0 * b.min()[NR::Y] + a.my1 * b.max()[NR::Y]);
            break;
        }

        case DialogAlign::SELECTION:
        {
            NR::Rect b =  selection->bounds();
            mp = NR::Point(a.mx0 * b.min()[NR::X] + a.mx1 * b.max()[NR::X],
                           a.my0 * b.min()[NR::Y] + a.my1 * b.max()[NR::Y]);
            break;
        }

        default:
            g_assert_not_reached ();
            break;
        };  // end of switch

        bool changed = false;
        //Move each item in the selected list
        for (std::list<SPItem *>::iterator it(selected.begin());
             it != selected.end();
             it++)
        {
            NR::Rect b = sp_item_bbox_desktop (*it);
            NR::Point const sp(a.sx0 * b.min()[NR::X] + a.sx1 * b.max()[NR::X],
                               a.sy0 * b.min()[NR::Y] + a.sy1 * b.max()[NR::Y]);
            NR::Point const mp_rel( mp - sp );
            if (LInfty(mp_rel) > 1e-9) {
                sp_item_move_rel(*it, NR::translate(mp_rel));
                changed = true;
            }
        }


        if (changed) {
            sp_document_done ( SP_DT_DOCUMENT (desktop) );
        }


    }
    guint _index;
    DialogAlign &_dialog;

    static const Coeffs _allCoeffs[10];

};
ActionAlign::Coeffs const ActionAlign::_allCoeffs[10] = {
    {1., 0., 0., 0., 0., 1., 0., 0.},
    {1., 0., 0., 0., 1., 0., 0., 0.},
    {.5, .5, 0., 0., .5, .5, 0., 0.},
    {0., 1., 0., 0., 0., 1., 0., 0.},
    {0., 1., 0., 0., 1., 0., 0., 0.},
    {0., 0., 0., 1., 0., 0., 1., 0.},
    {0., 0., 0., 1., 0., 0., 0., 1.},
    {0., 0., .5, .5, 0., 0., .5, .5},
    {0., 0., 1., 0., 0., 0., 1., 0.},
    {0., 0., 1., 0., 0., 0., 0., 1.}
};


struct BBoxSort
{
    SPItem *item;
    float anchor;
    NR::Rect bbox;
    BBoxSort(SPItem *pItem, NR::Dim2 orientation, double kBegin, double kEnd) :
        item(pItem),
        bbox (sp_item_bbox_desktop (pItem))
    {
        anchor = kBegin * bbox.min()[orientation] + kEnd * bbox.max()[orientation];
    }
    BBoxSort(const BBoxSort &rhs):
        //NOTE :  this copy ctor is called O(sort) when sorting the vector
        //this is bad. The vector should be a vector of pointers.
        //But I'll wait the bohem GC before doing that
        item(rhs.item), anchor(rhs.anchor), bbox(rhs.bbox) {
    }
};
bool operator< (const BBoxSort &a, const BBoxSort &b)
{
    return (a.anchor < b.anchor);
}

class ActionDistribute : public Action {
public :
    ActionDistribute(const Glib::ustring &id,
                     const Glib::ustring &tiptext,
                     guint row, guint column,
                     DialogAlign &dialog,
                     bool onInterSpace,
                     NR::Dim2 orientation,
                     double kBegin, double kEnd
        ):
        Action(id, tiptext, row, column,
               dialog.distribute_table(), dialog.tooltips()),
        _dialog(dialog),
        _onInterSpace(onInterSpace),
        _orientation(orientation),
        _kBegin(kBegin),
        _kEnd( kEnd)
    {}

private :
    virtual void on_button_click() {
        //Retreive selected objects
        SPDesktop *desktop = SP_ACTIVE_DESKTOP;
        if (!desktop) return;

        Inkscape::Selection *selection = SP_DT_SELECTION(desktop);
        if (!selection) return;

        std::list<SPItem *> selected;
        selection->list(selected);
        if (selected.empty()) return;

        //Check 2 or more selected objects
        std::list<SPItem *>::iterator second(selected.begin());
        ++second;
        if (second == selected.end()) return;


        std::vector< BBoxSort  > sorted;
        for (std::list<SPItem *>::iterator it(selected.begin());
            it != selected.end();
            ++it)
        {
            BBoxSort b (*it, _orientation, _kBegin, _kEnd);
            sorted.push_back(b);
        }
        //sort bbox by anchors
        std::sort(sorted.begin(), sorted.end());

        unsigned int len = sorted.size();
        bool changed = false;
        if (_onInterSpace)
        {
            //overall bboxes span
            float dist = (sorted.back().bbox.max()[_orientation] -
                          sorted.front().bbox.min()[_orientation]);
            //space eaten by bboxes
            float span = 0;
            for (unsigned int i = 0; i < len; i++)
            {
                span += sorted[i].bbox.extent(_orientation);
            }
            //new distance between each bbox
            float step = (dist - span) / (len - 1);
            float pos = sorted.front().bbox.min()[_orientation];
            for ( std::vector<BBoxSort> ::iterator it (sorted.begin());
                  it < sorted.end();
                  it ++ )
            {
                if (!NR_DF_TEST_CLOSE (pos, it->bbox.min()[_orientation], 1e-6)) {
                    NR::Point t(0.0, 0.0);
                    t[_orientation] = pos - it->bbox.min()[_orientation];
                    sp_item_move_rel(it->item, NR::translate(t));
                    changed = true;
                }
                pos += it->bbox.extent(_orientation);
                pos += step;
            }
        }
        else
        {
            //overall anchor span
            float dist = sorted.back().anchor - sorted.front().anchor;
            //distance between anchors
            float step = dist / (len - 1);

            for ( unsigned int i = 0; i < len ; i ++ )
            {
                BBoxSort & it(sorted[i]);
                //new anchor position
                float pos = sorted.front().anchor + i * step;
                //Don't move if we are really close
                if (!NR_DF_TEST_CLOSE (pos, it.anchor, 1e-6)) {
                    //Compute translation
                    NR::Point t(0.0, 0.0);
                    t[_orientation] = pos - it.anchor;
                    //translate
                    sp_item_move_rel(it.item, NR::translate(t));
                    changed = true;
                }
            }
        }

        if (changed) {
            sp_document_done ( SP_DT_DOCUMENT (desktop) );
        }
    }
    guint _index;
    DialogAlign &_dialog;
    bool _onInterSpace;
    NR::Dim2 _orientation;

    double _kBegin;
    double _kEnd;

};


class ActionNode : public Action {
public :
    ActionNode(const Glib::ustring &id,
               const Glib::ustring &tiptext,
               guint column,
               DialogAlign &dialog,
               NR::Dim2 orientation, bool distribute):
        Action(id, tiptext, 0, column,
               dialog.nodes_table(), dialog.tooltips()),
        _orientation(orientation),
        _distribute(distribute)
    {}

private :
    NR::Dim2 _orientation;
    bool _distribute;
    virtual void on_button_click()
    {

        if (!SP_ACTIVE_DESKTOP) return;
	SPEventContext *event_context = (SP_ACTIVE_DESKTOP)->event_context;
	if (!SP_IS_NODE_CONTEXT (event_context)) return ;

        Inkscape::NodePath::Path *nodepath = SP_NODE_CONTEXT (event_context)->nodepath;
        if (!nodepath) return;
        if (_distribute)
            sp_nodepath_selected_distribute(nodepath, _orientation);
        else
            sp_nodepath_selected_align(nodepath, _orientation);

    }
};

class ActionUnclump : public Action {
public :
    ActionUnclump(const Glib::ustring &id,
               const Glib::ustring &tiptext,
               guint row,
               guint column,
               DialogAlign &dialog):
        Action(id, tiptext, row, column,
               dialog.distribute_table(), dialog.tooltips())
    {}

private :
    virtual void on_button_click()
    {
        if (!SP_ACTIVE_DESKTOP) return;

        unclump ((GSList *) SP_DT_SELECTION(SP_ACTIVE_DESKTOP)->itemList());

        sp_document_done (SP_DT_DOCUMENT (SP_ACTIVE_DESKTOP));
    }
};

void DialogAlign::on_ref_change(){
//Make blink the master
}

/* verb here is not really a verb (not a Inkscape::Verb) */
void DialogAlign::on_tool_changed(unsigned int verb)
{
    setMode(verb == TOOLS_NODES) ;
}

void DialogAlign::setMode(bool nodeEdit)
{
    //Act on widgets used in node mode
    void ( Gtk::Widget::*mNode) ()  = nodeEdit ?
        &Gtk::Widget::show_all : &Gtk::Widget::hide_all;

    //Act on widgets used in selection mode
  void ( Gtk::Widget::*mSel) ()  = nodeEdit ?
      &Gtk::Widget::hide_all : &Gtk::Widget::show_all;


    ((_alignFrame).*(mSel))();
    ((_distributeFrame).*(mSel))();
    ((_nodesFrame).*(mNode))();

}
void DialogAlign::addAlignButton(const Glib::ustring &id, const Glib::ustring tiptext,
                                 guint row, guint col)
{
    _actionList.push_back(
        new ActionAlign(
            id, tiptext, row, col,
            *this , col + row * 5));
}
void DialogAlign::addDistributeButton(const Glib::ustring &id, const Glib::ustring tiptext,
                                      guint row, guint col, bool onInterSpace,
                                      NR::Dim2 orientation, float kBegin, float kEnd)
{
    _actionList.push_back(
        new ActionDistribute(
            id, tiptext, row, col, *this ,
            onInterSpace, orientation,
            kBegin, kEnd
            )
        );
}

void DialogAlign::addNodeButton(const Glib::ustring &id, const Glib::ustring tiptext,
                   guint col, NR::Dim2 orientation, bool distribute)
{
    _actionList.push_back(
        new ActionNode(
            id, tiptext, col,
            *this, orientation, distribute));
}

void DialogAlign::addUnclumpButton(const Glib::ustring &id, const Glib::ustring tiptext,
                                      guint row, guint col)
{
    _actionList.push_back(
        new ActionUnclump(
            id, tiptext, row, col, *this)
        );
}


DialogAlign::DialogAlign():
    Dockable(_("Layout"), "dialogs.align"),
    _alignFrame(_("Align")),
    _distributeFrame(_("Distribute")),
    _nodesFrame(_("Nodes")),
    _alignTable(2,5, true),
    _distributeTable(3,4, true),
    _nodesTable(1, 4, true),
    _anchorLabel(_("Relative to: "))
{
    //Instanciate the align buttons
    addAlignButton("al_left_out",
                   _("Align right sides of objects to left side of anchor"),
                   0, 0);
    addAlignButton("al_left_in",
                   _("Align left sides"),
                   0, 1);
    addAlignButton("al_center_hor",
                   _("Center horizontally"),
                   0, 2);
    addAlignButton("al_right_in",
                   _("Align right sides"),
                   0, 3);
    addAlignButton("al_right_out",
                   _("Align left sides of objects to right side of anchor"),
                   0, 4);
    addAlignButton("al_top_out",
                   _("Align bottoms of objects to top of anchor"),
                   1, 0);
    addAlignButton("al_top_in",
                   _("Align tops"),
                   1, 1);
    addAlignButton("al_center_ver",
                   _("Center vertically"),
                   1, 2);
    addAlignButton("al_bottom_in",
                   _("Align bottoms"),
                   1, 3);
    addAlignButton("al_bottom_out",
                   _("Align tops of objects to bottom of anchor"),
                   1, 4);



    //The distribute buttons
    addDistributeButton("distribute_hdist",
                        _("Make horizontal gaps between objects equal"),
                        0, 0, true, NR::X, .5, .5);

    addDistributeButton("distribute_left",
                        _("Distribute left sides equidistantly"),
                        0, 1, false, NR::X, 1., 0.);
    addDistributeButton("distribute_hcentre",
                        _("Distribute centers equidistantly horizontally"),
                        0, 2, false, NR::X, .5, .5);
    addDistributeButton("distribute_right",
                        _("Distribute right sides equidistantly"),
                        0, 3, false, NR::X, 0., 1.);

    addDistributeButton("distribute_vdist",
                        _("Make vertical gaps between objects equal"),
                        1, 0, true, NR::Y, .5, .5);

    addDistributeButton("distribute_bottom",
                        _("Distribute bottoms equidistantly"),
                        1, 1, false, NR::Y, 1., 0.);
    addDistributeButton("distribute_vcentre",
                        _("Distribute centers equidistantly vertically"),
                        1, 2, false, NR::Y, .5, .5);
    addDistributeButton("distribute_top",
                        _("Distribute tops equidistantly"),
                        1, 3, false, NR::Y, 0, 1);

    addUnclumpButton("unclump",
                        _("Unclump selected objects"),
                        2, 0);

    //Node Mode buttons
    addNodeButton("node_halign",
                  _("Align selected nodes horizontally"),
                  0, NR::X, false);
    addNodeButton("node_valign",
                  _("Align selected nodes vertically"),
                  1, NR::Y, false);
    addNodeButton("node_hdistribute",
                  _("Distribute selected nodes horizontally"),
                  2, NR::X, true);
    addNodeButton("node_vdistribute",
                  _("Distribute selected nodes vertically"),
                  3, NR::Y, true);

    //Rest of the widgetry


    _combo.append_text(_("Last selected"));
    _combo.append_text(_("First selected"));
    _combo.append_text(_("Biggest item"));
    _combo.append_text(_("Smallest item"));
    _combo.append_text(_("Page"));
    _combo.append_text(_("Drawing"));
    _combo.append_text(_("Selection"));

    _combo.set_active(6);
    _combo.signal_changed().connect(sigc::mem_fun(*this, &DialogAlign::on_ref_change));

    _anchorBox.pack_start(_anchorLabel);
    _anchorBox.pack_start(_combo);

    _alignBox.pack_start(_anchorBox);
    _alignBox.pack_start(_alignTable);

    _alignFrame.add(_alignBox);
    _distributeFrame.add(_distributeTable);
    _nodesFrame.add(_nodesTable);


    _widget.pack_start(_alignFrame, false, false, 0);
    _widget.pack_start(_distributeFrame, false, false, 0);
    _widget.pack_start(_nodesFrame, false, false, 0);

    _widget.show();

    bool in_node_mode = false;
    //Connect to desktop signals
    SPDesktop *pD = SP_ACTIVE_DESKTOP;
    if ( pD )
    {
        pD->_tool_changed.connect(sigc::mem_fun(*this, &DialogAlign::on_tool_changed));
        in_node_mode = tools_isactive (pD, TOOLS_NODES);
    }
    setMode(in_node_mode);
}

DialogAlign::~DialogAlign(){
    for (std::list<Action *>::iterator it = _actionList.begin();
         it != _actionList.end();
         it ++)
        delete *it;
};


DialogAlign & DialogAlign::get()
{
    static DialogAlign &da = *(new DialogAlign());
    return da;
}


std::list<SPItem *>::iterator DialogAlign::find_master( std::list<SPItem *> &list, bool horizontal){
    std::list<SPItem *>::iterator master = list.end();
    switch (getAlignTarget()) {
    case LAST:
        return list.begin();
        break;

    case FIRST:
        return --(list.end());
        break;

    case BIGGEST:
    {
        gdouble max = -1e18;
        for (std::list<SPItem *>::iterator it = list.begin(); it != list.end(); it++) {
            NR::Rect b = sp_item_bbox_desktop (*it);
            gdouble dim = b.extent(horizontal ? NR::X : NR::Y);
            if (dim > max) {
                max = dim;
                master = it;
            }
        }
        return master;
        break;
    }

    case SMALLEST:
    {
        gdouble max = 1e18;
        for (std::list<SPItem *>::iterator it = list.begin(); it != list.end(); it++) {
            NR::Rect b = sp_item_bbox_desktop (*it);
            gdouble dim = b.extent(horizontal ? NR::X : NR::Y);
            if (dim < max) {
                max = dim;
                master = it;
            }
        }
        return master;
        break;
    }

    default:
        g_assert_not_reached ();
        break;

    } // end of switch statement
    return NULL;
}

DialogAlign::AlignTarget DialogAlign::getAlignTarget(){
    return AlignTarget(_combo.get_active_row_number());
}
void sp_quick_align_dialog (void)
{
    DialogAlign::get().present();
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
