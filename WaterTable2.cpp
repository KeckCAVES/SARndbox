/***********************************************************************
WaterTable2 - Class to simulate water flowing over a surface using
improved water flow simulation based on Saint-Venant system of partial
differenctial equations.
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

#include "WaterTable2.h"

#include <stdarg.h>
#include <string>
#include <iostream>
#include <Misc/ThrowStdErr.h>
#include <Math/Math.h>
#include <Geometry/AffineCombiner.h>
#include <Geometry/Vector.h>
#include <GL/gl.h>
#include <GL/Extensions/GLARBDrawBuffers.h>
#include <GL/Extensions/GLARBFragmentShader.h>
#include <GL/Extensions/GLARBMultitexture.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/Extensions/GLARBTextureFloat.h>
#include <GL/Extensions/GLARBTextureRectangle.h>
#include <GL/Extensions/GLARBTextureRg.h>
#include <GL/Extensions/GLARBVertexShader.h>
#include <GL/Extensions/GLEXTFramebufferObject.h>
#include <GL/GLContextData.h>
#include <GL/GLTransformationWrappers.h>

#include "SurfaceRenderer.h"

namespace {

/****************
Helper functions:
****************/

GLfloat* makeBuffer(int width,int height,int numComponents,...)
	{
	va_list ap;
	va_start(ap,numComponents);
	GLfloat fill[4];
	for(int i=0;i<numComponents&&i<4;++i)
		fill[i]=GLfloat(va_arg(ap,double));
	va_end(ap);
	
	GLfloat* buffer=new GLfloat[height*width*numComponents];
	GLfloat* bPtr=buffer;
	for(int y=0;y<height;++y)
		for(int x=0;x<width;++x,bPtr+=numComponents)
			for(int i=0;i<numComponents;++i)
				bPtr[i]=fill[i];
	
	return buffer;
	}

GLhandleARB compileVertexShader(const char* shaderFileName)
	{
	/* Construct the full shader source file name: */
	std::string fullShaderFileName=SHADERDIR;
	fullShaderFileName.push_back('/');
	fullShaderFileName.append(shaderFileName);
	fullShaderFileName.append(".vs");
	
	/* Compile and return the vertex shader: */
	return glCompileVertexShaderFromFile(fullShaderFileName.c_str());
	}

GLhandleARB compileFragmentShader(const char* shaderFileName)
	{
	/* Construct the full shader source file name: */
	std::string fullShaderFileName=SHADERDIR;
	fullShaderFileName.push_back('/');
	fullShaderFileName.append(shaderFileName);
	fullShaderFileName.append(".fs");
	
	/* Compile and return the fragment shader: */
	return glCompileFragmentShaderFromFile(fullShaderFileName.c_str());
	}

}

/**************************************
Methods of class WaterTable2::DataItem:
**************************************/

WaterTable2::DataItem::DataItem(void)
	:quantityTextureObject(0),derivativeTextureObject(0),quantityStarTextureObject(0),waterTextureObject(0),
	 bathymetryFramebufferObject(0),derivativeFramebufferObject(0),maxStepSizeFramebufferObject(0),integrationFramebufferObject(0),waterFramebufferObject(0),
	 bathymetryShader(0),derivativeShader(0),maxStepSizeShader(0),boundaryShader(0),eulerStepShader(0),rungeKuttaStepShader(0),waterAddShader(0),waterShader(0)
	{
	for(int i=0;i<2;++i)
		{
		bathymetryTextureObjects[i]=0;
		maxStepSizeTextureObjects[i]=0;
		}
	
	/* Check for and initialize all required OpenGL extensions: */
	bool supported=GLARBDrawBuffers::isSupported();
	supported=supported&&GLARBFragmentShader::isSupported();
	supported=supported&&GLARBMultitexture::isSupported();
	supported=supported&&GLARBShaderObjects::isSupported();
	supported=supported&&GLARBTextureFloat::isSupported();
	supported=supported&&GLARBTextureRectangle::isSupported();
	supported=supported&&GLARBTextureRg::isSupported();
	supported=supported&&GLARBVertexShader::isSupported();
	supported=supported&&GLEXTFramebufferObject::isSupported();
	if(!supported)
		Misc::throwStdErr("WaterTable2: Required functionality not supported by local OpenGL");
	GLARBDrawBuffers::initExtension();
	GLARBFragmentShader::initExtension();
	GLARBMultitexture::initExtension();
	GLARBShaderObjects::initExtension();
	GLARBTextureFloat::initExtension();
	GLARBTextureRectangle::initExtension();
	GLARBTextureRg::initExtension();
	GLARBVertexShader::initExtension();
	GLEXTFramebufferObject::initExtension();
	}

