/***********************************************************************
ElevationColorMap - Class to represent elevation color maps for
topographic maps.
Copyright (c) 2014-2016 Oliver Kreylos

This file is part of the Augmented Reality Sandbox (SARndbox).

The Augmented Reality Sandbox is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Augmented Reality Sandbox is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Augmented Reality Sandbox; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#ifndef ELEVATIONCOLORMAP_INCLUDED
#define ELEVATIONCOLORMAP_INCLUDED

#include <GL/gl.h>
#include <GL/GLColorMap.h>
#include <GL/GLTextureObject.h>

#include "Types.h"

/* Forward declarations: */
class DepthImageRenderer;

class ElevationColorMap:public GLColorMap,public GLTextureObject
	{
	/* Elements: */
	private:
	GLfloat texturePlaneEq[4]; // Texture mapping plane equation in GLSL-compatible format
	
	/* Constructors and destructors: */
	public:
	ElevationColorMap(const char* heightMapName); // Creates an elevation color map by loading the given height map file
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	void load(const char* heightMapName); // Overrides elevation color map by loading the given height map file
	void calcTexturePlane(const Plane& basePlane); // Calculates the texture mapping plane for the given base plane equation
	void calcTexturePlane(const DepthImageRenderer* depthImageRenderer); // Calculates the texture mapping plane for the given depth image renderer
	void bindTexture(GLContextData& contextData) const; // Binds the elevation color map texture object to the currently active texture unit
	void uploadTexturePlane(GLint location) const; // Uploads the texture mapping plane equation into the GLSL 4-vector at the given uniform location
	};

#endif
