#define __SP_ITEM_C__

/** \file
 * Base class for visual SVG elements
 */
/*
 * Author:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <config.h>

#include <math.h>
#include <string.h>

#include <algorithm>

#include "macros.h"
#include "svg/svg.h"
#include "print.h"
#include "display/nr-arena.h"
#include "display/nr-arena-item.h"
#include "attributes.h"
#include "document.h"
#include "uri.h"
#include "uri-references.h"

#include "selection.h"
#include "style.h"
#include "helper/sp-intl.h"
#include "sp-root.h"
#include "sp-anchor.h"
#include "sp-clippath.h"
#include "sp-mask.h"
#include "sp-rect.h"
#include "sp-item.h"
#include "sp-item-rm-unsatisfied-cns.h"
#include "prefs-utils.h"
#include "libnr/nr-matrix.h"
#include "libnr/nr-matrix-div.h"
#include "libnr/nr-matrix-fns.h"
#include "libnr/nr-matrix-ops.h"
#include "libnr/nr-matrix-scale-ops.h"
#include "libnr/nr-matrix-translate-ops.h"
#include "libnr/nr-translate-scale-ops.h"
#include "libnr/nr-rect.h"
#include "svg/stringstream.h"
#include "algorithms/find-last-if.h"
#include "util/list.h"
#include "util/reverse-list.h"

#include "xml/repr.h"
#include "xml/repr-private.h"

#define noSP_ITEM_DEBUG_IDLE

static void sp_item_class_init(SPItemClass *klass);
static void sp_item_init(SPItem *item);

static void sp_item_build(SPObject *object, SPDocument *document, SPRepr *repr);
static void sp_item_release(SPObject *object);
static void sp_item_set(SPObject *object, unsigned key, gchar const *value);
static void sp_item_update(SPObject *object, SPCtx *ctx, guint flags);
static SPRepr *sp_item_write(SPObject *object, SPRepr *repr, guint flags);
static void sp_item_set_item_transform(SPItem *item, NR::Matrix const &transform);

static gchar *sp_item_private_description(SPItem *item);
static void sp_item_private_snappoints(SPItem const *item, SnapPointsIter p);

static SPItemView *sp_item_view_new_prepend(SPItemView *list, SPItem *item, unsigned flags, unsigned key, NRArenaItem *arenaitem);
static SPItemView *sp_item_view_list_remove(SPItemView *list, SPItemView *view);

static SPObjectClass *parent_class;

static void clip_ref_changed(SPObject *old_clip, SPObject *clip, SPItem *item);
static void mask_ref_changed(SPObject *old_clip, SPObject *clip, SPItem *item);

GType
sp_item_get_type(void)
{
    static GType type = 0;
    if (!type) {
        GTypeInfo info = {
            sizeof(SPItemClass),
            NULL, NULL,
            (GClassInitFunc) sp_item_class_init,
            NULL, NULL,
            sizeof(SPItem),
            16,
            (GInstanceInitFunc) sp_item_init,
            NULL,   /* value_table */
        };
        type = g_type_register_static(SP_TYPE_OBJECT, "SPItem", &info, (GTypeFlags)0);
    }
    return type;
}

static void
sp_item_class_init(SPItemClass *klass)
{
    SPObjectClass *sp_object_class = (SPObjectClass *) klass;

    parent_class = (SPObjectClass *)g_type_class_ref(SP_TYPE_OBJECT);

    sp_object_class->build = sp_item_build;
    sp_object_class->release = sp_item_release;
    sp_object_class->set = sp_item_set;
    sp_object_class->update = sp_item_update;
    sp_object_class->write = sp_item_write;

    klass->description = sp_item_private_description;
    klass->snappoints = sp_item_private_snappoints;
}

static void
sp_item_init(SPItem *item)
{
    SPObject *object = SP_OBJECT(item);

    item->sensitive = TRUE;
    item->printable = TRUE;

    item->transform = NR::identity();

    item->display = NULL;

    item->clip_ref = new SPClipPathReference(SP_OBJECT(item));
		{
			sigc::signal<void, SPObject *, SPObject *> cs1=item->clip_ref->changedSignal();
			sigc::slot2<void,SPObject*, SPObject *> sl1=sigc::bind(sigc::ptr_fun(clip_ref_changed), item);
			cs1.connect(sl1);
		}
		
    item->mask_ref = new SPMaskReference(SP_OBJECT(item));
		sigc::signal<void, SPObject *, SPObject *> cs2=item->mask_ref->changedSignal();
		sigc::slot2<void,SPObject*, SPObject *> sl2=sigc::bind(sigc::ptr_fun(mask_ref_changed), item);
    cs2.connect(sl2);

    if (!object->style) object->style = sp_style_new_from_object(SP_OBJECT(item));

    new (&item->_transformed_signal) sigc::signal<void, NR::Matrix const *, SPItem *>();
}

bool SPItem::isVisibleAndUnlocked() const {
    return (!isHidden() && !isLocked());
}

bool SPItem::isVisibleAndUnlocked(unsigned display_key) const {
    return (!isHidden(display_key) && !isLocked());
}