WaterTable2::DataItem::~DataItem(void)
	{
	/* Delete all allocated shaders, textures, and buffers: */
	glDeleteTextures(2,bathymetryTextureObjects);
	glDeleteTextures(1,&quantityTextureObject);
	glDeleteTextures(1,&derivativeTextureObject);
	glDeleteTextures(2,maxStepSizeTextureObjects);
	glDeleteTextures(1,&quantityStarTextureObject);
	glDeleteTextures(1,&waterTextureObject);
	glDeleteFramebuffersEXT(1,&bathymetryFramebufferObject);
	glDeleteFramebuffersEXT(1,&derivativeFramebufferObject);
	glDeleteFramebuffersEXT(1,&maxStepSizeFramebufferObject);
	glDeleteFramebuffersEXT(1,&integrationFramebufferObject);
	glDeleteFramebuffersEXT(1,&waterFramebufferObject);
	glDeleteObjectARB(bathymetryShader);
	glDeleteObjectARB(derivativeShader);
	glDeleteObjectARB(maxStepSizeShader);
	glDeleteObjectARB(boundaryShader);
	glDeleteObjectARB(eulerStepShader);
	glDeleteObjectARB(rungeKuttaStepShader);
	glDeleteObjectARB(waterAddShader);
	glDeleteObjectARB(waterShader);
	}

/************************************
Static elements of class WaterTable2:
************************************/

const char* WaterTable2::vertexShaderSource="\
	void main()\n\
		{\n\
		/* Use standard vertex position: */\n\
		gl_Position=ftransform();\n\
		}\n";

/****************************
Methods of class WaterTable2:
****************************/

GLfloat WaterTable2::calcDerivative(WaterTable2::DataItem* dataItem,GLuint quantityTextureObject,bool calcMaxStepSize) const
	{
	/*********************************************************************
	Step 1: Calculate partial spatial derivatives, partial fluxes across
	cell boundaries, and the temporal derivative.
	*********************************************************************/
	
	/* Set up the derivative computation frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->derivativeFramebufferObject);
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the temporal derivative computation shader: */
	glUseProgramObjectARB(dataItem->derivativeShader);
	glUniformARB<2>(dataItem->derivativeShaderUniformLocations[0],1,cellSize);
	glUniformARB(dataItem->derivativeShaderUniformLocations[1],theta);
	glUniformARB(dataItem->derivativeShaderUniformLocations[2],g);
	glUniformARB(dataItem->derivativeShaderUniformLocations[3],epsilon);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->derivativeShaderUniformLocations[4],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,quantityTextureObject);
	glUniform1iARB(dataItem->derivativeShaderUniformLocations[5],1);
	
	/* Run the temporal derivative computation: */
	glBegin(GL_QUADS);
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	glEnd();
	
	/* Unbind unneeded textures: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/*********************************************************************
	Step 2: Gather the maximum step size by reducing the maximum step size
	texture.
	*********************************************************************/
	
	GLfloat stepSize=maxStepSize;
	
	if(calcMaxStepSize)
		{
		/* Set up the maximum step size reduction shader: */
		glUseProgramObjectARB(dataItem->maxStepSizeShader);
		
		/* Bind the maximum step size computation frame buffer: */
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->maxStepSizeFramebufferObject);
		
		/* Reduce the maximum step size texture in a sequence of half-reduction steps: */
		int reducedWidth=size[0];
		int reducedHeight=size[1];
		int currentMaxStepSizeTexture=0;
		while(reducedWidth>1||reducedHeight>1)
			{
			/* Set up the simulation frame buffer for maximum step size reduction: */
			glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-currentMaxStepSizeTexture));
			
			/* Reduce the viewport by a factor of two: */
			glViewport(0,0,(reducedWidth+1)/2,(reducedHeight+1)/2);
			glUniformARB(dataItem->maxStepSizeShaderUniformLocations[0],GLfloat(reducedWidth-1),GLfloat(reducedHeight-1));
			
			/* Bind the current max step size texture: */
			glActiveTextureARB(GL_TEXTURE0_ARB);
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->maxStepSizeTextureObjects[currentMaxStepSizeTexture]);
			glUniform1iARB(dataItem->maxStepSizeShaderUniformLocations[1],0);
			
			/* Run the reduction step: */
			glBegin(GL_QUADS);
			glVertex2i(0,0);
			glVertex2i(size[0],0);
			glVertex2i(size[0],size[1]);
			glVertex2i(0,size[1]);
			glEnd();
			
			/* Go to the next step: */
			reducedWidth=(reducedWidth+1)/2;
			reducedHeight=(reducedHeight+1)/2;
			currentMaxStepSizeTexture=1-currentMaxStepSizeTexture;
			}
		
		/* Read the final value written into the last reduced 1x1 frame buffer: */
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT+currentMaxStepSizeTexture);
		glReadPixels(0,0,1,1,GL_LUMINANCE,GL_FLOAT,&stepSize);
		
		/* Limit the step size to the client-specified range: */
		stepSize=Math::min(stepSize,maxStepSize);
		}
	
	return stepSize;
	}

