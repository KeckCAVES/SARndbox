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

#include "WaterTable2.h"

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <iostream>
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

#include "DepthImageRenderer.h"
#include "ShaderHelper.h"

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

}

/**************************************
Methods of class WaterTable2::DataItem:
**************************************/

WaterTable2::DataItem::DataItem(void)
	:currentBathymetry(0),bathymetryVersion(0),currentQuantity(0),
	 derivativeTextureObject(0),waterTextureObject(0),
	 bathymetryFramebufferObject(0),derivativeFramebufferObject(0),maxStepSizeFramebufferObject(0),integrationFramebufferObject(0),waterFramebufferObject(0),
	 bathymetryShader(0),waterAdaptShader(0),derivativeShader(0),maxStepSizeShader(0),boundaryShader(0),eulerStepShader(0),rungeKuttaStepShader(0),waterAddShader(0),waterShader(0)
	{
	for(int i=0;i<2;++i)
		{
		bathymetryTextureObjects[i]=0;
		maxStepSizeTextureObjects[i]=0;
		}
	for(int i=0;i<3;++i)
		quantityTextureObjects[i]=0;
	
	/* Initialize all required OpenGL extensions: */
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
	glDeleteTextures(3,quantityTextureObjects);
	glDeleteTextures(1,&derivativeTextureObject);
	glDeleteTextures(2,maxStepSizeTextureObjects);
	glDeleteTextures(1,&waterTextureObject);
	glDeleteFramebuffersEXT(1,&bathymetryFramebufferObject);
	glDeleteFramebuffersEXT(1,&derivativeFramebufferObject);
	glDeleteFramebuffersEXT(1,&maxStepSizeFramebufferObject);
	glDeleteFramebuffersEXT(1,&integrationFramebufferObject);
	glDeleteFramebuffersEXT(1,&waterFramebufferObject);
	glDeleteObjectARB(bathymetryShader);
	glDeleteObjectARB(waterAdaptShader);
	glDeleteObjectARB(derivativeShader);
	glDeleteObjectARB(maxStepSizeShader);
	glDeleteObjectARB(boundaryShader);
	glDeleteObjectARB(eulerStepShader);
	glDeleteObjectARB(rungeKuttaStepShader);
	glDeleteObjectARB(waterAddShader);
	glDeleteObjectARB(waterShader);
	}

/****************************
Methods of class WaterTable2:
****************************/

void WaterTable2::calcTransformations(void)
	{
	/* Calculate the combined modelview and projection matrix to render depth images into the bathymetry grid: */
	{
	bathymetryPmv=PTransform::identity;
	PTransform::Matrix& bpmvm=bathymetryPmv.getMatrix();
	Scalar hw=Math::div2(cellSize[0]);
	Scalar left=domain.min[0]+hw;
	Scalar right=domain.max[0]-hw;
	Scalar hh=Math::div2(cellSize[1]);
	Scalar bottom=domain.min[1]+hh;
	Scalar top=domain.max[1]-hh;
	Scalar near=-domain.max[2];
	Scalar far=-domain.min[2];
	bpmvm(0,0)=Scalar(2)/(right-left);
	bpmvm(0,3)=-(right+left)/(right-left);
	bpmvm(1,1)=Scalar(2)/(top-bottom);
	bpmvm(1,3)=-(top+bottom)/(top-bottom);
	bpmvm(2,2)=Scalar(-2)/(far-near);
	bpmvm(2,3)=-(far+near)/(far-near);
	bathymetryPmv*=baseTransform;
	}
	
	/* Calculate the combined modelview and projection matrix to render water-adding geometry into the water texture: */
	{
	waterAddPmv=PTransform::identity;
	PTransform::Matrix& wapmvm=waterAddPmv.getMatrix();
	Scalar left=domain.min[0];
	Scalar right=domain.max[0];
	Scalar bottom=domain.min[1];
	Scalar top=domain.max[1];
	Scalar near=-domain.max[2]*Scalar(5);
	Scalar far=-domain.min[2];
	wapmvm(0,0)=Scalar(2)/(right-left);
	wapmvm(0,3)=-(right+left)/(right-left);
	wapmvm(1,1)=Scalar(2)/(top-bottom);
	wapmvm(1,3)=-(top+bottom)/(top-bottom);
	wapmvm(2,2)=Scalar(-2)/(far-near);
	wapmvm(2,3)=-(far+near)/(far-near);
	waterAddPmv*=baseTransform;
	}
	
	/* Convert the water addition matrix to column-major OpenGL format: */
	GLfloat* wapPtr=waterAddPmvMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++wapPtr)
			*wapPtr=GLfloat(waterAddPmv.getMatrix()(i,j));
	
	/* Calculate a transformation from camera space into water texture space: */
	waterTextureTransform=PTransform::identity;
	PTransform::Matrix& wttm=waterTextureTransform.getMatrix();
	wttm(0,0)=Scalar(size[0])/(domain.max[0]-domain.min[0]);
	wttm(0,3)=wttm(0,0)*-domain.min[0];
	wttm(1,1)=Scalar(size[1])/(domain.max[1]-domain.min[1]);
	wttm(1,3)=wttm(1,1)*-domain.min[1];
	waterTextureTransform*=baseTransform;
	
	/* Convert the water texture transform to column-major OpenGL format: */
	GLfloat* wttmPtr=waterTextureTransformMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++wttmPtr)
			*wttmPtr=GLfloat(wttm(i,j));
	}

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

