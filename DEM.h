/***********************************************************************
DEM - Class to represent digital elevation models (DEMs) as float-valued
texture objects.
Copyright (c) 2013-2016 Oliver Kreylos

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

#ifndef DEM_INCLUDED
#define DEM_INCLUDED

#include <GL/gl.h>
#include <GL/GLObject.h>

#include "Types.h"

class DEM:public GLObject
	{
	/* Embedded classes: */
	private:
	struct DataItem:public GLObject::DataItem
		{
		/* Elements: */
		public:
		GLuint textureObjectId; // ID of texture object holding digital elevation model
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	private:
	int demSize[2]; // Width and height of the DEM grid
	Scalar demBox[4]; // Lower-left and upper-right corner coordinates of the DEM
	float* dem; // Array of DEM elevation measurements
	OGTransform transform; // Transformation from camera space to DEM space (z up)
	Scalar verticalScale; // Vertical scale (exaggeration) factor
	Scalar verticalScaleBase; // Base elevation around which vertical scale is applied
	PTransform demTransform; // Full transformation matrix from camera space to DEM pixel space
	GLfloat demTransformMatrix[16]; // Full transformation matrix from camera space to DEM pixel space to upload to OpenGL
	
	/* Private methods: */
	void calcMatrix(void); // Calculates the camera space to DEM pixel space transformation
	
	/* Constructors and destructors: */
	public:
	DEM(void); // Creates an uninitialized DEM
	virtual ~DEM(void);
	
	/* Methods from class GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	void load(const char* demFileName); // Loads the DEM from the given file
	const Scalar* getDemBox(void) const // Returns the DEM's bounding box as lower-left x, lower-left y, upper-right x, upper-right y
		{
		return demBox;
		}
	float calcAverageElevation(void) const; // Calculates the average elevation of the DEM
	void setTransform(const OGTransform& newTransform,Scalar newVerticalScale,Scalar newVerticalScaleBase); // Sets the DEM transformation
	const PTransform& getDemTransform(void) const // Returns the full transformation from camera space to vertically-scaled DEM pixel space
		{
		return demTransform;
		}
	Scalar getVerticalScale(void) const // Returns the scaling factor from camera space elevations to DEM elevations
		{
		return transform.getScaling()/verticalScale;
		}
	void bindTexture(GLContextData& contextData) const; // Binds the DEM texture object to the currently active texture unit
	void uploadDemTransform(GLint location) const; // Uploads the DEM transformation into the GLSL 4x4 matrix at the given uniform location
	};

#endif