WaterTable2::WaterTable2(GLsizei width,GLsizei height,const Plane& basePlane,const Point basePlaneCorners[4])
	{
	/* Initialize the water table size: */
	size[0]=width;
	size[1]=height;
	
	/* Project the corner points to the base plane and calculate their centroid: */
	Point bpc[4];
	Point::AffineCombiner centroidC;
	for(int i=0;i<4;++i)
		{
		bpc[i]=basePlane.project(basePlaneCorners[i]);
		centroidC.addPoint(bpc[i]);
		}
	Point baseCentroid=centroidC.getPoint();
	
	/* Calculate the transformation from camera space to upright elevation model space: */
	typedef Point::Vector Vector;
	Vector z=basePlane.getNormal();
	Vector x=(bpc[1]-bpc[0])+(bpc[3]-bpc[2]);
	Vector y=Geometry::cross(z,x);
	baseTransform=Transform::translateFromOriginTo(baseCentroid);
	baseTransform*=Transform::rotate(Transform::Rotation::fromBaseVectors(x,y));
	baseTransform.doInvert();
	
	/* Calculate the domain of upright elevation model space: */
	domain=Box::empty;
	for(int i=0;i<4;++i)
		domain.addPoint(baseTransform.transform(bpc[i]));
	domain.min[2]=Scalar(-20);
	domain.max[2]=Scalar(100);
	
	/* Calculate the grid's cell size: */
	for(int i=0;i<2;++i)
		cellSize[i]=GLfloat((domain.max[i]-domain.min[i])/Scalar(size[i]));
	std::cout<<cellSize[0]<<" x "<<cellSize[1]<<std::endl;
	
	/* Initialize simulation parameters: */
	theta=1.3f;
	g=9.81f;
	epsilon=0.01f*Math::max(Math::max(cellSize[0],cellSize[1]),1.0f);
	attenuation=127.0f/128.0f; // 31.0f/32.0f;
	maxStepSize=1.0f;
	
	/* Create a 4x4 matrix expressing the texture transformation: */
	Geometry::Matrix<double,4,4> stMat(1.0);
	stMat(0,0)=double(size[0])/(domain.max[0]-domain.min[0]);
	stMat(0,3)=stMat(0,0)*-domain.min[0];
	stMat(1,1)=double(size[1])/(domain.max[1]-domain.min[1]);
	stMat(1,3)=stMat(1,1)*-domain.min[1];
	Geometry::Matrix<double,4,4> btMat(1.0);
	baseTransform.writeMatrix(btMat);
	Geometry::Matrix<double,4,4> texMat=stMat*btMat;
	for(int i=0;i<4;++i)
		for(int j=0;j<4;++j)
			waterTextureMatrix[j*4+i]=texMat(i,j);
	
	/* Initialize the water deposit amount: */
	waterDeposit=0.0f;
	}

WaterTable2::~WaterTable2(void)
	{
	}