bool SPItem::isLocked() const {
    return !sensitive;
}

void SPItem::setLocked(bool locked) {
    sp_repr_set_attr(SP_OBJECT_REPR(this), "sodipodi:insensitive",
                     ( locked ? "1" : NULL ));
}

bool SPItem::isHidden() const {
    return style->display.computed == SP_CSS_DISPLAY_NONE;
}

void SPItem::setHidden(bool hide) {
    style->display.set = TRUE;
    style->display.value = ( hide ? SP_CSS_DISPLAY_NONE : SP_CSS_DISPLAY_BLOCK );
    style->display.computed = style->display.value;
    style->display.inherit = FALSE;
    updateRepr();
}

bool SPItem::isHidden(unsigned display_key) const {
    for ( SPItemView *view(display) ; view ; view = view->next ) {
        if ( view->key == display_key ) {
            g_assert(view->arenaitem != NULL);
            for ( NRArenaItem *arenaitem = view->arenaitem ;
                  arenaitem ; arenaitem = arenaitem->parent )
            {
                if (!arenaitem->visible) {
                    return true;
                }
            }
            return false;
        }
    }
    return true;
}

/** Returns something suitable for the `Hide' checkbox in the Object Properties dialog box.
 *  Corresponds to setExplicitlyHidden.
 */
bool
SPItem::isExplicitlyHidden() const
{
    return (this->style->display.set
	    && this->style->display.value == SP_CSS_DISPLAY_NONE);
}

/** Sets the display CSS property to `hidden' if \a val is true, otherwise makes it unset */
void
SPItem::setExplicitlyHidden(bool const val) {
    this->style->display.set = val;
    this->style->display.value = ( val ? SP_CSS_DISPLAY_NONE : SP_CSS_DISPLAY_BLOCK );
    this->style->display.computed = this->style->display.value;
    this->updateRepr();
}

namespace {

bool is_item(SPObject const &object) {
    return SP_IS_ITEM(&object);
}

}

void SPItem::raiseToTop() {
    using Inkscape::Algorithms::find_last_if;

    SPObject *topmost=find_last_if<SPObject::SiblingIterator>(
        SP_OBJECT_NEXT(this), NULL, &is_item
    );
    if (topmost) {
        SPRepr *repr=SP_OBJECT_REPR(this);
        sp_repr_change_order(sp_repr_parent(repr), repr, SP_OBJECT_REPR(topmost));
    }
}

void SPItem::raiseOne() {
    SPObject *next_higher=std::find_if<SPObject::SiblingIterator>(
        SP_OBJECT_NEXT(this), NULL, &is_item
    );
    if (next_higher) {
        SPRepr *repr=SP_OBJECT_REPR(this);
        SPRepr *ref=SP_OBJECT_REPR(next_higher);
        sp_repr_change_order(sp_repr_parent(repr), repr, ref);
    }
}

void SPItem::lowerOne() {
    using Inkscape::Util::MutableList;
    using Inkscape::Util::reverse_list;

    MutableList<SPObject &> next_lower=std::find_if(
        reverse_list<SPObject::SiblingIterator>(
            SP_OBJECT_PARENT(this)->firstChild(), this
        ),
        MutableList<SPObject &>(),
        &is_item
    );
    if (next_lower) {
        ++next_lower;
        SPRepr *repr=SP_OBJECT_REPR(this);
        SPRepr *ref=( next_lower ? SP_OBJECT_REPR(&*next_lower) : NULL );
        sp_repr_change_order(sp_repr_parent(repr), repr, ref);
    }
}

void SPItem::lowerToBottom() {
    using Inkscape::Algorithms::find_last_if;
    using Inkscape::Util::MutableList;
    using Inkscape::Util::reverse_list;

    MutableList<SPObject &> bottom=find_last_if(
        reverse_list<SPObject::SiblingIterator>(
            SP_OBJECT_PARENT(this)->firstChild(), this
        ),
        MutableList<SPObject &>(),
        &is_item
    );
    if (bottom) {
        ++bottom;
        SPRepr *repr=SP_OBJECT_REPR(this);
        SPRepr *ref=( bottom ? SP_OBJECT_REPR(&*bottom) : NULL );
        sp_repr_change_order(sp_repr_parent(repr), repr, ref);
    }
}

static void
sp_item_build(SPObject *object, SPDocument *document, SPRepr *repr)
{
    sp_object_read_attr(object, "style");
    sp_object_read_attr(object, "transform");
    sp_object_read_attr(object, "clip-path");
    sp_object_read_attr(object, "mask");
    sp_object_read_attr(object, "sodipodi:insensitive");
    sp_object_read_attr(object, "sodipodi:nonprintable");

    if (((SPObjectClass *) (parent_class))->build) {
        (* ((SPObjectClass *) (parent_class))->build)(object, document, repr);
    }
}

