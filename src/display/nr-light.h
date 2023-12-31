// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * TODO: insert short description here
 *//*
 * Authors: see git history
 *
 * Copyright (C) 2017 Authors
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#ifndef SEEN_NR_LIGHT_H
#define SEEN_NR_LIGHT_H

/** \file
 * These classes provide tools to compute interesting objects relative to light
 * sources. Each class provides a constructor converting information contained
 * in a sp light object into information useful in the current setting, a
 * method to get the light vector (at a given point) and a method to get the
 * light color components (at a given point).
 */

#include <2geom/forward.h>

#include "display/nr-3dutils.h"
#include "display/nr-light-types.h"

class SPFeDistantLight;
class SPFePointLight;
class SPFeSpotLight;
typedef unsigned int guint32;

namespace Inkscape {
namespace Filters {

enum LightComponent {
    LIGHT_RED = 0,
    LIGHT_GREEN,
    LIGHT_BLUE
};

class DistantLight {
    public:
        /**
         * Constructor
         *
         * \param light the sp light object
         * \param lighting_color the lighting_color used
         */
        DistantLight(DistantLightData const &light, guint32 lighting_color);
        virtual ~DistantLight();

        /**
         * Computes the light vector of the distant light
         *
         * \param v a Fvector reference where we store the result
         */
        void light_vector(NR::Fvector &v);

        /**
         * Computes the light components of the distant light
         *
         * \param lc a Fvector reference where we store the result, X=R, Y=G, Z=B
         */
        void light_components(NR::Fvector &lc);

    private:
        guint32 color;
        double azimuth; //azimuth in rad
        double elevation; //elevation in rad
};

class PointLight {
    public:
        /**
         * Constructor
         *
         * \param light the sp light object
         * \param lighting_color the lighting_color used
         * \param trans the transformation between absolute coordinate (those
         * employed in the sp light object) and current coordinate (those
         * employed in the rendering)
         * \param device_scale for high DPI monitors.
         */
        PointLight(PointLightData const &light, guint32 lighting_color, const Geom::Affine &trans, int device_scale = 1);
        virtual ~PointLight();
        /**
         * Computes the light vector of the distant light at point (x,y,z).
         * x, y and z are given in the arena_item coordinate, they are used as
         * is
         *
         * \param v a Fvector reference where we store the result
         * \param x x coordinate of the current point
         * \param y y coordinate of the current point
         * \param z z coordinate of the current point
         */
        void light_vector(NR::Fvector &v, double x, double y, double z);

        /**
         * Computes the light components of the distant light
         *
         * \param lc a Fvector reference where we store the result, X=R, Y=G, Z=B
         */
        void light_components(NR::Fvector &lc);

    private:
        guint32 color;
        //light position coordinates in render setting
        double l_x;
        double l_y;
        double l_z;
};

class SpotLight {
    public:
        /**
         * Constructor
         *
         * \param light the sp light object
         * \param lighting_color the lighting_color used
         * \param trans the transformation between absolute coordinate (those
         * employed in the sp light object) and current coordinate (those
         * employed in the rendering)
         * \param device_scale for high DPI monitors.
         */
        SpotLight(SpotLightData const &light, guint32 lighting_color, const Geom::Affine &trans, int device_scale = 1);
        virtual ~SpotLight();

        /**
         * Computes the light vector of the distant light at point (x,y,z).
         * x, y and z are given in the arena_item coordinate, they are used as
         * is
         *
         * \param v a Fvector reference where we store the result
         * \param x x coordinate of the current point
         * \param y y coordinate of the current point
         * \param z z coordinate of the current point
         */
        void light_vector(NR::Fvector &v, double x, double y, double z);

        /**
         * Computes the light components of the distant light at the current
         * point. We only need the light vector to compute these
         *
         * \param lc a Fvector reference where we store the result, X=R, Y=G, Z=B
         * \param L the light vector of the current point
         */
        void light_components(NR::Fvector &lc, const NR::Fvector &L);

    private:
        guint32 color;
        //light position coordinates in render setting
        double l_x;
        double l_y;
        double l_z;
        double cos_lca; //cos of the limiting cone angle
        double speExp; //specular exponent;
        NR::Fvector S; //unit vector from light position in the direction
                   //the spot point at
};


} /* namespace Filters */
} /* namespace Inkscape */

#endif // SEEN_INKSCAPE_NR_LIGHT_H
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
