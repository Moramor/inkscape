/*
 * The reference corresponding to href of <use> element.
 *
 * Copyright (C) 2004 Bulia Byak
 * Copyright (C) 2004 Monash University
 *
 * Released under GNU GPL, read the file 'COPYING' for more information.
 */

#include <forward.h>
#include "sp-use-reference.h"
#include "sp-object.h"
#include "sp-item.h"

#include "livarot/Path.h"
#include "livarot/Shape.h"

#include "sp-shape.h"
#include "sp-text.h"
#include "prefs-utils.h"

#include "libnr/nr-matrix.h"
#include "libnr/nr-matrix-ops.h"
#include "libnr/nr-point.h"
#include "libnr/nr-point-ops.h"


bool SPUseReference::_acceptObject(SPObject * const obj) const
{
    if (SP_IS_ITEM(obj)) {
        SPObject * const owner = getOwner();
        /* Refuse references to us or to an ancestor. */
        for ( SPObject *iter = owner ; iter ; iter = SP_OBJECT_PARENT(iter) ) {
            if ( iter == obj ) {
                return false;
            }
        }
        return true;
    } else {
        return false;
    }
}


static void sp_usepath_href_changed(SPObject *old_ref, SPObject *ref, SPUsePath *offset);
static void sp_usepath_move_compensate(NR::Matrix const *mp, SPItem *original, SPUsePath *self);
static void sp_usepath_delete_self(SPObject *deleted, SPUsePath *offset);
static void sp_usepath_source_modified (SPObject *iSource, guint flags, SPItem *item);

SPUsePath::SPUsePath(SPObject* i_owner):SPUseReference(i_owner)
{
	owner=i_owner;
  originalPath = NULL;
	sourceDirty=false;
  sourceHref = NULL;
  sourceRepr = NULL;
  sourceObject = NULL;
	new (&_delete_connection) sigc::connection();
	new (&_changed_connection) sigc::connection();
	new (&_transformed_connection) sigc::connection();
	_changed_connection = changedSignal().connect(sigc::bind(sigc::ptr_fun(sp_usepath_href_changed), this)); // listening to myself, this should be virtual instead
}
SPUsePath::~SPUsePath(void)
{
  if (originalPath) delete originalPath;
  originalPath = NULL;
  
	_changed_connection.disconnect(); // to do before unlinking

	quit_listening();
	unlink();

	_delete_connection.~Connection();
	_changed_connection.~Connection();
	_transformed_connection.~Connection();
}

void            
SPUsePath::link(char* to)
{
	if ( to == NULL ) {
		quit_listening();
		unlink();
	} else {
		if ( sourceHref && ( strcmp(to, sourceHref) == 0 ) ) {
		} else {
			if ( sourceHref ) g_free(sourceHref);
			sourceHref = g_strdup(to);
			try {
				attach(Inkscape::URI(to));
			} catch (Inkscape::BadURIException &e) {
				g_warning("%s", e.what());
				detach();
			}
		}
	}
}
void            
SPUsePath::unlink(void)
{
	if ( sourceHref ) g_free(sourceHref);
	sourceHref = NULL;
	detach();
}
void 
SPUsePath::start_listening(SPObject* to)
{
	if ( to == NULL ) return;	
	sourceObject = to;
	sourceRepr = SP_OBJECT_REPR(to);
	_delete_connection = to->connectDelete(sigc::bind(sigc::ptr_fun(&sp_usepath_delete_self), this));
	_transformed_connection = SP_ITEM(to)->connectTransformed(sigc::bind(sigc::ptr_fun(&sp_usepath_move_compensate), this));
	_modified_connection = g_signal_connect (G_OBJECT (to), "modified", G_CALLBACK (sp_usepath_source_modified), this);
}
void 
SPUsePath::quit_listening(void)
{
	if ( sourceObject == NULL )  return;
	g_signal_handler_disconnect (sourceObject, _modified_connection);
	_delete_connection.disconnect();
	_transformed_connection.disconnect();
	sourceRepr = NULL;
	sourceObject = NULL;
}

static void
sp_usepath_href_changed(SPObject */*old_ref*/, SPObject */*ref*/, SPUsePath *offset)
{
	offset->quit_listening();
	SPItem *refobj = offset->getObject();
	if ( refobj ) offset->start_listening(refobj);
	offset->sourceDirty=true;
	SP_OBJECT(offset->owner)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

static void
sp_usepath_move_compensate(NR::Matrix const *mp, SPItem *original, SPUsePath *self)
{	
	guint mode = prefs_get_int_attribute("options.clonecompensation", "value", SP_CLONE_COMPENSATION_PARALLEL);
	if (mode == SP_CLONE_COMPENSATION_NONE) return;
	SPItem *item = SP_ITEM(self->owner);
	
	/*	NR::Matrix m(*mp);
	if (!(m.is_translation())) return;
	NR::Matrix t = NR::Matrix(&(item->transform));
	NR::Matrix clone_move = t.inverse() * m * t;
	
	// calculate the compensation matrix and the advertized movement matrix
	NR::Matrix advertized_move;
	if (mode == SP_CLONE_COMPENSATION_PARALLEL) {
		//		clone_move = clone_move.inverse();
		advertized_move.set_identity();
	} else if (mode == SP_CLONE_COMPENSATION_UNMOVED) {
		clone_move = clone_move.inverse() * m;
		advertized_move = m;
	} else {
		g_assert_not_reached();
	}
	
	// commit the compensation
	NRMatrix clone_move_nr = clone_move;
	nr_matrix_multiply(&item->transform, &item->transform, &clone_move_nr);
	sp_item_write_transform(item, SP_OBJECT_REPR(item), &item->transform, &advertized_move);*/
	self->sourceDirty=true;
	SP_OBJECT(item)->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}
static void
sp_usepath_delete_self(SPObject */*deleted*/, SPUsePath *offset)
{
	guint const mode = prefs_get_int_attribute("options.cloneorphans", "value", SP_CLONE_ORPHANS_UNLINK);
	
	if (mode == SP_CLONE_ORPHANS_UNLINK) {
		// leave it be. just forget about the source
		offset->quit_listening();
		offset->unlink();
	} else if (mode == SP_CLONE_ORPHANS_DELETE) {
		offset->owner->deleteObject();
	}
}
static void
sp_usepath_source_modified (SPObject *iSource, guint flags, SPItem *item)
{
	SPUsePath *offset = (SPUsePath*)item;
	offset->sourceDirty=true;
	offset->owner->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void   SPUsePath::refresh_source(void)
{
  sourceDirty=false;
	if ( originalPath ) delete originalPath;
  originalPath = NULL;
	
  // le mauvais cas: pas d'attribut d => il faut verifier que c'est une SPShape puis prendre le contour
  SPObject *refobj=sourceObject;
  if ( refobj == NULL ) return;
  SPItem *item = SP_ITEM (refobj);
  
  SPCurve *curve=NULL;
  if (!SP_IS_SHAPE (item) && !SP_IS_TEXT (item)) return;
  if (SP_IS_SHAPE (item)) {
    curve = sp_shape_get_curve (SP_SHAPE (item));
    if (curve == NULL)
      return;
  }
  if (SP_IS_TEXT (item)) {
 	  curve = sp_text_normalized_bpath (SP_TEXT (item));
 	  if (curve == NULL)
	    return;
  }
	originalPath=new Path;
	NR::Matrix  dummy;
  originalPath->LoadArtBPath (curve->bpath,dummy,false);
  sp_curve_unref (curve);
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
