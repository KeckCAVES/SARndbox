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

#include "DepthImageRenderer.h"

#include <GL/gl.h>
#include <GL/GLVertexArrayParts.h>
#include <GL/GLContextData.h>
#include <GL/Extensions/GLARBFragmentShader.h>
#include <GL/Extensions/GLARBMultitexture.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/Extensions/GLARBTextureFloat.h>
#include <GL/Extensions/GLARBTextureRectangle.h>
#include <GL/Extensions/GLARBTextureRg.h>
#include <GL/Extensions/GLARBVertexBufferObject.h>
#include <GL/Extensions/GLARBVertexShader.h>
#include <GL/GLTransformationWrappers.h>

#include "ShaderHelper.h"

/*********************************************
Methods of class DepthImageRenderer::DataItem:
*********************************************/

DepthImageRenderer::DataItem::DataItem(void)
	:vertexBuffer(0),indexBuffer(0),
	 depthTexture(0),depthTextureVersion(0),
	 depthShader(0),elevationShader(0)
	{
	/* Initialize all required extensions: */
	GLARBFragmentShader::initExtension();
	GLARBMultitexture::initExtension();
	GLARBShaderObjects::initExtension();
	GLARBTextureFloat::initExtension();
	GLARBTextureRectangle::initExtension();
	GLARBTextureRg::initExtension();
	GLARBVertexBufferObject::initExtension();
	GLARBVertexShader::initExtension();
	
	/* Allocate the buffers and textures: */
	glGenBuffersARB(1,&vertexBuffer);
	glGenBuffersARB(1,&indexBuffer);
	glGenTextures(1,&depthTexture);
	}

DepthImageRenderer::DataItem::~DataItem(void)
	{
	/* Release all allocated buffers, textures, and shaders: */
	glDeleteBuffersARB(1,&vertexBuffer);
	glDeleteBuffersARB(1,&indexBuffer);
	glDeleteTextures(1,&depthTexture);
	glDeleteObjectARB(depthShader);
	glDeleteObjectARB(elevationShader);
	}

/***********************************
Methods of class DepthImageRenderer:
***********************************/

DepthImageRenderer::DepthImageRenderer(const unsigned int sDepthImageSize[2])
	:depthImageVersion(0)
	{
	/* Copy the depth image size: */
	for(int i=0;i<2;++i)
		depthImageSize[i]=sDepthImageSize[i];
	
	/* Initialize the depth image: */
	depthImage=Kinect::FrameBuffer(depthImageSize[0],depthImageSize[1],depthImageSize[1]*depthImageSize[0]*sizeof(float));
	float* diPtr=depthImage.getData<float>();
	for(unsigned int y=0;y<depthImageSize[1];++y)
		for(unsigned int x=0;x<depthImageSize[0];++x,++diPtr)
			*diPtr=0.0f;
	++depthImageVersion;
	}