WaterTable2::WaterTable2(GLsizei width,GLsizei height,const GLfloat sCellSize[2])
	:depthImageRenderer(0),
	 baseTransform(ONTransform::identity),
	 dryBoundary(true),
	 readBathymetryRequest(0U),readBathymetryBuffer(0),readBathymetryReply(0U)
	{
	/* Initialize the water table size and cell size: */
	size[0]=width;
	size[1]=height;
	for(int i=0;i<2;++i)
		cellSize[i]=sCellSize[i];
	
	/* Calculate a simulation domain: */
	for(int i=0;i<2;++i)
		{
		domain.min[i]=Scalar(0);
		domain.max[i]=Scalar(size[i])*Scalar(cellSize[i]);
		}
	
	/* Calculate the water table transformations: */
	calcTransformations();
	
	/* Initialize simulation parameters: */
	theta=1.3f;
	g=9.81f;
	epsilon=0.01f*Math::max(Math::max(cellSize[0],cellSize[1]),1.0f);
	attenuation=127.0f/128.0f; // 31.0f/32.0f;
	maxStepSize=1.0f;
	
	/* Initialize the water deposit amount: */
	waterDeposit=0.0f;
	}

WaterTable2::WaterTable2(GLsizei width,GLsizei height,const DepthImageRenderer* sDepthImageRenderer,const Point basePlaneCorners[4])
	:depthImageRenderer(sDepthImageRenderer),
	 dryBoundary(true),
	 readBathymetryRequest(0U),readBathymetryBuffer(0),readBathymetryReply(0U)
	{
	/* Initialize the water table size: */
	size[0]=width;
	size[1]=height;
	
	/* Project the corner points to the base plane and calculate their centroid: */
	const Plane& basePlane=depthImageRenderer->getBasePlane();
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
	Vector y=z^x;
	baseTransform=ONTransform::translateFromOriginTo(baseCentroid);
	baseTransform*=ONTransform::rotate(ONTransform::Rotation::fromBaseVectors(x,y));
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
	
	/* Calculate the water table transformations: */
	calcTransformations();
	
	/* Initialize simulation parameters: */
	theta=1.3f;
	g=9.81f;
	epsilon=0.01f*Math::max(Math::max(cellSize[0],cellSize[1]),1.0f);
	attenuation=127.0f/128.0f; // 31.0f/32.0f;
	maxStepSize=1.0f;
	
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
	/* Create the cell-centered quantity state textures: */
	glGenTextures(3,dataItem->quantityTextureObjects);
	GLfloat* q=makeBuffer(size[0],size[1],3,double(domain.min[2]),0.0,0.0);
	for(int i=0;i<3;++i)
		{
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[i]);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGB32F,size[0],size[1],0,GL_RGB,GL_FLOAT,q);
		}
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
	
	/* Attach the quantity textures to the integration step frame buffer: */
	for(int i=0;i<3;++i)
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT+i,GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[i],0);
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
	
	/* Create a simple vertex shader to render quads in pixel space: */
	static const char* vertexShaderSourceTemplate="void main(){gl_Position=vec4(gl_Vertex.x*%f-1.0,gl_Vertex.y*%f-1.0,0.0,1.0);}";
	char vertexShaderSource[256];
	snprintf(vertexShaderSource,sizeof(vertexShaderSource),vertexShaderSourceTemplate,2.0/double(size[0]),2.0/double(size[1]));
	
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
	
	/* Create the water adaptation shader: */
	{
	GLhandleARB vertexShader=glCompileVertexShaderFromString(vertexShaderSource);
	GLhandleARB fragmentShader=compileFragmentShader("Water2WaterAdaptShader");
	dataItem->waterAdaptShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->waterAdaptShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->waterAdaptShader,"bathymetrySampler");
	dataItem->waterAdaptShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->waterAdaptShader,"newQuantitySampler");
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
	dataItem->waterAddShaderUniformLocations[0]=glGetUniformLocationARB(dataItem->waterAddShader,"pmv");
	dataItem->waterAddShaderUniformLocations[1]=glGetUniformLocationARB(dataItem->waterAddShader,"stepSize");
	dataItem->waterAddShaderUniformLocations[2]=glGetUniformLocationARB(dataItem->waterAddShader,"waterSampler");
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
	}

