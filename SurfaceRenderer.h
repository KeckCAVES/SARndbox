/***********************************************************************
SurfaceRenderer - Class to render a surface defined by a regular grid in
depth image space.
Copyright (c) 2012-2015 Oliver Kreylos

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

#ifndef SURFACERENDERER_INCLUDED
#define SURFACERENDERER_INCLUDED

#include <IO/FileMonitor.h>
#include <Geometry/Plane.h>
#include <Geometry/ProjectiveTransformation.h>
#include <GL/gl.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/GLObject.h>
#include <Kinect/FrameBuffer.h>

/* Forward declarations: */
class GLLightTracker;
class WaterTable2;

class SurfaceRenderer:public GLObject
	{
	/* Embedded classes: */
	public:
	typedef double Scalar;
	typedef Geometry::Plane<Scalar,3> Plane; // Type for planes in camera space
	typedef Geometry::ProjectiveTransformation<Scalar,3> PTransform; // Type for projective transformations
	
	private:
	struct DataItem:public GLObject::DataItem
		{
		/* Elements: */
		public:
		GLuint vertexBuffer; // ID of vertex buffer object holding surface's template vertices
		GLuint indexBuffer; // ID of index buffer object holding surface's triangles
		GLuint depthTexture; // ID of texture object holding surface's vertex elevations in depth image space
		unsigned int depthTextureVersion; // Version number of the depth image texture
		GLhandleARB depthShader; // Shader program to render the surface's depth only
		GLint depthShaderUniforms[2]; // Locations of the depth shader's uniform variables
		GLhandleARB elevationShader; // Shader program to render the surface's elevation relative to a plane
		GLint elevationShaderUniforms[3]; // Locations of the elevation shader's uniform variables
		GLuint contourLineFramebufferSize[2]; // Current width and height of contour line rendering frame buffer
		GLuint contourLineFramebufferObject; // Frame buffer object used to render topographic contour lines
		GLuint contourLineDepthBufferObject; // Depth render buffer for topographic contour line frame buffer
		GLuint contourLineColorTextureObject; // Color texture object for topographic contour line frame buffer
		GLhandleARB heightMapShader; // Shader program to render the surface using a height color map
		GLint heightMapShaderUniforms[13]; // Locations of the height map shader's uniform variables
		unsigned int surfaceSettingsVersion; // Version number of surface settings for which the height map shader was built
		unsigned int lightTrackerVersion; // Version number of light tracker state for which the height map shader was built
		GLhandleARB globalAmbientHeightMapShader; // Shader program to render the global ambient component of the surface using a height color map
		GLint globalAmbientHeightMapShaderUniforms[10]; // Locations of the global ambient height map shader's uniform variables
		GLhandleARB shadowedIlluminatedHeightMapShader; // Shader program to render the surface using illumination with shadows and a height color map
		GLint shadowedIlluminatedHeightMapShaderUniforms[13]; // Locations of the shadowed illuminated height map shader's uniform variables
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	IO::FileMonitor fileMonitor; // Monitor to watch the renderer's external shader source files
	unsigned int size[2]; // Width and height of the depth image
	PTransform depthProjection; // The transformation from depth image space to camera space
	GLfloat depthProjectionMatrix[16]; // Same, in GLSL-compatible format
	GLfloat tangentDepthProjectionMatrix[16]; // Depth projection for tangent planes in GLSL-compatible format
	Plane basePlane; // Base plane to calculate surface elevation
	GLfloat basePlaneEq[4]; // Base plane equation in GLSL-compatible format
	bool usePreboundDepthTexture; // Flag if the renderer should use the depth texture already bound to texture unit 0
	bool drawContourLines; // Flag if topographic contour lines are enabled
	GLfloat contourLineFactor; // Inverse elevation distance between adjacent topographic contour lines
	bool useHeightMap; // Flag whether to use a height color map for the surface
	GLfloat heightMapScale,heightMapOffset; // Scale and offset values to convert from elevation to height color map texture coordinates
	bool illuminate; // Flag whether the surface shall be illuminated
	WaterTable2* waterTable; // Pointer to the water table object; if NULL, water is ignored
	bool advectWaterTexture; // Flag whether water texture coordinates are advected to visualize water flow
	unsigned int surfaceSettingsVersion; // Version number of surface settings to invalidate surface rendering shader on changes
	GLfloat waterOpacity; // Scaling factor for water opacity
	Kinect::FrameBuffer depthImage; // The most recent float-pixel depth image
	unsigned int depthImageVersion; // Version number of the depth image
	double animationTime; // Time value for water animation
	
	/* Private methods: */
	void shaderSourceFileChanged(const IO::FileMonitor::Event& event); // Callback called when one of the external shader source files is changed
	GLhandleARB createSinglePassSurfaceShader(const GLLightTracker& lt,GLint* uniformLocations) const; // Creates a single-pass surface rendering shader based on current renderer settings
	
	/* Constructors and destructors: */
	public:
	SurfaceRenderer(const unsigned int sSize[2],const PTransform& sDepthProjection,const Plane& sBasePlane); // Creates a renderer for the given image size, depth projection, and base plane
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	void setUsePreboundDepthTexture(bool newUsePreboundDepthTexture); // Enables or disables using a pre-bound depth texture
	void setDrawContourLines(bool newDrawContourLines); // Enables or disables topographic contour lines
	void setContourLineDistance(GLfloat newContourLineDistance); // Sets the elevation distance between adjacent topographic contour lines
	void setUseHeightMap(bool newUseHeightMap); // Enable or disable height-based surface coloring
	void setHeightMapRange(GLsizei newHeightMapSize,GLfloat newMinElevation,GLfloat newMaxElevation); // Sets the elevation range for height color mapping
	void setIlluminate(bool newIlluminate); // Sets the illumination flag
	void setWaterTable(WaterTable2* newWaterTable); // Sets the pointer to the water table; NULL disables water handling
	void setAdvectWaterTexture(bool newAdvectWaterTexture); // Sets the water texture coordinate advection flag
	void setWaterOpacity(GLfloat newWaterOpacity); // Sets the water opacity factor
	void setDepthImage(const Kinect::FrameBuffer& newDepthImage); // Sets a new depth image for subsequent surface rendering
	void setAnimationTime(double newAnimationTime); // Sets the time for water animation in seconds
	void glRenderDepthOnly(const PTransform& modelviewProjection,GLContextData& contextData) const; // Renders the surface into a pure depth buffer, for early z culling or shadow passes etc.
	void glRenderElevation(GLContextData& contextData) const; // Renders the surface's elevation relative to the base plane into the current frame buffer
	void glPrepareContourLines(GLContextData& contextData) const; // Prepares to render topographic contour lines by rendering the base plane-relative elevations of pixel corners into a frame buffer
	void glRenderSinglePass(GLuint heightColorMapTexture,GLContextData& contextData) const; // Renders the surface in a single pass using the current surface settings
	void glRenderGlobalAmbientHeightMap(GLuint heightColorMapTexture,GLContextData& contextData) const; // Renders the global ambient component of the surface as an illuminated height map in the current OpenGL context using the given pixel-corner elevation texture and 1D height color map
	void glRenderShadowedIlluminatedHeightMap(GLuint heightColorMapTexture,GLuint shadowTexture,const PTransform& shadowProjection,GLContextData& contextData) const; // Renders the surface as an illuminated height map in the current OpenGL context using the given pixel-corner elevation texture and 1D height color map
	};

#endif
