#define __NR_OBJECT_C__

/*
 * RGBA display list system for inkscape
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   MenTaLguY <mental@rydia.net>
 *
 * This code is in public domain
 */

#include <new>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libnr/nr-macros.h>

#include "nr-object.h"

unsigned int
nr_emit_fail_warning (const gchar *file, unsigned int line, const gchar *method, const gchar *expr)
{
	fprintf (stderr, "File %s line %d (%s): Assertion %s failed\n", file, line, method, expr);
	return 1;
}

/* NRObject */

static NRObjectClass **classes = NULL;
static unsigned int classes_len = 0;
static unsigned int classes_size = 0;

NRType
nr_type_is_a (NRType type, NRType test)
{
	NRObjectClass *klass;

	nr_return_val_if_fail (type < classes_len, FALSE);
	nr_return_val_if_fail (test < classes_len, FALSE);

	klass = classes[type];

	while (klass) {
		if (klass->type == test) return TRUE;
		klass = klass->parent;
	}

	return FALSE;
}

void *
nr_object_check_instance_cast (void *ip, NRType tc)
{
	nr_return_val_if_fail (ip != NULL, NULL);
	nr_return_val_if_fail (nr_type_is_a (((NRObject *) ip)->klass->type, tc), ip);
	return ip;
}

unsigned int
nr_object_check_instance_type (void *ip, NRType tc)
{
	if (ip == NULL) return FALSE;
	return nr_type_is_a (((NRObject *) ip)->klass->type, tc);
}

NRType
nr_object_register_type (NRType parent,
			 gchar const *name,
			 unsigned int csize,
			 unsigned int isize,
			 void (* cinit) (NRObjectClass *),
			 void (* iinit) (NRObject *))
{
	NRType type;
	NRObjectClass *klass;

	if (classes_len >= classes_size) {
		classes_size += 32;
		classes = nr_renew (classes, NRObjectClass *, classes_size);
		if (classes_len == 0) {
			classes[0] = NULL;
			classes_len = 1;
		}
	}

	type = classes_len;
	classes_len += 1;

	classes[type] = (NRObjectClass*)new char[csize];
	klass = classes[type];
	memset (klass, 0, csize);

	if (classes[parent]) {
		memcpy (klass, classes[parent], classes[parent]->csize);
	}

	klass->type = type;
	klass->parent = classes[parent];
	klass->name = strdup (name);
	klass->csize = csize;
	klass->isize = isize;
	klass->cinit = cinit;
	klass->iinit = iinit;

	klass->cinit (klass);

	return type;
}

static void nr_object_class_init (NRObjectClass *klass);
static void nr_object_init (NRObject *object);
static void nr_object_finalize (NRObject *object);

NRType
nr_object_get_type (void)
{
	static NRType type = 0;
	if (!type) {
		type = nr_object_register_type (0,
						"NRObject",
						sizeof (NRObjectClass),
						sizeof (NRObject),
						(void (*) (NRObjectClass *)) nr_object_class_init,
						(void (*) (NRObject *)) nr_object_init);
	}
	return type;
}

static void
nr_object_class_init (NRObjectClass *klass)
{
	klass->finalize = nr_object_finalize;
	klass->cpp_ctor = NRObject::invoke_ctor<NRObject>;
}

static void nr_object_init (NRObject *object)
{
}

static void nr_object_finalize (NRObject *object)
{
}

/* Dynamic lifecycle */

static void
nr_class_tree_object_invoke_init (NRObjectClass *klass, NRObject *object)
{
	if (klass->parent) {
		nr_class_tree_object_invoke_init (klass->parent, object);
	}
	klass->iinit (object);
}

namespace {

void perform_finalization(void *base, void *obj) {
	NRObject *object=reinterpret_cast<NRObject *>(obj);
	object->klass->finalize(object);
	object->~NRObject();
}

}