static void
sp_item_release(SPObject *object)
{
    SPItem *item = (SPItem *) object;

    if (item->clip_ref) {
        item->clip_ref->detach();
        delete item->clip_ref;
        item->clip_ref = NULL;
    }

    if (item->mask_ref) {
        item->mask_ref->detach();
        delete item->mask_ref;
        item->mask_ref = NULL;
    }

    if (((SPObjectClass *) (parent_class))->release) {
        ((SPObjectClass *) parent_class)->release(object);
    }

    while (item->display) {
        nr_arena_item_unparent(item->display->arenaitem);
        item->display = sp_item_view_list_remove(item->display, item->display);
    }

    item->_transformed_signal.~signal();
}

static void
sp_item_set(SPObject *object, unsigned key, gchar const *value)
{
    SPItem *item = (SPItem *) object;

    switch (key) {
        case SP_ATTR_TRANSFORM: {
            NR::Matrix t;
            if (value && sp_svg_transform_read(value, &t)) {
                sp_item_set_item_transform(item, t);
            } else {
                sp_item_set_item_transform(item, NR::identity());
            }
            break;
        }
        case SP_PROP_CLIP_PATH: {
            gchar *uri = Inkscape::parse_css_url(value);
            if (uri) {
                try {
                    item->clip_ref->attach(Inkscape::URI(uri));
                } catch (Inkscape::BadURIException &e) {
                    g_warning("%s", e.what());
                    item->clip_ref->detach();
                }
                g_free(uri);
            } else {
                item->clip_ref->detach();
            }

            break;
        }
        case SP_PROP_MASK: {
            gchar *uri=Inkscape::parse_css_url(value);
            if (uri) {
                try {
                    item->mask_ref->attach(Inkscape::URI(uri));
                } catch (Inkscape::BadURIException &e) {
                    g_warning("%s", e.what());
                    item->mask_ref->detach();
                }
                g_free(uri);
            } else {
                item->mask_ref->detach();
            }

            break;
        }
        case SP_ATTR_SODIPODI_INSENSITIVE:
            item->sensitive = !value;
            for (SPItemView *v = item->display; v != NULL; v = v->next) {
                nr_arena_item_set_sensitive(v->arenaitem, item->sensitive);
            }
            break;
        case SP_ATTR_SODIPODI_NONPRINTABLE:
            item->printable = !value;
            for (SPItemView *v = item->display; v != NULL; v = v->next) {
                if (v->flags & SP_ITEM_SHOW_PRINT) {
                    nr_arena_item_set_visible(v->arenaitem, item->printable);
                }
            }
            break;
        case SP_ATTR_STYLE:
            sp_style_read_from_object(object->style, object);
            object->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
            break;
        default:
            if (SP_ATTRIBUTE_IS_CSS(key)) {
                sp_style_read_from_object(object->style, object);
                object->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG);
            } else {
                if (((SPObjectClass *) (parent_class))->set) {
                    (* ((SPObjectClass *) (parent_class))->set)(object, key, value);
                }
            }
            break;
    }
}

static void
clip_ref_changed(SPObject *old_clip, SPObject *clip, SPItem *item)
{
    if (old_clip) {
        SPItemView *v;
        /* Hide clippath */
        for (v = item->display; v != NULL; v = v->next) {
            sp_clippath_hide(SP_CLIPPATH(old_clip), NR_ARENA_ITEM_GET_KEY(v->arenaitem));
            nr_arena_item_set_clip(v->arenaitem, NULL);
        }
    }
    if (SP_IS_CLIPPATH(clip)) {
        NRRect bbox;
        sp_item_invoke_bbox(item, &bbox, NR::identity(), TRUE);
        for (SPItemView *v = item->display; v != NULL; v = v->next) {
            if (!v->arenaitem->key) {
                NR_ARENA_ITEM_SET_KEY(v->arenaitem, sp_item_display_key_new(3));
            }
            NRArenaItem *ai = sp_clippath_show(SP_CLIPPATH(clip),
                                               NR_ARENA_ITEM_ARENA(v->arenaitem),
                                               NR_ARENA_ITEM_GET_KEY(v->arenaitem));
            nr_arena_item_set_clip(v->arenaitem, ai);
            nr_arena_item_unref(ai);
            sp_clippath_set_bbox(SP_CLIPPATH(clip), NR_ARENA_ITEM_GET_KEY(v->arenaitem), &bbox);
        }
    }
}

static void
mask_ref_changed(SPObject *old_mask, SPObject *mask, SPItem *item)
{
    if (old_mask) {
        /* Hide mask */
        for (SPItemView *v = item->display; v != NULL; v = v->next) {
            sp_mask_hide(SP_MASK(old_mask), NR_ARENA_ITEM_GET_KEY(v->arenaitem));
            nr_arena_item_set_mask(v->arenaitem, NULL);
        }
    }
    if (SP_IS_MASK(mask)) {
        NRRect bbox;
        sp_item_invoke_bbox(item, &bbox, NR::identity(), TRUE);
        for (SPItemView *v = item->display; v != NULL; v = v->next) {
            if (!v->arenaitem->key) {
                NR_ARENA_ITEM_SET_KEY(v->arenaitem, sp_item_display_key_new(3));
            }
            NRArenaItem *ai = sp_mask_show(SP_MASK(mask),
                                           NR_ARENA_ITEM_ARENA(v->arenaitem),
                                           NR_ARENA_ITEM_GET_KEY(v->arenaitem));
            nr_arena_item_set_mask(v->arenaitem, ai);
            nr_arena_item_unref(ai);
            sp_mask_set_bbox(SP_MASK(mask), NR_ARENA_ITEM_GET_KEY(v->arenaitem), &bbox);
        }
    }
}

