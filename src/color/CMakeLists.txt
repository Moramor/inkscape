# SPDX-License-Identifier: GPL-2.0-or-later

set(color_SRC
	cms-system.cpp
	cms-util.cpp
    cmyk-conv.cpp
	color-conv.cpp

	# -------
	# Headers
	color-profile-cms-fns.h
	cms-color-types.h
	cms-system.h
	cms-util.h
    cmyk-conv.h
	color-conv.h
)

add_inkscape_source("${color_SRC}")