NRObject *NRObject::alloc(NRType type) {
	nr_return_val_if_fail (type < classes_len, NULL);

	NRObjectClass *klass=classes[type];

	if ( klass->parent && klass->cpp_ctor == klass->parent->cpp_ctor ) {
		g_error("Cannot instantiate NRObject class %s which has not registered a C++ constructor\n", klass->name);
	}

	NRObject *object = reinterpret_cast<NRObject *>(new (Inkscape::GC::SCANNED) char[klass->isize]);
	GC_register_finalizer_ignore_self(GC_base(object), perform_finalization, object, NULL, NULL);
	memset(object, 0xf0, klass->isize);

	klass->cpp_ctor(object);
	object->klass = klass; // one of our parent constructors resets this :/
	nr_class_tree_object_invoke_init (klass, object);

	return object;
}

/* NRActiveObject */

static void nr_active_object_class_init (NRActiveObjectClass *klass);
static void nr_active_object_init (NRActiveObject *object);
static void nr_active_object_finalize (NRObject *object);

static NRObjectClass *parent_class;

NRType
nr_active_object_get_type (void)
{
	static NRType type = 0;
	if (!type) {
		type = nr_object_register_type (NR_TYPE_OBJECT,
						"NRActiveObject",
						sizeof (NRActiveObjectClass),
						sizeof (NRActiveObject),
						(void (*) (NRObjectClass *)) nr_active_object_class_init,
						(void (*) (NRObject *)) nr_active_object_init);
	}
	return type;
}

static void
nr_active_object_class_init (NRActiveObjectClass *klass)
{
	NRObjectClass *object_class;

	object_class = (NRObjectClass *) klass;

	parent_class = ((NRObjectClass *) klass)->parent;

	object_class->finalize = nr_active_object_finalize;
	object_class->cpp_ctor = NRObject::invoke_ctor<NRActiveObject>;
}

static void
nr_active_object_init (NRActiveObject *object)
{
}

static void
nr_active_object_finalize (NRObject *object)
{
	NRActiveObject *aobject;

	aobject = (NRActiveObject *) object;

	if (aobject->callbacks) {
		unsigned int i;
		for (i = 0; i < aobject->callbacks->length; i++) {
			NRObjectListener *listener;
			listener = aobject->callbacks->listeners + i;
			if (listener->vector->dispose) listener->vector->dispose (object, listener->data);
		}
		free (aobject->callbacks);
	}

	((NRObjectClass *) (parent_class))->finalize (object);
}

void
nr_active_object_add_listener (NRActiveObject *object, const NRObjectEventVector *vector, unsigned int size, void *data)
{
	NRObjectListener *listener;

	if (!object->callbacks) {
		object->callbacks = (NRObjectCallbackBlock*)malloc (sizeof (NRObjectCallbackBlock));
		object->callbacks->size = 1;
		object->callbacks->length = 0;
	}
	if (object->callbacks->length >= object->callbacks->size) {
		int newsize;
		newsize = object->callbacks->size << 1;
		object->callbacks = (NRObjectCallbackBlock*)realloc (object->callbacks, sizeof (NRObjectCallbackBlock) + (newsize - 1) * sizeof (NRObjectListener));
		object->callbacks->size = newsize;
	}
	listener = object->callbacks->listeners + object->callbacks->length;
	listener->vector = vector;
	listener->size = size;
	listener->data = data;
	object->callbacks->length += 1;
}

void
nr_active_object_remove_listener_by_data (NRActiveObject *object, void *data)
{
	if (object->callbacks) {
		unsigned int i;
		for (i = 0; i < object->callbacks->length; i++) {
			NRObjectListener *listener;
			listener = object->callbacks->listeners + i;
			if (listener->data == data) {
				object->callbacks->length -= 1;
				if (object->callbacks->length < 1) {
					free (object->callbacks);
					object->callbacks = NULL;
				} else if (object->callbacks->length != i) {
					*listener = object->callbacks->listeners[object->callbacks->length];
				}
				return;
			}
		}
	}
}