void WaterTable2::initContext(GLContextData& contextData) const
	{
	/* Create a data item and add it to the context: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	glActiveTextureARB(GL_TEXTURE0_ARB);
	
	{
	/* Create the vertex-centered bathymetry textures, replacing the outermost layer of cells with ghost cells: */
	glGenTextures(2,dataItem->bathymetryTextureObjects);
	GLfloat* b=makeBuffer(size[0]-1,size[1]-1,1,double(domain.min[2]));
	for(int i=0;i<2;++i)
		{
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[i]);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_R32F,size[0]-1,size[1]-1,0,GL_LUMINANCE,GL_FLOAT,b);
		}
	delete[] b;
	}
	
	{
	/* Create the cell-centered quantity state texture: */
	glGenTextures(1,&dataItem->quantityTextureObject);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	GLfloat* q=makeBuffer(size[0],size[1],3,double(domain.min[2]),0.0,0.0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB32F,size[0],size[1],0,GL_RGB,GL_FLOAT,q);
	delete[] q;
	}
	
	{
	/* Create the cell-centered temporal derivative texture: */
	glGenTextures(1,&dataItem->derivativeTextureObject);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	GLfloat* qt=makeBuffer(size[0],size[1],3,0.0,0.0,0.0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB32F,size[0],size[1],0,GL_RGB,GL_FLOAT,qt);
	delete[] qt;
	}
	
	{
	/* Create the cell-centered maximum step size gathering textures: */
	glGenTextures(2,dataItem->maxStepSizeTextureObjects);
	GLfloat* mss=makeBuffer(size[0],size[1],1,10000.0);
	for(int i=0;i<2;++i)
		{
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->maxStepSizeTextureObjects[i]);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_R32F,size[0],size[1],0,GL_LUMINANCE,GL_FLOAT,mss);
		}
	delete[] mss;
	}
	
	{
	/* Create the cell-centered intermediate quantity state texture: */
	glGenTextures(1,&dataItem->quantityStarTextureObject);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityStarTextureObject);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	GLfloat* qStar=makeBuffer(size[0],size[1],3,double(domain.min[2]),0.0,0.0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB32F,size[0],size[1],0,GL_RGB,GL_FLOAT,qStar);
	delete[] qStar;
	}
	
	{
	/* Create the cell-centered water texture: */
	glGenTextures(1,&dataItem->waterTextureObject);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	GLfloat* w=makeBuffer(size[0],size[1],1,0.0);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_R32F,size[0],size[1],0,GL_LUMINANCE,GL_FLOAT,w);
	delete[] w;
	}
	
	/* Protect the newly-created textures: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Save the currently bound frame buffer: */
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	
	{
	/* Create the bathymetry rendering frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->bathymetryFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->bathymetryFramebufferObject);
	
	/* Attach the bathymetry textures to the bathymetry rendering frame buffer: */
	for(int i=0;i<2;++i)
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT+i,GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[i],0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	}
	
	{
	/* Create the temporal derivative computation frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->derivativeFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->derivativeFramebufferObject);
	
	/* Attach the derivative and maximum step size textures to the temporal derivative computation frame buffer: */
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject,0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT1_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->maxStepSizeTextureObjects[0],0);
	GLenum drawBuffers[2]={GL_COLOR_ATTACHMENT0_EXT,GL_COLOR_ATTACHMENT1_EXT};
	glDrawBuffersARB(2,drawBuffers);
	glReadBuffer(GL_NONE);
	}
	
	{
	/* Create the maximum step size computation frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->maxStepSizeFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->maxStepSizeFramebufferObject);
	
	/* Attach the maximum step size textures to the maximum step size computation frame buffer: */
	for(int i=0;i<2;++i)
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT+i,GL_TEXTURE_RECTANGLE_ARB,dataItem->maxStepSizeTextureObjects[i],0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	}
	
	{
	/* Create the integration step frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->integrationFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	
	/* Attach the intermediate quantity texture to the integration step frame buffer: */
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject,0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT1_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityStarTextureObject,0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	}
	
	{
	/* Create the water frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->waterFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->waterFramebufferObject);
	
	/* Attach the water texture to the water frame buffer: */
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject,0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glReadBuffer(GL_NONE);
	}
	
	/* Restore the previously bound frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	
	/* Create the bathymetry update shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2BathymetryUpdateShader");
	dataItem->bathymetryShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->bathymetryShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->bathymetryShader,"oldBathymetrySampler");
	dataItem->bathymetryShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->bathymetryShader,"newBathymetrySampler");
	dataItem->bathymetryShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->bathymetryShader,"quantitySampler");
	}
	
	/* Create the temporal derivative computation shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2SlopeAndFluxAndDerivativeShader");
	dataItem->derivativeShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->derivativeShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->derivativeShader,"cellSize");
	dataItem->derivativeShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->derivativeShader,"theta");
	dataItem->derivativeShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->derivativeShader,"g");
	dataItem->derivativeShaderUniformLocations[3]=glGetUniformLocationARB(dataItem->derivativeShader,"epsilon");
	dataItem->derivativeShaderUniformLocations[4]=glGetUniformLocationARB(dataItem->derivativeShader,"bathymetrySampler");
	dataItem->derivativeShaderUniformLocations[5]=glGetUniformLocationARB(dataItem->derivativeShader,"quantitySampler");
	}
	
	/* Create the maximum step size gathering shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2MaxStepSizeShader");
	dataItem->maxStepSizeShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->maxStepSizeShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->maxStepSizeShader,"fullTextureSize");
	dataItem->maxStepSizeShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->maxStepSizeShader,"maxStepSizeSampler");
	}
	
	/* Create the boundary condition shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2BoundaryShader");
	dataItem->boundaryShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->boundaryShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->boundaryShader,"bathymetrySampler");
	}
	
	/* Create the Euler integration step shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2EulerStepShader");
	dataItem->eulerStepShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->eulerStepShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->eulerStepShader,"stepSize");
	dataItem->eulerStepShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->eulerStepShader,"attenuation");
	dataItem->eulerStepShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->eulerStepShader,"quantitySampler");
	dataItem->eulerStepShaderUniformLocations[3]=glGetUniformLocationARB(dataItem->eulerStepShader,"derivativeSampler");
	}
	
	/* Create the Runge-Kutta integration step shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2RungeKuttaStepShader");
	dataItem->rungeKuttaStepShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->rungeKuttaStepShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->rungeKuttaStepShader,"stepSize");
	dataItem->rungeKuttaStepShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->rungeKuttaStepShader,"attenuation");
	dataItem->rungeKuttaStepShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->rungeKuttaStepShader,"quantitySampler");
	dataItem->rungeKuttaStepShaderUniformLocations[3]=glGetUniformLocationARB(dataItem->rungeKuttaStepShader,"quantityStarSampler");
	dataItem->rungeKuttaStepShaderUniformLocations[4]=glGetUniformLocationARB(dataItem->rungeKuttaStepShader,"derivativeSampler");
	}
	
	/* Create the water adder rendering shader: */
	{
	GLhandleARB vertexShader=compileVertexShader("Water2WaterAddShader");
	GLhandleARB fragmentShader=compileFragmentShader("Water2WaterAddShader");
	dataItem->waterAddShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->waterAddShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->waterAddShader,"stepSize");
	dataItem->waterAddShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->waterAddShader,"waterSampler");
	}
	
	/* Create the water shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2WaterUpdateShader");
	dataItem->waterShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->waterShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->waterShader,"bathymetrySampler");
	dataItem->waterShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->waterShader,"quantitySampler");
	dataItem->waterShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->waterShader,"waterSampler");
	}
	
	/*********************************************************************
	Initialize simulation state:
	*********************************************************************/
	
	#if 0
	
	/* Create the bathymetry texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[0]);
	GLfloat* b=new GLfloat[(size[1]-1)*(size[0]-1)];
	GLfloat* bPtr=b;
	for(int y=0;y<size[1]-1;++y)
		for(int x=0;x<size[0]-1;++x,++bPtr)
			{
			#if 0
			
			/* Flat bathymetry: */
			*bPtr=domain.min[2];
			
			#elif 0
			
			/* Swimming pool: */
			if(x>0&&x<size[0]-2&&y>0&&y<size[1]-2)
				{
				/* Gaussian blob island: */
				GLfloat cx=GLfloat(size[0])*0.5f;
				GLfloat cy=GLfloat(size[1])*0.5f;
				GLfloat arg=Math::exp(-(Math::sqr(GLfloat(x)-cx)+Math::sqr(GLfloat(y)-cy))/Math::sqr(20.0f))*15.0f;
				*bPtr=arg;
				}
			else
				*bPtr=25.0f;
			
			#elif 0
			
			/* Gaussian blob island: */
			GLfloat cx=GLfloat(size[0])*0.5f;
			GLfloat cy=GLfloat(size[1])*0.5f;
			GLfloat arg=Math::exp(-(Math::sqr(GLfloat(x)-cx)+Math::sqr(GLfloat(y)-cy))/Math::sqr(20.0f))*25.0f;
			*bPtr=arg;
			
			#else
			
			/* Reservoir with outflow channel: */
			if(x==0||x==size[0]-2||y==0||y==size[1]-2)
				*bPtr=50.0f;
			else if(x>=5&&x<=size[0]-7&&y>=5&&y<size[1]/4)
				*bPtr=0.0f;
			else if(x>=size[0]/2-15&&x<size[0]/2+35&&y>=size[1]/4+5)
				*bPtr=0.0f;
			else if(y>=size[1]/4+5)
				*bPtr=5.0f;
			else if(x>=size[0]/2-10&&x<size[0]/2+30&&y>=5)
				*bPtr=0.0f;
			else
				*bPtr=50.0f;
			
			#endif
			}
	glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0]+1,size[1]+1,GL_LUMINANCE,GL_FLOAT,b);
	
	/* Create the initial quantity state texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	GLfloat cx=GLfloat(size[0])*0.25f;
	GLfloat cy=GLfloat(size[1])*0.333f;
	GLfloat* q=new GLfloat[size[1]*size[0]*3];
	GLfloat* qPtr=q;
	GLfloat* bRowPtr=b;
	for(int y=0;y<size[1];++y,bRowPtr+=size[0]+1)
		{
		GLfloat* bPtr=bRowPtr;
		for(int x=0;x<size[0];++x,qPtr+=3,++bPtr)
			{
			#if 1
			
			/* Dam failure: */
			if(y<size[1]/4)
				qPtr[0]=40.0f;
			else
				qPtr[0]=0.0f;
			
			#elif 0
			
			/* Gaussian water blob: */
			GLfloat arg=Math::exp(-(Math::sqr(GLfloat(x)-cx)+Math::sqr(GLfloat(y)-cy))/Math::sqr(16.0f))*40.0f+10.0f;
			qPtr[0]=arg;
			
			#elif 0
			
			/* Rectangular water blob: */
			if(x>=size[0]/4-10&&x<size[0]/4+10&&y>=size[1]/3-10&&y<size[1]/3+10)
				qPtr[0]=40.0f;
			else
				qPtr[0]=0.0f;
			
			#else
			
			/* Flat surface: */
			qPtr[0]=domain.min[2];
			
			#endif
			
			/* Check water surface height against bathymetry height: */
			int left=x>0?-1:0;
			int down=y>0?-(size[0]-1):0;
			GLfloat b=Math::mid(Math::mid(bPtr[down+left],bPtr[down]),Math::mid(bPtr[left],bPtr[0]));
			if(qPtr[0]<b)
				qPtr[0]=b;
			
			qPtr[1]=qPtr[2]=0.0f;
			}
		}
	glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_RGB,GL_FLOAT,q);
	
	delete[] b;
	delete[] q;
	
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	#endif
	
	dataItem->currentBathymetry=0;
	}