static void
sp_item_update(SPObject *object, SPCtx *ctx, guint flags)
{
    SPItem *item = SP_ITEM(object);

    if (((SPObjectClass *) (parent_class))->update)
        (* ((SPObjectClass *) (parent_class))->update)(object, ctx, flags);

    if (flags & (SP_OBJECT_CHILD_MODIFIED_FLAG | SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_STYLE_MODIFIED_FLAG)) {
        if (flags & SP_OBJECT_MODIFIED_FLAG) {
            for (SPItemView *v = item->display; v != NULL; v = v->next) {
                nr_arena_item_set_transform(v->arenaitem, item->transform);
            }
        }

        SPClipPath *clip_path = item->clip_ref->getObject();
        SPMask *mask = item->mask_ref->getObject();

        if ( clip_path || mask ) {
            NRRect bbox;
            sp_item_invoke_bbox(item, &bbox, NR::identity(), TRUE);
            if (clip_path) {
                for (SPItemView *v = item->display; v != NULL; v = v->next) {
                    sp_clippath_set_bbox(clip_path, NR_ARENA_ITEM_GET_KEY(v->arenaitem), &bbox);
                }
            }
            if (mask) {
                for (SPItemView *v = item->display; v != NULL; v = v->next) {
                    sp_mask_set_bbox(mask, NR_ARENA_ITEM_GET_KEY(v->arenaitem), &bbox);
                }
            }
        }

        if (flags & SP_OBJECT_STYLE_MODIFIED_FLAG) {
            for (SPItemView *v = item->display; v != NULL; v = v->next) {
                nr_arena_item_set_opacity(v->arenaitem, SP_SCALE24_TO_FLOAT(object->style->opacity.value));
                nr_arena_item_set_visible(v->arenaitem, !item->isHidden());
            }
        }
    }
}

static SPRepr *
sp_item_write(SPObject *object, SPRepr *repr, guint flags)
{
    SPItem *item = SP_ITEM(object);

    gchar c[256];
    if (sp_svg_transform_write(c, 256, item->transform)) {
        sp_repr_set_attr(repr, "transform", c);
    } else {
        sp_repr_set_attr(repr, "transform", NULL);
    }

    if (SP_OBJECT_PARENT(object)) {
        gchar *s = sp_style_write_difference(SP_OBJECT_STYLE(object), SP_OBJECT_STYLE(SP_OBJECT_PARENT(object)));
        if (s) {
            sp_repr_set_attr(repr, "style", (*s) ? s : NULL);
            g_free(s);
        }
        // Since differencing with parent may remove some of the properties in style=, this may
        // unexpectedly reveal (and enable) the css attrs of the same names if they are present
        // (they are lower priority than style= but are read if there's no corresponding inline
        // property). So here we unset any style attrs that correspond to our object's
        // SPStyle. This means all properties will end up in style= and the attrs will be gone.
        sp_style_unset_property_attrs (object);
    }

    if (flags & SP_OBJECT_WRITE_EXT) {
        sp_repr_set_attr(repr, "sodipodi:insensitive", item->sensitive ? NULL : "true");
    }

    if (((SPObjectClass *) (parent_class))->write) {
        ((SPObjectClass *) (parent_class))->write(object, repr, flags);
    }

    return repr;
}

void
sp_item_invoke_bbox(SPItem const *item, NRRect *bbox, NR::Matrix const &transform, unsigned const clear)
{
    sp_item_invoke_bbox_full(item, bbox, transform, 0, clear);
}

void
sp_item_invoke_bbox_full(SPItem const *item, NRRect *bbox, NR::Matrix const &transform, unsigned const flags, unsigned const clear)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));
    g_assert(bbox != NULL);

    if (clear) {
        bbox->x0 = bbox->y0 = 1e18;
        bbox->x1 = bbox->y1 = -1e18;
    }

    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->bbox) {
        ((SPItemClass *) G_OBJECT_GET_CLASS(item))->bbox(item, bbox, transform, flags);
    }
}

unsigned sp_item_pos_in_parent(SPItem *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    SPObject *parent = SP_OBJECT_PARENT(item);
    g_assert(parent != NULL);
    g_assert(SP_IS_OBJECT(parent));

    SPObject *object = SP_OBJECT(item);

    unsigned pos=0;
    for ( SPObject *iter = sp_object_first_child(parent) ; iter ; iter = SP_OBJECT_NEXT(iter)) {
        if ( iter == object ) {
            return pos;
        }
        if (SP_IS_ITEM(iter)) {
            pos++;
        }
    }

    g_assert_not_reached();
    return 0;
}

void
sp_item_bbox_desktop(SPItem *item, NRRect *bbox)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));
    g_assert(bbox != NULL);

    sp_item_invoke_bbox(item, bbox, sp_item_i2d_affine(item), TRUE);
}