void WaterTable2::setElevationRange(Scalar newMin,Scalar newMax)
	{
	/* Set the new elevation range: */
	domain.min[2]=newMin;
	domain.max[2]=newMax;
	
	/* Recalculate the water table transformations: */
	calcTransformations();
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

void WaterTable2::setDryBoundary(bool newDryBoundary)
	{
	dryBoundary=newDryBoundary;
	}

void WaterTable2::updateBathymetry(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Check if the current bathymetry texture is outdated: */
	if(dataItem->bathymetryVersion!=depthImageRenderer->getDepthImageVersion())
		{
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
		
		/* Render the surface into the bathymetry grid: */
		depthImageRenderer->renderElevation(bathymetryPmv,contextData);
		
		/* Set up the integration frame buffer to update the conserved quantities based on bathymetry changes: */
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentQuantity));
		glViewport(0,0,size[0],size[1]);
		
		/* Set up the bathymetry update shader: */
		glUseProgramObjectARB(dataItem->bathymetryShader);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
		glUniform1iARB(dataItem->bathymetryShaderUniformLocations[0],0);
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[1-dataItem->currentBathymetry]);
		glUniform1iARB(dataItem->bathymetryShaderUniformLocations[1],1);
		
		/* Check if the current bathymetry grid was requested: */
		if(readBathymetryReply!=readBathymetryRequest)
			{
			/* Read back the bathymetry grid into the supplied buffer: */
			glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RED,GL_FLOAT,readBathymetryBuffer);
			
			/* Finish the request: */
			readBathymetryReply=readBathymetryRequest;
			}
		
		glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
		glUniform1iARB(dataItem->bathymetryShaderUniformLocations[2],2);
		
		/* Run the bathymetry update: */
		glBegin(GL_QUADS);
		glVertex2i(0,0);
		glVertex2i(size[0],0);
		glVertex2i(size[0],size[1]);
		glVertex2i(0,size[1]);
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
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
		glClearColor(currentClearColor[0],currentClearColor[1],currentClearColor[2],currentClearColor[3]);
		glPopAttrib();
		
		/* Update the bathymetry and quantity grids: */
		dataItem->currentBathymetry=1-dataItem->currentBathymetry;
		dataItem->bathymetryVersion=depthImageRenderer->getDepthImageVersion();
		dataItem->currentQuantity=1-dataItem->currentQuantity;
		}
	}

void WaterTable2::updateBathymetry(const GLfloat* bathymetryGrid,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Set up the integration frame buffer to update the conserved quantities based on bathymetry changes: */
	glPushAttrib(GL_VIEWPORT_BIT);
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentQuantity));
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the bathymetry update shader: */
	glUseProgramObjectARB(dataItem->bathymetryShader);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[0],0);
	
	/* Upload the new bathymetry grid: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[1-dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[1],1);
	glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0]-1,size[1]-1,GL_LUMINANCE,GL_FLOAT,bathymetryGrid);
	
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
	glUniform1iARB(dataItem->bathymetryShaderUniformLocations[2],2);
	
	/* Run the bathymetry update: */
	glBegin(GL_QUADS);
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	glEnd();
	
	/* Unbind all shaders and textures: */
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glUseProgramObjectARB(0);

	/* Restore OpenGL state: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	glPopAttrib();

	/* Update the bathymetry and quantity grids: */
	dataItem->currentBathymetry=1-dataItem->currentBathymetry;
	dataItem->currentQuantity=1-dataItem->currentQuantity;
	}

void WaterTable2::setWaterLevel(const GLfloat* waterGrid,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Set up the integration frame buffer to adapt the new water level to the current bathymetry: */
	glPushAttrib(GL_VIEWPORT_BIT);
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentQuantity));
	glViewport(0,0,size[0],size[1]);
	
	/* Bind the water adaptation shader: */
	glUseProgramObjectARB(dataItem->waterAdaptShader);
	
	/* Bind the current bathymetry texture: */
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
	glUniform1iARB(dataItem->waterAdaptShaderUniformLocations[0],0);
	
	/* Bind the current quantity texture: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
	glUniform1iARB(dataItem->waterAdaptShaderUniformLocations[1],1);
	
	/* Upload the new water level texture: */
	glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_RED,GL_FLOAT,waterGrid);
	
	/* Run the water adaptation shader: */
	glBegin(GL_QUADS);
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	glEnd();
	
	/* Unbind all shaders and textures: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glUseProgramObjectARB(0);

	/* Restore OpenGL state: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	glPopAttrib();

	/* Update the quantity grid: */
	dataItem->currentQuantity=1-dataItem->currentQuantity;
	}

