// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Functions to keep a listing of all modules in the system.  Has its
 * own file mostly for abstraction reasons, but is pretty simple
 * otherwise.
 *
 * Authors:
 *   Ted Gould <ted@gould.cx>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2002-2004 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef SEEN_MODULES_DB_H
#define SEEN_MODULES_DB_H

#include <map>
#include <list>
#include <cstring>

#include <glib.h>
#include <vector>


namespace Inkscape {
namespace Extension {

class Template; // New
class Input;    // Load
class Output;   // Save
class Effect;   // Modify
class Extension;

class DB {
private:
    /** A string comparison function to be used in the moduledict
        to find the different extensions in the hash map. */
    struct ltstr {
        bool operator()(const char* s1, const char* s2) const {
            if ( (s1 == nullptr) && (s2 != nullptr) ) {
                return true;
            } else if (s1 == nullptr || s2 == nullptr) {
                return false;
            } else {
                return strcmp(s1, s2) < 0;
            }
        }
    };
    /** This is the actual database.  It has all of the modules in it,
        indexed by their ids.  It's a hash table for faster lookups */
    std::map <const char *, Extension *, ltstr> moduledict;
    /** Maintain an ordered list of modules for generating the extension
        lists via "foreach" */
    std::list <Extension *> modulelist;

    static void foreach_internal (gpointer in_key, gpointer in_value, gpointer in_data);

public:
    DB ();
    Extension * get (const gchar *key) const;
    void register_ext (Extension *module);
    void unregister_ext (Extension *module);
    void foreach (void (*in_func)(Extension * in_plug, gpointer in_data), gpointer in_data);

private:
    static void template_internal(Extension *in_plug, gpointer data);
    static void input_internal (Extension * in_plug, gpointer data);
    static void output_internal (Extension * in_plug, gpointer data);
    static void effect_internal (Extension * in_plug, gpointer data);

public:
    typedef std::list<Template *> TemplateList;
    typedef std::list<Output *> OutputList;
    typedef std::list<Input *> InputList;
    typedef std::list<Effect *> EffectList;

    TemplateList &get_template_list(TemplateList &ou_list);
    InputList  &get_input_list  (InputList &ou_list);
    OutputList &get_output_list (OutputList &ou_list);

    std::vector<Effect*> get_effect_list();
}; /* class DB */

extern DB db;

} } /* namespace Extension, Inkscape */

#endif // SEEN_MODULES_DB_H

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