NR::Rect sp_item_bbox_desktop(SPItem *item)
{
    NRRect ret;
    sp_item_bbox_desktop(item, &ret);
    return NR::Rect(ret);
}

static void sp_item_private_snappoints(SPItem const *item, SnapPointsIter p)
{
    NRRect bbox;
    sp_item_invoke_bbox(item, &bbox, sp_item_i2d_affine(item), TRUE);
    NR::Rect const bbox2(bbox);
    /* Just a pair of opposite corners of the bounding box suffices given that we don't yet
       support angled guide lines. */
    
    *p = bbox2.min();
    *p = bbox2.max();
}

void sp_item_snappoints(SPItem const *item, SnapPointsIter p)
{
    g_assert (item != NULL);
    g_assert (SP_IS_ITEM(item));

    SPItemClass const &item_class = *(SPItemClass const *) G_OBJECT_GET_CLASS(item);
    if (item_class.snappoints) {
        item_class.snappoints(item, p);
    }
}

void
sp_item_invoke_print(SPItem *item, SPPrintContext *ctx)
{
    if (item->printable) {
        if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->print) {
            if (!item->transform.test_identity()
                || SP_OBJECT_STYLE(item)->opacity.value != SP_SCALE24_MAX)
            {
                sp_print_bind(ctx, item->transform, SP_SCALE24_TO_FLOAT(SP_OBJECT_STYLE(item)->opacity.value));
                ((SPItemClass *) G_OBJECT_GET_CLASS(item))->print(item, ctx);
                sp_print_release(ctx);
            } else {
                ((SPItemClass *) G_OBJECT_GET_CLASS(item))->print(item, ctx);
            }
        }
    }
}

static gchar *
sp_item_private_description(SPItem *item)
{
    return g_strdup(_("Object"));
}

gchar *
sp_item_description(SPItem *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->description) {
        return ((SPItemClass *) G_OBJECT_GET_CLASS(item))->description(item);
    }

    g_assert_not_reached();
    return NULL;
}

/**
* Allocates unique integer keys.
* \param numkeys Number of keys required.
* \return First allocated key; hence if the returned key is n
* you can use n, n + 1, ..., n + (numkeys - 1)
*/
unsigned
sp_item_display_key_new(unsigned numkeys)
{
    static unsigned dkey = 0;

    dkey += numkeys;

    return dkey - numkeys;
}

NRArenaItem *
sp_item_invoke_show(SPItem *item, NRArena *arena, unsigned key, unsigned flags)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));
    g_assert(arena != NULL);
    g_assert(NR_IS_ARENA(arena));

    NRArenaItem *ai = NULL;
    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->show) {
        ai = ((SPItemClass *) G_OBJECT_GET_CLASS(item))->show(item, arena, key, flags);
    }

    if (ai != NULL) {
        item->display = sp_item_view_new_prepend(item->display, item, flags, key, ai);
        nr_arena_item_set_transform(ai, item->transform);
        nr_arena_item_set_opacity(ai, SP_SCALE24_TO_FLOAT(SP_OBJECT_STYLE(item)->opacity.value));
        nr_arena_item_set_visible(ai, !item->isHidden());
        nr_arena_item_set_sensitive(ai, item->sensitive);
        if (flags & SP_ITEM_SHOW_PRINT) {
            nr_arena_item_set_visible(ai, item->printable);
        }
        if (item->clip_ref->getObject()) {
            NRArenaItem *ac;
            if (!item->display->arenaitem->key) NR_ARENA_ITEM_SET_KEY(item->display->arenaitem, sp_item_display_key_new(3));
            ac = sp_clippath_show(item->clip_ref->getObject(), arena, NR_ARENA_ITEM_GET_KEY(item->display->arenaitem));
            nr_arena_item_set_clip(ai, ac);
            nr_arena_item_unref(ac);
        }
        if (item->mask_ref->getObject()) {
            NRArenaItem *ac;
            if (!item->display->arenaitem->key) NR_ARENA_ITEM_SET_KEY(item->display->arenaitem, sp_item_display_key_new(3));
            ac = sp_mask_show(item->mask_ref->getObject(), arena, NR_ARENA_ITEM_GET_KEY(item->display->arenaitem));
            nr_arena_item_set_mask(ai, ac);
            nr_arena_item_unref(ac);
        }
        NR_ARENA_ITEM_SET_DATA(ai, item);
    }

    return ai;
}

void
sp_item_invoke_hide(SPItem *item, unsigned key)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->hide) {
        ((SPItemClass *) G_OBJECT_GET_CLASS(item))->hide(item, key);
    }

    SPItemView *ref = NULL;
    SPItemView *v = item->display;
    while (v != NULL) {
        SPItemView *next = v->next;
        if (v->key == key) {
            if (item->clip_ref->getObject()) {
                sp_clippath_hide(item->clip_ref->getObject(), NR_ARENA_ITEM_GET_KEY(v->arenaitem));
                nr_arena_item_set_clip(v->arenaitem, NULL);
            }
            if (item->mask_ref->getObject()) {
                sp_mask_hide(item->mask_ref->getObject(), NR_ARENA_ITEM_GET_KEY(v->arenaitem));
                nr_arena_item_set_mask(v->arenaitem, NULL);
            }
            if (!ref) {
                item->display = v->next;
            } else {
                ref->next = v->next;
            }
            nr_arena_item_unparent(v->arenaitem);
            g_free(v);
        } else {
            ref = v;
        }
        v = next;
    }
}