void DepthImageRenderer::initContext(GLContextData& contextData) const
	{
	/* Create a data item and add it to the context: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	/* Upload the grid of template vertices into the vertex buffer: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB,depthImageSize[1]*depthImageSize[0]*sizeof(Vertex),0,GL_STATIC_DRAW_ARB);
	Vertex* vPtr=static_cast<Vertex*>(glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	if(lensDistortion.isIdentity())
		{
		/* Create uncorrected pixel positions: */
		for(unsigned int y=0;y<depthImageSize[1];++y)
			for(unsigned int x=0;x<depthImageSize[0];++x,++vPtr)
				{
				vPtr->position[0]=Scalar(x)+Scalar(0.5);
				vPtr->position[1]=Scalar(y)+Scalar(0.5);
				}
		}
	else
		{
		/* Create lens distortion-corrected pixel positions: */
		for(unsigned int y=0;y<depthImageSize[1];++y)
			for(unsigned int x=0;x<depthImageSize[0];++x,++vPtr)
				{
				/* Undistort the image point: */
				Kinect::LensDistortion::Point dp(Kinect::LensDistortion::Scalar(x)+Kinect::LensDistortion::Scalar(0.5),Kinect::LensDistortion::Scalar(y)+Kinect::LensDistortion::Scalar(0.5));
				Kinect::LensDistortion::Point up=lensDistortion.undistortPixel(dp);
				
				/* Store the undistorted point: */
				vPtr->position[0]=Scalar(up[0]);
				vPtr->position[1]=Scalar(up[1]);
				}
		}
	glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	
	/* Upload the surface's triangle indices into the index buffer: */
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB,(depthImageSize[1]-1)*depthImageSize[0]*2*sizeof(GLuint),0,GL_STATIC_DRAW_ARB);
	GLuint* iPtr=static_cast<GLuint*>(glMapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	for(unsigned int y=1;y<depthImageSize[1];++y)
		for(unsigned int x=0;x<depthImageSize[0];++x,iPtr+=2)
			{
			iPtr[0]=GLuint(y*depthImageSize[0]+x);
			iPtr[1]=GLuint((y-1)*depthImageSize[0]+x);
			}
	glUnmapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Initialize the depth image texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_LUMINANCE32F_ARB,depthImageSize[0],depthImageSize[1],0,GL_LUMINANCE,GL_FLOAT,0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Create the depth rendering shader: */
	dataItem->depthShader=linkVertexAndFragmentShader("SurfaceDepthShader");
	dataItem->depthShaderUniforms[0]=glGetUniformLocationARB(dataItem->depthShader,"depthSampler");
	dataItem->depthShaderUniforms[1]=glGetUniformLocationARB(dataItem->depthShader,"projectionModelviewDepthProjection");
	
	/* Create the elevation rendering shader: */
	dataItem->elevationShader=linkVertexAndFragmentShader("SurfaceElevationShader");
	dataItem->elevationShaderUniforms[0]=glGetUniformLocationARB(dataItem->elevationShader,"depthSampler");
	dataItem->elevationShaderUniforms[1]=glGetUniformLocationARB(dataItem->elevationShader,"basePlaneDic");
	dataItem->elevationShaderUniforms[2]=glGetUniformLocationARB(dataItem->elevationShader,"weightDic");
	dataItem->elevationShaderUniforms[3]=glGetUniformLocationARB(dataItem->elevationShader,"projectionModelviewDepthProjection");
	}

void DepthImageRenderer::setDepthProjection(const PTransform& newDepthProjection)
	{
	/* Set the depth unprojection matrix: */
	depthProjection=newDepthProjection;
	
	/* Convert the depth projection matrix to column-major OpenGL format: */
	GLfloat* dpmPtr=depthProjectionMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++dpmPtr)
			*dpmPtr=GLfloat(depthProjection.getMatrix()(i,j));
	
	/* Create the weight calculation equation: */
	for(int i=0;i<4;++i)
		weightDicEq[i]=GLfloat(depthProjection.getMatrix()(3,i));
	
	/* Recalculate the base plane equation in depth image space: */
	setBasePlane(basePlane);
	}

void DepthImageRenderer::setIntrinsics(const Kinect::FrameSource::IntrinsicParameters& ips)
	{
	/* Set the lens distortion parameters: */
	lensDistortion=ips.depthLensDistortion;
	
	/* Set the depth unprojection matrix: */
	depthProjection=ips.depthProjection;
	
	/* Convert the depth projection matrix to column-major OpenGL format: */
	GLfloat* dpmPtr=depthProjectionMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++dpmPtr)
			*dpmPtr=GLfloat(depthProjection.getMatrix()(i,j));
	
	/* Create the weight calculation equation: */
	for(int i=0;i<4;++i)
		weightDicEq[i]=GLfloat(depthProjection.getMatrix()(3,i));
	
	/* Recalculate the base plane equation in depth image space: */
	setBasePlane(basePlane);
	}

void DepthImageRenderer::setBasePlane(const Plane& newBasePlane)
	{
	/* Set the base plane: */
	basePlane=newBasePlane;
	
	/* Transform the base plane to depth image space and into a GLSL-compatible format: */
	const PTransform::Matrix& dpm=depthProjection.getMatrix();
	const Plane::Vector& bpn=basePlane.getNormal();
	Scalar bpo=basePlane.getOffset();
	for(int i=0;i<4;++i)
		basePlaneDicEq[i]=GLfloat(dpm(0,i)*bpn[0]+dpm(1,i)*bpn[1]+dpm(2,i)*bpn[2]-dpm(3,i)*bpo);
	}

void DepthImageRenderer::setDepthImage(const Kinect::FrameBuffer& newDepthImage)
	{
	/* Update the depth image: */
	depthImage=newDepthImage;
	++depthImageVersion;
	}

Scalar DepthImageRenderer::intersectLine(const Point& p0,const Point& p1,Scalar elevationMin,Scalar elevationMax) const
	{
	/* Initialize the line segment: */
	Scalar lambda0=Scalar(0);
	Scalar lambda1=Scalar(1);
	
	/* Intersect the line segment with the upper elevation plane: */
	Scalar d0=basePlane.calcDistance(p0);
	Scalar d1=basePlane.calcDistance(p1);
	if(d0*d1<Scalar(0))
		{
		/* Calculate the intersection parameter: */
		
		// IMPLEMENT ME!
		
		return Scalar(2);
		}
	else if(d1>Scalar(0))
		{
		/* Trivially reject with maximum intercept: */
		return Scalar(2);
		}
	
	return Scalar(2);
	}

