/***********************************************************************
DepthImageRenderer - Class to centralize storage of raw or filtered
depth images on the GPU, and perform simple repetitive rendering tasks
such as rendering elevation values into a frame buffer.
Copyright (c) 2014-2018 Oliver Kreylos

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

#ifndef DEPTHIMAGERENDERER_INCLUDED
#define DEPTHIMAGERENDERER_INCLUDED

#include <GL/gl.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/GLObject.h>
#include <GL/GLGeometryVertex.h>
#include <Kinect/FrameBuffer.h>
#include <Kinect/FrameSource.h>

#include "Types.h"

class DepthImageRenderer:public GLObject
	{
	/* Embedded classes: */
	private:
	typedef GLGeometry::Vertex<void,0,void,0,void,GLfloat,2> Vertex; // Type for template vertices
	
	struct DataItem:public GLObject::DataItem // Structure storing per-context OpenGL state
		{
		/* Elements: */
		public:
		
		/* OpenGL state management: */
		GLuint vertexBuffer; // ID of vertex buffer object holding surface's template vertices
		GLuint indexBuffer; // ID of index buffer object holding surface's triangles
		GLuint depthTexture; // ID of texture object holding surface's vertex elevations in depth image space
		unsigned int depthTextureVersion; // Version number of the depth image texture
		
		/* GLSL shader management: */
		GLhandleARB depthShader; // Shader program to render the surface's depth only
		GLint depthShaderUniforms[2]; // Locations of the depth shader's uniform variables
		GLhandleARB elevationShader; // Shader program to render the surface's elevation relative to a plane
		GLint elevationShaderUniforms[4]; // Locations of the elevation shader's uniform variables
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	unsigned int depthImageSize[2]; // Size of depth image texture
	Kinect::LensDistortion lensDistortion; // 2D lens distortion parameters
	PTransform depthProjection; // Projection matrix from depth image space into 3D camera space
	GLfloat depthProjectionMatrix[16]; // Same, in GLSL-compatible format
	GLfloat weightDicEq[4]; // Equation to calculate the weight of a depth image-space point in 3D camera space
	Plane basePlane; // Base plane to calculate surface elevation
	GLfloat basePlaneDicEq[4]; // Base plane equation in depth image space in GLSL-compatible format
	
	/* Transient state: */
	Kinect::FrameBuffer depthImage; // The most recent float-pixel depth image
	unsigned int depthImageVersion; // Version number of the depth image
	
	/* Constructors and destructors: */
	public:
	DepthImageRenderer(const unsigned int sDepthImageSize[2]); // Creates an elevation renderer for the given depth image size
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	const unsigned int* getDepthImageSize(void) const // Returns the depth image size
		{
		return depthImageSize;
		}
	unsigned int getDepthImageSize(int index) const // Returns one component of the depth image size
		{
		return depthImageSize[index];
		}
	const PTransform& getDepthProjection(void) const // Returns the depth unprojection matrix
		{
		return depthProjection;
		}
	const Plane& getBasePlane(void) const // Returns the elevation base plane
		{
		return basePlane;
		}
	void setDepthProjection(const PTransform& newDepthProjection); // Sets a new depth unprojection matrix
	void setIntrinsics(const Kinect::FrameSource::IntrinsicParameters& ips); // Sets a new depth unprojection matrix and, if present, 2D lens distortion parameters
	void setBasePlane(const Plane& newBasePlane); // Sets a new base plane for elevation rendering
	void setDepthImage(const Kinect::FrameBuffer& newDepthImage); // Sets a new depth image for subsequent surface rendering
	Scalar intersectLine(const Point& p0,const Point& p1,Scalar elevationMin,Scalar elevationMax) const; // Intersects a line segment with the current depth image in camera space; returns intersection point's parameter along line
	unsigned int getDepthImageVersion(void) const // Returns the version number of the current depth image
		{
		return depthImageVersion;
		}
	void uploadDepthProjection(GLint location) const; // Uploads the depth unprojection matrix into the GLSL 4x4 matrix at the given uniform location
	void bindDepthTexture(GLContextData& contextData) const; // Binds the up-to-date depth texture image to the currently active texture unit
	void renderSurfaceTemplate(GLContextData& contextData) const; // Renders the template quad strip mesh using current OpenGL settings
	void renderDepth(const PTransform& projectionModelview,GLContextData& contextData) const; // Renders the surface into a pure depth buffer, for early z culling or shadow passes etc.
	void renderElevation(const PTransform& projectionModelview,GLContextData& contextData) const; // Renders the surface's elevation relative to the base plane into the current one-component floating-point valued frame buffer
	};

#endif