/**
Find out the inverse of previous transform of an item (from its repr)
*/
NR::Matrix
sp_item_transform_repr (SPItem *item)
{
    NR::Matrix t_old(NR::identity());
    gchar const *t_attr = sp_repr_attr(SP_OBJECT_REPR(item), "transform");
    if (t_attr) {
        NR::Matrix t;
        if (sp_svg_transform_read(t_attr, &t)) {
            t_old = t;
        }
    }

    return t_old;
}


/**
 Recursively scale stroke width in \a item and its children by \a expansion
*/
void
sp_item_adjust_stroke_width_recursive(SPItem *item, double expansion)
{
    sp_shape_adjust_stroke (item, expansion);

    for (SPObject *o = SP_OBJECT(item)->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o))
            sp_item_adjust_stroke_width_recursive(SP_ITEM(o), expansion);
    }
}

/**
 Recursively adjust rx and ry of rects
*/
void
sp_item_adjust_rects_recursive(SPItem *item, NR::Matrix advertized_transform)
{
    if (SP_IS_RECT (item)) {
        sp_rect_compensate_rxry (SP_RECT(item), advertized_transform);
    }

    for (SPObject *o = SP_OBJECT(item)->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o))
            sp_item_adjust_rects_recursive(SP_ITEM(o), advertized_transform);
    }
}

/**
 Recursively compensate pattern or gradient transform
*/
void
sp_item_adjust_paint_recursive (SPItem *item, NR::Matrix advertized_transform, NR::Matrix t_ancestors, bool is_pattern)
{
// _Before_ full pattern/gradient transform: t_paint * t_item * t_ancestors
// _After_ full pattern/gradient transform: t_paint_new * t_item * t_ancestors * advertised_transform
// By equating these two expressions we get t_paint_new = t_paint * paint_delta, where:
    NR::Matrix t_item = sp_item_transform_repr (item);
    NR::Matrix paint_delta = t_item * t_ancestors * advertized_transform * t_ancestors.inverse() * t_item.inverse();

    if (is_pattern)
        sp_shape_adjust_pattern (item, paint_delta);
    else 
        sp_shape_adjust_gradient (item, paint_delta);

    for (SPObject *o = SP_OBJECT(item)->children; o != NULL; o = o->next) {
        if (SP_IS_ITEM(o)) {
// At the level of the transformed item, t_ancestors is identity;
// below it, it is the accmmulated chain of transforms from this level to the top level
            sp_item_adjust_paint_recursive (SP_ITEM(o), advertized_transform, t_item * t_ancestors, is_pattern);
        }
    }
}

/** 
A temporary wrapper for the next function accepting the NRMatrix instead of NR::Matrix
*/
void
sp_item_write_transform(SPItem *item, SPRepr *repr, NRMatrix const *transform, NR::Matrix const *adv)
{
    if (transform == NULL)
        sp_item_write_transform(item, repr, NR::identity(), adv);
    else 
        sp_item_write_transform(item, repr, NR::Matrix (transform), adv);
}

/**
Set a new transform on an object. Compensate for stroke scaling and gradient/pattern
fill transform, if necessary. Call the object's set_transform method if transforms are
stored optimized. Send _transformed_signal. Invoke _write method so that the repr is
updated with the new transform.
 */
void
sp_item_write_transform(SPItem *item, SPRepr *repr, NR::Matrix const &transform, NR::Matrix const *adv)
{
    g_return_if_fail(item != NULL);
    g_return_if_fail(SP_IS_ITEM(item));
    g_return_if_fail(repr != NULL);

    // calculate the relative transform, if not given by the adv attribute
    NR::Matrix advertized_transform;
    if (adv != NULL) {
        advertized_transform = *adv;
    } else {
        advertized_transform = sp_item_transform_repr (item).inverse() * transform;
    }

     // recursively compensate for stroke scaling, depending on user preference
    if (prefs_get_int_attribute("options.transform", "stroke", 1) == 0) {
        double const expansion = 1. / NR::expansion(advertized_transform);
        sp_item_adjust_stroke_width_recursive(item, expansion);
    }

    // recursively compensate rx/ry of a rect if requested
    if (prefs_get_int_attribute("options.transform", "rectcorners", 1) == 0) {
        sp_item_adjust_rects_recursive(item, advertized_transform);
    }

    // recursively compensate pattern fill if it's not to be transformed 
    if (prefs_get_int_attribute("options.transform", "pattern", 1) == 0) {
        sp_item_adjust_paint_recursive (item, advertized_transform.inverse(), NR::identity(), true);
    }

    // recursively compensate gradient fill if it's not to be transformed
    if (prefs_get_int_attribute("options.transform", "gradient", 1) == 0) {
        sp_item_adjust_paint_recursive (item, advertized_transform.inverse(), NR::identity(), false);
    } else {
        // this converts the gradient fill, if any, to userspace; we need to do it here _before_ the new transform is set, so as to use the pre-transform bbox
        sp_item_adjust_paint_recursive (item, NR::identity(), NR::identity(), false);
    }

    // run the object's set_transform if transforms are stored optimized
    gint preserve = prefs_get_int_attribute("options.preservetransform", "value", 0);
    NR::Matrix transform_attr (transform);
    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->set_transform && !preserve) {
        transform_attr = ((SPItemClass *) G_OBJECT_GET_CLASS(item))->set_transform(item, transform);
    }
    sp_item_set_item_transform(item, transform_attr);

    // send the relative transform with a _transformed_signal
    item->_transformed_signal.emit(&advertized_transform, item);

    SP_OBJECT(item)->updateRepr();
}