GLfloat WaterTable2::runSimulationStep(bool forceStepSize,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Save relevant OpenGL state: */
	glPushAttrib(GL_COLOR_BUFFER_BIT|GL_VIEWPORT_BIT);
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	
	/*********************************************************************
	Step 1: Calculate temporal derivative of most recent quantities.
	*********************************************************************/
	
	GLfloat stepSize=calcDerivative(dataItem,dataItem->quantityTextureObjects[dataItem->currentQuantity],!forceStepSize);
	
	/*********************************************************************
	Step 2: Perform the tentative Euler integration step.
	*********************************************************************/
	
	/* Set up the Euler step integration frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+2);
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the Euler integration step shader: */
	glUseProgramObjectARB(dataItem->eulerStepShader);
	glUniformARB(dataItem->eulerStepShaderUniformLocations[0],stepSize);
	glUniformARB(dataItem->eulerStepShaderUniformLocations[1],Math::pow(attenuation,stepSize));
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
	glUniform1iARB(dataItem->eulerStepShaderUniformLocations[2],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject);
	glUniform1iARB(dataItem->eulerStepShaderUniformLocations[3],1);
	
	/* Run the Euler integration step: */
	glBegin(GL_QUADS);
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	glEnd();
	
	/*********************************************************************
	Step 3: Calculate temporal derivative of intermediate quantities.
	*********************************************************************/
	
	calcDerivative(dataItem,dataItem->quantityTextureObjects[2],false);
	
	/*********************************************************************
	Step 4: Perform the final Runge-Kutta integration step.
	*********************************************************************/
	
	/* Set up the Runge-Kutta step integration frame buffer: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->integrationFramebufferObject);
	glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentQuantity));
	glViewport(0,0,size[0],size[1]);
	
	/* Set up the Runge-Kutta integration step shader: */
	glUseProgramObjectARB(dataItem->rungeKuttaStepShader);
	glUniformARB(dataItem->rungeKuttaStepShaderUniformLocations[0],stepSize);
	glUniformARB(dataItem->rungeKuttaStepShaderUniformLocations[1],Math::pow(attenuation,stepSize));
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[2],0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[2]);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[3],1);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->derivativeTextureObject);
	glUniform1iARB(dataItem->rungeKuttaStepShaderUniformLocations[4],2);
	
	/* Run the Runge-Kutta integration step: */
	glBegin(GL_QUADS);
	glVertex2i(0,0);
	glVertex2i(size[0],0);
	glVertex2i(size[0],size[1]);
	glVertex2i(0,size[1]);
	glEnd();
	
	if(dryBoundary)
		{
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
		}
	
	/* Update the current quantities: */
	dataItem->currentQuantity=1-dataItem->currentQuantity;
	
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
		glUniformMatrix4fvARB(dataItem->waterAddShaderUniformLocations[0],1,GL_FALSE,waterAddPmvMatrix);
		glUniform1fARB(dataItem->waterAddShaderUniformLocations[1],stepSize);
		
		/* Bind the water texture: */
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject);
		glUniform1iARB(dataItem->waterAddShaderUniformLocations[2],0);
		
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
		glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT+(1-dataItem->currentQuantity));
		glViewport(0,0,size[0],size[1]);
		
		/* Set up the water update shader: */
		glUseProgramObjectARB(dataItem->waterShader);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->bathymetryTextureObjects[dataItem->currentBathymetry]);
		glUniform1iARB(dataItem->waterShaderUniformLocations[0],0);
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
		glUniform1iARB(dataItem->waterShaderUniformLocations[1],1);
		glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->waterTextureObject);
		glUniform1iARB(dataItem->waterShaderUniformLocations[2],2);
		
		/* Run the water update: */
		glBegin(GL_QUADS);
		glVertex2i(0,0);
		glVertex2i(size[0],0);
		glVertex2i(size[0],size[1]);
		glVertex2i(0,size[1]);
		glEnd();
		
		/* Update the current quantities: */
		dataItem->currentQuantity=1-dataItem->currentQuantity;
		}
	
	/* Unbind all shaders and textures: */
	glUseProgramObjectARB(0);
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
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
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->quantityTextureObjects[dataItem->currentQuantity]);
	}

void WaterTable2::uploadWaterTextureTransform(GLint location) const
	{
	/* Upload the matrix to OpenGL: */
	glUniformMatrix4fvARB(location,1,GL_FALSE,waterTextureTransformMatrix);
	}

bool WaterTable2::requestBathymetry(GLfloat* newReadBathymetryBuffer)
	{
	/* Check if the previous bathymetry request has been fulfilled: */
	if(readBathymetryReply==readBathymetryRequest)
		{
		/* Set up the new bathymetry request: */
		++readBathymetryRequest;
		readBathymetryBuffer=newReadBathymetryBuffer;
		
		return true;
		}
	else
		return false;
	}