void DepthImageRenderer::uploadDepthProjection(GLint location) const
	{
	/* Upload the matrix to OpenGL: */
	glUniformMatrix4fvARB(location,1,GL_FALSE,depthProjectionMatrix);
	}

void DepthImageRenderer::bindDepthTexture(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the depth image texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
	
	/* Check if the texture is outdated: */
	if(dataItem->depthTextureVersion!=depthImageVersion)
		{
		/* Upload the new depth texture: */
		glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,depthImageSize[0],depthImageSize[1],GL_LUMINANCE,GL_FLOAT,depthImage.getData<GLfloat>());
		
		/* Mark the depth texture as current: */
		dataItem->depthTextureVersion=depthImageVersion;
		}
	}

void DepthImageRenderer::renderSurfaceTemplate(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Draw the surface template: */
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	GLuint* indexPtr=0;
	for(unsigned int y=1;y<depthImageSize[1];++y,indexPtr+=depthImageSize[0]*2)
		glDrawElements(GL_QUAD_STRIP,depthImageSize[0]*2,GL_UNSIGNED_INT,indexPtr);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	}

void DepthImageRenderer::renderDepth(const PTransform& projectionModelview,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the depth rendering shader: */
	glUseProgramObjectARB(dataItem->depthShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Bind the depth image texture: */
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
	
	/* Check if the texture is outdated: */
	if(dataItem->depthTextureVersion!=depthImageVersion)
		{
		/* Upload the new depth texture: */
		glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,depthImageSize[0],depthImageSize[1],GL_LUMINANCE,GL_FLOAT,depthImage.getData<GLfloat>());
		
		/* Mark the depth texture as current: */
		dataItem->depthTextureVersion=depthImageVersion;
		}
	glUniform1iARB(dataItem->depthShaderUniforms[0],0); // Tell the shader that the depth texture is in texture unit 0
	
	/* Upload the combined projection, modelview, and depth projection matrix: */
	PTransform pmvdp=projectionModelview;
	pmvdp*=depthProjection;
	glUniformARB(dataItem->depthShaderUniforms[1],pmvdp);
	
	/* Draw the surface: */
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	GLuint* indexPtr=0;
	for(unsigned int y=1;y<depthImageSize[1];++y,indexPtr+=depthImageSize[0]*2)
		glDrawElements(GL_QUAD_STRIP,depthImageSize[0]*2,GL_UNSIGNED_INT,indexPtr);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the depth rendering shader: */
	glUseProgramObjectARB(0);
	}

void DepthImageRenderer::renderElevation(const PTransform& projectionModelview,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the elevation rendering shader: */
	glUseProgramObjectARB(dataItem->elevationShader);
	
	/* Set up the depth image texture: */
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
	
	/* Check if the texture is outdated: */
	if(dataItem->depthTextureVersion!=depthImageVersion)
		{
		/* Upload the new depth texture: */
		glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,depthImageSize[0],depthImageSize[1],GL_LUMINANCE,GL_FLOAT,depthImage.getData<GLfloat>());
		
		/* Mark the depth texture as current: */
		dataItem->depthTextureVersion=depthImageVersion;
		}
	glUniform1iARB(dataItem->elevationShaderUniforms[0],0); // Tell the shader that the depth texture is in texture unit 0
	
	/* Upload the base plane equation in depth image space: */
	glUniformARB<4>(dataItem->elevationShaderUniforms[1],1,basePlaneDicEq);
	
	/* Upload the base weight equation in depth image space: */
	glUniformARB<4>(dataItem->elevationShaderUniforms[2],1,weightDicEq);
	
	/* Upload the combined projection, modelview, and depth projection matrix: */
	PTransform pmvdp=projectionModelview;
	pmvdp*=depthProjection;
	glUniformARB(dataItem->elevationShaderUniforms[3],pmvdp);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Draw the surface: */
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	GLuint* indexPtr=0;
	for(unsigned int y=1;y<depthImageSize[1];++y,indexPtr+=depthImageSize[0]*2)
		glDrawElements(GL_QUAD_STRIP,depthImageSize[0]*2,GL_UNSIGNED_INT,indexPtr);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Unbind the elevation rendering shader: */
	glUseProgramObjectARB(0);
	}