gint
sp_item_event(SPItem *item, SPEvent *event)
{
    g_return_val_if_fail(item != NULL, FALSE);
    g_return_val_if_fail(SP_IS_ITEM(item), FALSE);
    g_return_val_if_fail(event != NULL, FALSE);

    if (((SPItemClass *) G_OBJECT_GET_CLASS(item))->event)
        return ((SPItemClass *) G_OBJECT_GET_CLASS(item))->event(item, event);

    return FALSE;
}

/* Sets item private transform (not propagated to repr) */

static void sp_item_set_item_transform(SPItem *item, NR::Matrix const &transform)
{
    g_return_if_fail(item != NULL);
    g_return_if_fail(SP_IS_ITEM(item));

    if (!matrix_equalp(transform, item->transform, NR_EPSILON)) {
        item->transform = transform;
        /* The SP_OBJECT_USER_MODIFIED_FLAG_B is used to mark the fact that it's only a
           transformation.  It's apparently not used anywhere else. */
        item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_OBJECT_USER_MODIFIED_FLAG_B);
        sp_item_rm_unsatisfied_cns(*item);
    }
}


/**
 * \pre \a ancestor really is an ancestor (\>=) of \a object.
 *   ("Ancestor (\>=)" here includes as far as \a object itself.)
 *
 * \pre in_same_coordsys_as_anc(object, ancestor).
 */
NR::Matrix
i2anc_affine(SPObject const *object, SPObject const *const ancestor) {
    NR::Matrix ret(NR::identity());
    g_return_val_if_fail(object != NULL && ancestor != NULL, ret);

    while ( object != ancestor ) {
        g_return_val_if_fail(SP_IS_ITEM(object), ret);
        /* g_error is correct if object is <defs> or a non-SVG element type.
         *
         * I wonder if there are cases (perhaps involving a nested <svg> inside a foreignObject)
         * where some different behaviour is appropriate.  We'll wait for a bug report and example
         * document to decide what to do.
         */

        ret *= SP_ITEM(object)->transform;
        object = SP_OBJECT_PARENT(object);
    }
    return ret;
}

NR::Matrix
i2i_affine(SPObject const *src, SPObject const *dest) {
    g_return_val_if_fail(src != NULL && dest != NULL, NR::identity());
    SPObject const *ancestor = src->nearestCommonAncestor(dest);
    return i2anc_affine(src, ancestor) / i2anc_affine(dest, ancestor);
}

NR::Matrix SPItem::getRelativeTransform(SPObject const *dest) const {
    return i2i_affine(this, dest);
}


/**
 * Returns the accumulated transformation of the item and all its ancestors, including root's viewport.
 * \pre (item != NULL) and SP_IS_ITEM(item).
 */
NR::Matrix sp_item_i2doc_affine(SPItem const *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    NR::Matrix ret(NR::identity());
#ifdef NDEBUG
    g_assert(ret.test_identity());
#endif

    /* Note that this routine may be called for items that are not members of
    ** the root.  For example, markers are members of a <def> object.  Hence
    ** we terminate either when reaching the top of the tree, or when a
    ** non-item object is reached.
    */
    while ( NULL != SP_OBJECT_PARENT(item) && SP_IS_ITEM(SP_OBJECT_PARENT(item)) ) {
        ret *= item->transform;
        item = SP_ITEM(SP_OBJECT_PARENT(item));
    }

    /* Then we only do root-related stuff if we found a root item */
    if (SP_IS_ROOT(item)) {
        SPRoot const *root = SP_ROOT(item);

        // Viewbox is relative to root's transform:
        // http://www.w3.org/TR/SVG11/coords.html#ViewBoxAttributeEffectOnSiblingAttributes
        ret *= root->c2p;
        ret *= item->transform;
    }

    return ret;
}

/**
 * Returns the accumulated transformation of the item and all its ancestors, but excluding root's viewport.
 * Used in path operations mostly.
 * \pre (item != NULL) and SP_IS_ITEM(item).
 */
NR::Matrix sp_item_i2root_affine(SPItem const *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    NR::Matrix ret(NR::identity());
    g_assert(ret.test_identity());
    while ( NULL != SP_OBJECT_PARENT(item) ) {
        ret *= item->transform;
        item = SP_ITEM(SP_OBJECT_PARENT(item));
    }
    g_assert(SP_IS_ROOT(item));

    ret *= item->transform;

    return ret;
}

