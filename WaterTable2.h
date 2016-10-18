/***********************************************************************
WaterTable2 - Class to simulate water flowing over a surface using
improved water flow simulation based on Saint-Venant system of partial
differenctial equations.
Copyright (c) 2012-2016 Oliver Kreylos

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

#ifndef WATERTABLE2_INCLUDED
#define WATERTABLE2_INCLUDED

#include <vector>
#include <Misc/FunctionCalls.h>
#include <Geometry/Point.h>
#include <Geometry/Box.h>
#include <Geometry/OrthonormalTransformation.h>
#include <GL/gl.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/GLObject.h>
#include <GL/GLContextData.h>

#include "Types.h"

/* Forward declarations: */
class DepthImageRenderer;

typedef Misc::FunctionCall<GLContextData&> AddWaterFunction; // Type for render functions called to locally add water to the water table

class WaterTable2:public GLObject
	{
	/* Embedded classes: */
	public:
	typedef Geometry::Box<Scalar,3> Box;
	typedef Geometry::OrthonormalTransformation<Scalar,3> ONTransform;
	
	private:
	struct DataItem:public GLObject::DataItem // Structure holding per-context state
		{
		/* Elements: */
		public:
		GLuint bathymetryTextureObjects[2]; // Double-buffered one-component float color texture object holding the vertex-centered bathymetry grid
		int currentBathymetry; // Index of bathymetry texture containing the most recent bathymetry grid
		unsigned int bathymetryVersion; // Version number of the most recent bathymetry grid
		GLuint quantityTextureObjects[3]; // Double-buffered three-component color texture object holding the cell-centered conserved quantity grid (w, hu, hv)
		int currentQuantity; // Index of quantity texture containing the most recent conserved quantity grid
		GLuint derivativeTextureObject; // Three-component color texture object holding the cell-centered temporal derivative grid
		GLuint maxStepSizeTextureObjects[2]; // Double-buffered one-component color texture objects to gather the maximum step size for Runge-Kutta integration steps
		GLuint waterTextureObject; // One-component color texture object to add or remove water to/from the conserved quantity grid
		GLuint bathymetryFramebufferObject; // Frame buffer used to render the bathymetry surface into the bathymetry grid
		GLuint derivativeFramebufferObject; // Frame buffer used for temporal derivative computation
		GLuint maxStepSizeFramebufferObject; // Frame buffer used to calculate the maximum integration step size
		GLuint integrationFramebufferObject; // Frame buffer used for the Euler and Runge-Kutta integration steps
		GLuint waterFramebufferObject; // Frame buffer used for the water rendering step
		GLhandleARB bathymetryShader; // Shader to update cell-centered conserved quantities after a change to the bathymetry grid
		GLint bathymetryShaderUniformLocations[3];
		GLhandleARB waterAdaptShader; // Shader to adapt a new conserved quantity grid to the current bathymetry grid
		GLint waterAdaptShaderUniformLocations[2];
		GLhandleARB derivativeShader; // Shader to compute face-centered partial fluxes and cell-centered temporal derivatives
		GLint derivativeShaderUniformLocations[6];
		GLhandleARB maxStepSizeShader; // Shader to compute a maximum step size for a subsequent Runge-Kutta integration step
		GLint maxStepSizeShaderUniformLocations[2];
		GLhandleARB boundaryShader; // Shader to enforce boundary conditions on the quantities grid
		GLint boundaryShaderUniformLocations[1];
		GLhandleARB eulerStepShader; // Shader to compute an Euler integration step
		GLint eulerStepShaderUniformLocations[4];
		GLhandleARB rungeKuttaStepShader; // Shader to compute a Runge-Kutta integration step
		GLint rungeKuttaStepShaderUniformLocations[5];
		GLhandleARB waterAddShader; // Shader to render water adder objects
		GLint waterAddShaderUniformLocations[3];
		GLhandleARB waterShader; // Shader to add or remove water from the conserved quantities grid
		GLint waterShaderUniformLocations[3];
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	GLsizei size[2]; // Width and height of water table in pixels
	const DepthImageRenderer* depthImageRenderer; // Renderer object used to update the water table's bathymetry grid
	ONTransform baseTransform; // Transformation from camera space to upright elevation map space
	Box domain; // Domain of elevation map space in rotated camera space
	GLfloat cellSize[2]; // Width and height of water table cells in world coordinate units
	PTransform bathymetryPmv; // Combined projection and modelview matrix to render the current surface into the bathymetry grid
	PTransform waterAddPmv; // Combined projection and modelview matrix to render water-adding geometry into the water grid
	GLfloat waterAddPmvMatrix[16]; // Same, in GLSL-compatible format
	GLfloat theta; // Coefficient for minmod flux-limiting differential operator
	GLfloat g; // Gravitiational acceleration constant
	GLfloat epsilon; // Coefficient for desingularizing division operator
	GLfloat attenuation; // Attenuation factor for partial discharges
	GLfloat maxStepSize; // Maximum step size for each Runge-Kutta integration step
	PTransform waterTextureTransform; // Projective transformation from camera space to water level texture space
	GLfloat waterTextureTransformMatrix[16]; // Same in GLSL-compatible format
	std::vector<const AddWaterFunction*> renderFunctions; // A list of functions that are called after each water flow simulation step to locally add or remove water from the water table
	GLfloat waterDeposit; // A fixed amount of water added at every iteration of the flow simulation, for evaporation etc.
	bool dryBoundary; // Flag whether to enforce dry boundary conditions at the end of each simulation step
	unsigned int readBathymetryRequest; // Request token to read back the current bathymetry grid from the GPU
	mutable GLfloat* readBathymetryBuffer; // Buffer into which to read the current bathymetry grid
	mutable unsigned int readBathymetryReply; // Reply token after reading back the current bathymetry grid
	
	/* Private methods: */
	void calcTransformations(void); // Calculates derived transformations
	GLfloat calcDerivative(DataItem* dataItem,GLuint quantityTextureObject,bool calcMaxStepSize) const; // Calculates the temporal derivative of the conserved quantities in the given texture object and returns maximum step size if flag is true
	
	/* Constructors and destructors: */
	public:
	WaterTable2(GLsizei width,GLsizei height,const GLfloat sCellSize[2]); // Creates water table for offline simulation
	WaterTable2(GLsizei width,GLsizei height,const DepthImageRenderer* sDepthImageRenderer,const Point basePlaneCorners[4]); // Creates a water table of the given size in pixels, for the base plane quadrilateral defined by the depth image renderer's plane equation and four corner points
	virtual ~WaterTable2(void);
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	const GLsizei* getSize(void) const // Returns the size of the water table
		{
		return size;
		}
	const ONTransform& getBaseTransform(void) const // Returns the transformation from camera space to upright elevation map space
		{
		return baseTransform;
		}
	const Box& getDomain(void) const // Returns the water table's domain in rotated camera space
		{
		return domain;
		}
	const GLfloat* getCellSize(void) const // Returns the water table's cell size
		{
		return cellSize;
		}
	GLfloat getAttenuation(void) const // Returns the attenuation factor for partial discharges
		{
		return attenuation;
		}
	bool getDryBoundary(void) const // Returns true if dry boundaries are enforced after every simulation step
		{
		return dryBoundary;
		}
	void setElevationRange(Scalar newMin,Scalar newMax); // Sets the range of possible elevations in the water table
	void setAttenuation(GLfloat newAttenuation); // Sets the attenuation factor for partial discharges
	void setMaxStepSize(GLfloat newMaxStepSize); // Sets the maximum step size for all subsequent integration steps
	const PTransform& getWaterTextureTransform(void) const // Returns the matrix transforming from camera space into water texture space
		{
		return waterTextureTransform;
		}
	void addRenderFunction(const AddWaterFunction* newRenderFunction); // Adds a render function to the list; object remains owned by caller
	void removeRenderFunction(const AddWaterFunction* removeRenderFunction); // Removes the given render function from the list but does not delete it
	GLfloat getWaterDeposit(void) const // Returns the current amount of water deposited on every simulation step
		{
		return waterDeposit;
		}
	void setWaterDeposit(GLfloat newWaterDeposit); // Sets the amount of deposited water
	void setDryBoundary(bool newDryBoundary); // Enables or disables enforcement of dry boundaries
	void updateBathymetry(GLContextData& contextData) const; // Prepares the water table for subsequent calls to the runSimulationStep() method
	void updateBathymetry(const GLfloat* bathymetryGrid,GLContextData& contextData) const; // Updates the bathymetry directly with a vertex-centered elevation grid of grid size minus 1
	void setWaterLevel(const GLfloat* waterGrid,GLContextData& contextData) const; // Sets the current water level to the given grid, and resets flux components to zero
	GLfloat runSimulationStep(bool forceStepSize,GLContextData& contextData) const; // Runs a water flow simulation step, always uses maxStepSize if flag is true (may lead to instability); returns step size taken by Runge-Kutta integration step
	void bindBathymetryTexture(GLContextData& contextData) const; // Binds the bathymetry texture object to the active texture unit
	void bindQuantityTexture(GLContextData& contextData) const; // Binds the most recent conserved quantities texture object to the active texture unit
	void uploadWaterTextureTransform(GLint location) const; // Uploads the water texture transformation into the GLSL 4x4 matrix at the given uniform location
	GLsizei getBathymetrySize(int index) const // Returns the width or height of the bathymetry grid
		{
		return size[index]-1;
		}
	bool requestBathymetry(GLfloat* newReadBathymetryBuffer); // Requests reading back the current bathymetry grid from the GPU during the next rendering cycle; returns true if request can be granted
	bool haveBathymetry(void) const // Returns true if the most recent bathymetry request has been fulfilled
		{
		return readBathymetryReply==readBathymetryRequest;
		}
	};

#endif