void WaterTable2::setElevationRange(WaterTable2::Scalar newMin,WaterTable2::Scalar newMax)
	{
	domain.min[2]=newMin;
	domain.max[2]=newMax;
	}

void WaterTable2::setAttenuation(GLfloat newAttenuation)
	{
	attenuation=newAttenuation;
	}

void WaterTable2::setMaxStepSize(GLfloat newMaxStepSize)
	{
	maxStepSize=newMaxStepSize;
	}

void WaterTable2::addRenderFunction(const AddWaterFunction* newRenderFunction)
	{
	/* Store the new render function: */
	renderFunctions.push_back(newRenderFunction);
	}

void WaterTable2::removeRenderFunction(const AddWaterFunction* removeRenderFunction)
	{
	/* Find the given render function in the list and remove it: */
	for(std::vector<const AddWaterFunction*>::iterator rfIt=renderFunctions.begin();rfIt!=renderFunctions.end();++rfIt)
		if(*rfIt==removeRenderFunction)
			{
			/* Remove the list element: */
			renderFunctions.erase(rfIt);
			break;
			}
	}

void WaterTable2::setWaterDeposit(GLfloat newWaterDeposit)
	{
	waterDeposit=newWaterDeposit;
	}

void WaterTable2::updateBathymetry(const SurfaceRenderer& bathymetryRenderer,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Save relevant OpenGL state: */
	glPushAttrib(GL_VIEWPORT_BIT);
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	GLfloat currentClearColor[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE,currentClearColor);
	
	/* Bind the bathymetry rendering frame buffer and clear it: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->bathymetryFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentBathymetry));
	glViewport(0,0,size[0]-1,size[1]-1);
	glClearColor(GLfloat(domain.min[2]),0.0f,0.0f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	
	/* Set the transformation from camera space to upright elevation model space: */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	Scalar hw=Math::div2(cellSize[0]);
	Scalar hh=Math::div2(cellSize[1]);
	glOrtho(domain.min[0]+hw,domain.max[0]-hw,domain.min[1]+hh,domain.max[1]-hh,-domain.max[2],-domain.min[2]);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadMatrix(baseTransform);
	
	/* Render the surface: */
	bathymetryRenderer.glRenderElevation(contextData);
	
	/* Set up the integration frame buffer to update the conserved quantities based on bathymetry changes: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the bathymetry update shader: */
	glUseProgramObjectARB(dataItem->bathymetryShader);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[0],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[1-dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[1],1);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[2],2);
	
	/* Run the bathymetry update: */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glBegin(GL_QUADS);
	glVertex2i(-1,-1);
	glVertex2i( 1,-1);
	glVertex2i( 1, 1);
	glVertex2i(-1, 1);
	glEnd();
	
	/* Unbind all shaders and textures: */
	glUseProgramObjectARB(0);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Restore OpenGL state: */
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	glClearColor(currentClearColor[0],currentClearColor[1],currentClearColor[2],currentClearColor[3]);
	glPopAttrib();
	
	/* Update the bathymetry grid: */
	dataItem->currentBathymetry=1-dataItem->currentBathymetry;
	}