NRMatrix *sp_item_i2doc_affine(SPItem const *item, NRMatrix *affine)
{
    g_return_val_if_fail(item != NULL, NULL);
    g_return_val_if_fail(SP_IS_ITEM(item), NULL);
    g_return_val_if_fail(affine != NULL, NULL);

    *affine = sp_item_i2doc_affine(item);
    return affine;
}

NRMatrix *sp_item_i2root_affine(SPItem const *item, NRMatrix *affine)
{
    g_return_val_if_fail(item != NULL, NULL);
    g_return_val_if_fail(SP_IS_ITEM(item), NULL);
    g_return_val_if_fail(affine != NULL, NULL);

    *affine = sp_item_i2root_affine(item);
    return affine;
}

/* fixme: This is EVIL!!! */

NR::Matrix sp_item_i2d_affine(SPItem const *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    NR::Matrix const ret( sp_item_i2doc_affine(item)
                          * NR::scale(0.8, -0.8)
                          * NR::translate(0, sp_document_height(SP_OBJECT_DOCUMENT(item))) );
#ifdef NDEBUG
    NRMatrix tst;
    sp_item_i2d_affine(item, &tst);
    assert_close( ret, NR::Matrix(&tst) );
#endif
    return ret;
}

// same as i2d but with i2root instead of i2doc
NR::Matrix sp_item_i2r_affine(SPItem const *item)
{
    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));

    NR::Matrix const ret( sp_item_i2root_affine(item)
                          * NR::scale(0.8, -0.8)
                          * NR::translate(0, sp_document_height(SP_OBJECT_DOCUMENT(item))) );
    return ret;
}

NRMatrix *sp_item_i2d_affine(SPItem const *item, NRMatrix *affine)
{
    g_return_val_if_fail(item != NULL, NULL);
    g_return_val_if_fail(SP_IS_ITEM(item), NULL);
    g_return_val_if_fail(affine != NULL, NULL);

    sp_item_i2doc_affine(item, affine);

    NRMatrix doc2dt;
    nr_matrix_set_scale(&doc2dt, 0.8, -0.8);
    doc2dt.c[5] = sp_document_height(SP_OBJECT_DOCUMENT(item));

    nr_matrix_multiply(affine, affine, &doc2dt);

    return affine;
}

void sp_item_set_i2d_affine(SPItem *item, NR::Matrix const &i2dt)
{
    g_return_if_fail( item != NULL );
    g_return_if_fail( SP_IS_ITEM(item) );

    NR::Matrix dt2p; /* desktop to item parent transform */
    if (SP_OBJECT_PARENT(item)) {
        dt2p = sp_item_i2d_affine((SPItem *) SP_OBJECT_PARENT(item)).inverse();
    } else {
        dt2p = ( NR::translate(0, -sp_document_height(SP_OBJECT_DOCUMENT(item)))
                 * NR::scale(1.25, -1.25) );
    }

    NR::Matrix const i2p( i2dt * dt2p );
    sp_item_set_item_transform(item, i2p);
}


NR::Matrix
sp_item_dt2i_affine(SPItem const *item, SPDesktop *)
{
    /* fixme: Implement the right way (Lauris) */
    return sp_item_i2d_affine(item).inverse();
}

NRMatrix *
sp_item_dt2i_affine(SPItem const *item, SPDesktop *dt, NRMatrix *affine)
{
    /* fixme: Implement the right way (Lauris) */
    NRMatrix i2dt;
    sp_item_i2d_affine(item, &i2dt);
    nr_matrix_invert(affine, &i2dt);
    return affine;
}

/* Item views */

static SPItemView *
sp_item_view_new_prepend(SPItemView *list, SPItem *item, unsigned flags, unsigned key, NRArenaItem *arenaitem)
{
    SPItemView *new_view;

    g_assert(item != NULL);
    g_assert(SP_IS_ITEM(item));
    g_assert(arenaitem != NULL);
    g_assert(NR_IS_ARENA_ITEM(arenaitem));

    new_view = g_new(SPItemView, 1);

    new_view->next = list;
    new_view->flags = flags;
    new_view->key = key;
    new_view->arenaitem = arenaitem;

    return new_view;
}

static SPItemView *
sp_item_view_list_remove(SPItemView *list, SPItemView *view)
{
    if (view == list) {
        list = list->next;
    } else {
        SPItemView *prev;
        prev = list;
        while (prev->next != view) prev = prev->next;
        prev->next = view->next;
    }

    g_free(view);

    return list;
}

/**
Return the arenaitem corresponding to the given item in the display with the given key
 */
NRArenaItem *
sp_item_get_arenaitem(SPItem *item, unsigned key)
{
    for ( SPItemView *iv = item->display ; iv ; iv = iv->next ) {
        if ( iv->key == key ) {
            return iv->arenaitem;
        }
    }

    return NULL;
}

int
sp_item_repr_compare_position(SPItem *first, SPItem *second)
{
    return sp_repr_compare_position(SP_OBJECT_REPR(first),
                                    SP_OBJECT_REPR(second));
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
