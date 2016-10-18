/***********************************************************************
WaterRenderer - Class to render a water surface defined by regular grids
of vertex-centered bathymetry and cell-centered water level values.
Copyright (c) 2014 Oliver Kreylos

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

#ifndef WATERRENDERER_INCLUDED
#define WATERRENDERER_INCLUDED

#include <GL/gl.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/GLObject.h>
#include <GL/GLGeometryVertex.h>

#include "Types.h"

/* Forward declarations: */
class WaterTable2;

class WaterRenderer:public GLObject
	{
	/* Embedded classes: */
	private:
	typedef GLGeometry::Vertex<void,0,void,0,void,GLfloat,2> Vertex; // Type for template vertices
	
	struct DataItem:public GLObject::DataItem // Structure storing per-context OpenGL state
		{
		/* Elements: */
		public:
		
		/* OpenGL state management: */
		GLuint vertexBuffer; // ID of vertex buffer object holding water surface's template vertices
		GLuint indexBuffer; // ID of index buffer object holding water surface's triangles
		
		/* GLSL shader management: */
		GLhandleARB waterShader; // Shader program to render the water surface
		GLint waterShaderUniforms[5]; // Locations of the water shader's uniform variables
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	const WaterTable2* waterTable; // Water table whose water surface is rendered
	unsigned int bathymetryGridSize[2]; // Size of vertex-centered bathymetry grid
	unsigned int waterGridSize[2]; // Size of cell-centered water level grid; one cell larger than bathymetry grid
	GLfloat cellSize[2]; // Cell size of the bathymetry and water level grids in world coordinate units
	PTransform gridTransform; // Vertex transformation from grid space to world space
	PTransform tangentGridTransform; // Transposed tangent plane transformation from grid space to world space
	
	/* Constructors and destructors: */
	public:
	WaterRenderer(const WaterTable2* sWaterTable); // Creates a water renderer for the given water table
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	void render(const PTransform& projection,const OGTransform& modelview,GLContextData& contextData) const; // Renders the water surface
	};

#endif