GLfloat WaterTable2::runSimulationStep(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Save relevant OpenGL state: */
	glPushAttrib(GL_COLOR_BUFFER_BIT|GL_VIEWPORT_BIT);
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	
	/* Save and reset OpenGL matrices: */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0,double(size[0]),0.0,double(size[1]),-1.0,1.0); // Set projection matrix for pixel-coordinate rendering
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	
	/*********************************************************************
	Step 1: Calculate temporal derivative of most recent quantities.
	*********************************************************************/
	
	GLfloat stepSize=calcDerivative(dataItem,dataItem->quantityTextureObject,true);
	
	/*********************************************************************
	Step 2: Perform the tentative Euler integration step.
	*********************************************************************/
	
	/* Set up the Euler step integration frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT1_EXT);
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the Euler integration step shader: */
	glUseProgramObjectARB(dataItem->eulerStepShader);
	glUniformARB(dataItem->eulerStepShaderUniformLocations[0],stepSize);
	glUniformARB(dataItem->eulerStepShaderUniformLocations[1],Math::pow(attenuation,stepSize));
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	glUniform1iARB(dataItem->eulerStepShaderUniformLocations[2],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject);
	glUniform1iARB(dataItem->eulerStepShaderUniformLocations[3],1);
	
	/* Run the Euler integration step on the interior pixels: */
	glBegin(GL_QUADS);
	#if 0
	glVertex2i(1,1);
	glVertex2i(size[0]-1,1);
	glVertex2i(size[0]-1,size[1]-1);
	glVertex2i(1,size[1]-1);
	#else
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	#endif
	glEnd();
	
	/*********************************************************************
	Step 3: Calculate temporal derivative of intermediate quantities.
	*********************************************************************/
	
	calcDerivative(dataItem,dataItem->quantityStarTextureObject,false);
	
	/*********************************************************************
	Step 4: Perform the final Runge-Kutta integration step.
	*********************************************************************/
	
	/* Set up the Runge-Kutta step integration frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the Runge-Kutta integration step shader: */
	glUseProgramObjectARB(dataItem->rungeKuttaStepShader);
	glUniformARB(dataItem->rungeKuttaStepShaderUniformLocations[0],stepSize);
	glUniformARB(dataItem->rungeKuttaStepShaderUniformLocations[1],Math::pow(attenuation,stepSize));
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[2],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityStarTextureObject);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[3],1);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[4],2);
	
	/* Run the Runge-Kutta integration step on the interior pixels: */
	glBegin(GL_QUADS);
	#if 0
	glVertex2i(1,1);
	glVertex2i(size[0]-1,1);
	glVertex2i(size[0]-1,size[1]-1);
	glVertex2i(1,size[1]-1);
	#else
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	#endif
	glEnd();
	
	/* Set up the boundary condition shader to enforce dry boundaries: */
	glUseProgramObjectARB(dataItem->boundaryShader);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->boundaryShaderUniformLocations[0],0);
	
	/* Run the boundary condition shader on the outermost layer of pixels: */
	//glColorMask(GL_TRUE,GL_FALSE,GL_FALSE,GL_FALSE);
	glBegin(GL_LINE_LOOP);
	glVertex2f(0.5f,0.5f);
	glVertex2f(GLfloat(size[0])-0.5f,0.5f);
	glVertex2f(GLfloat(size[0])-0.5f,GLfloat(size[1])-0.5f);
	glVertex2f(0.5f,GLfloat(size[1])-0.5f);
	glEnd();
	//glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
	
	if(waterDeposit!=0.0f||!renderFunctions.empty())
		{
		/* Save OpenGL state: */
		GLfloat currentClearColor[4];
		glGetFloatv(GL_COLOR_CLEAR_VALUE,currentClearColor);
		
		/*******************************************************************
		Step 5: Render all water sources and sinks additively into the water
		texture.
		*******************************************************************/
		
		/* Set up and clear the water frame buffer: */
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->waterFramebufferObject);
		glViewport(0,0,size[0],size[1]);
		glClearColor(waterDeposit*stepSize,0.0f,0.0f,0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		
		/* Enable additive rendering: */
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE,GL_ONE);
		
		/* Set up the water adding shader: */
		glUseProgramObjectARB(dataItem->waterAddShader);
		glUniform1fARB(dataItem->waterAddShaderUniformLocations[0],stepSize);
		
		/* Bind the water texture: */
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject);
		glUniform1iARB(dataItem->waterAddShaderUniformLocations[1],0);
		
		/* Set modelview and projection matrices to render from camera coordinates into the water table texture: */
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(domain.min[0],domain.max[0],domain.min[1],domain.max[1],-domain.max[2],-domain.min[2]);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrix(baseTransform);
		
		/* Call all render functions: */
		for(std::vector<const AddWaterFunction*>::const_iterator rfIt=renderFunctions.begin();rfIt!=renderFunctions.end();++rfIt)
			(**rfIt)(contextData);
		
		/* Restore OpenGL state: */
		glDisable(GL_BLEND);
		glClearColor(currentClearColor[0],currentClearColor[1],currentClearColor[2],currentClearColor[3]);
		
		/*******************************************************************
		Step 6: Update the conserved quantities based on the water texture.
		*******************************************************************/
		
		/* Set up the integration frame buffer to update the conserved quantities based on the water texture: */
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		glViewport(0,0,size[0],size[1]);
		
		/* Set up the water update shader: */
		glUseProgramObjectARB(dataItem->waterShader);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
		glUniform1iARB(dataItem->waterShaderUniformLocations[0],0);
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
		glUniform1iARB(dataItem->waterShaderUniformLocations[1],1);
		glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject);
		glUniform1iARB(dataItem->waterShaderUniformLocations[2],2);
		
		/* Run the water update: */
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glBegin(GL_QUADS);
		glVertex2i(-1,-1);
		glVertex2i( 1,-1);
		glVertex2i( 1, 1);
		glVertex2i(-1, 1);
		glEnd();
		}
	
	/* Unbind all shaders and textures: */
	glUseProgramObjectARB(0);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Restore OpenGL matrices: */
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	
	/* Restore OpenGL state: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	glPopAttrib();
	
	/* Return the Runge-Kutta step's step size: */
	return stepSize;
	}

void WaterTable2::bindBathymetryTexture(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the bathymetry texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	}

void WaterTable2::bindQuantityTexture(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the conserved quantities texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObject);
	}
