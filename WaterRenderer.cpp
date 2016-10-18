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

#include "WaterRenderer.h"

// DEBUGGING
#include <iostream>

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

#include "WaterTable2.h"
#include "ShaderHelper.h"

/****************************************
Methods of class WaterRenderer::DataItem:
****************************************/

WaterRenderer::DataItem::DataItem(void)
	:vertexBuffer(0),indexBuffer(0),
	 waterShader(0)
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
	
	/* Allocate the buffers: */
	glGenBuffersARB(1,&vertexBuffer);
	glGenBuffersARB(1,&indexBuffer);
	}

WaterRenderer::DataItem::~DataItem(void)
	{
	/* Release all allocated buffers and shaders: */
	glDeleteBuffersARB(1,&vertexBuffer);
	glDeleteBuffersARB(1,&indexBuffer);
	glDeleteObjectARB(waterShader);
	}

/******************************
Methods of class WaterRenderer:
******************************/

WaterRenderer::WaterRenderer(const WaterTable2* sWaterTable)
	:waterTable(sWaterTable)
	{
	/* Copy the water table's grid sizes and grid cell size: */
	for(int i=0;i<2;++i)
		{
		bathymetryGridSize[i]=waterTable->getSize()[i]-1;
		waterGridSize[i]=waterTable->getSize()[i];
		cellSize[i]=waterTable->getCellSize()[i];
		}
	
	/* Get the water table's domain: */
	const WaterTable2::Box& wd=waterTable->getDomain();
	
	/* Calculate the transformation from grid space to world space: */
	gridTransform=PTransform::identity;
	PTransform::Matrix& gtm=gridTransform.getMatrix();
	gtm(0,0)=(wd.max[0]-wd.min[0])/Scalar(waterGridSize[0]);
	gtm(0,3)=wd.min[0];
	gtm(1,1)=(wd.max[1]-wd.min[1])/Scalar(waterGridSize[1]);
	gtm(1,3)=wd.min[1];
	gridTransform.leftMultiply(Geometry::invert(waterTable->getBaseTransform()));
	
	/* Calculate the transposed tangent-plane transformation from grid space to world space: */
	tangentGridTransform=PTransform::identity;
	PTransform::Matrix& tgtm=tangentGridTransform.getMatrix();
	tgtm(0,0)=Scalar(waterGridSize[0])/(wd.max[0]-wd.min[0]);
	tgtm(0,3)=-wd.min[0]*tgtm(0,0);
	tgtm(1,1)=Scalar(waterGridSize[1])/(wd.max[1]-wd.min[1]);
	tgtm(1,3)=-wd.min[1]*tgtm(1,1);
	tangentGridTransform*=waterTable->getBaseTransform();
	}

void WaterRenderer::initContext(GLContextData& contextData) const
	{
	/* Create a data item and add it to the context: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	/* Upload the grid of template vertices into the vertex buffer: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB,waterGridSize[1]*waterGridSize[0]*sizeof(Vertex),0,GL_STATIC_DRAW_ARB);
	Vertex* vPtr=static_cast<Vertex*>(glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	for(unsigned int y=0;y<waterGridSize[1];++y)
		for(unsigned int x=0;x<waterGridSize[0];++x,++vPtr)
			{
			/* Set the template vertex' position to the pixel center's position: */
			vPtr->position[0]=GLfloat(x)+0.5f;
			vPtr->position[1]=GLfloat(y)+0.5f;
			}
	glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	
	/* Upload the surface's triangle indices into the index buffer: */
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB,(waterGridSize[1]-1)*waterGridSize[0]*2*sizeof(GLuint),0,GL_STATIC_DRAW_ARB);
	GLuint* iPtr=static_cast<GLuint*>(glMapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	for(unsigned int y=1;y<waterGridSize[1];++y)
		for(unsigned int x=0;x<waterGridSize[0];++x,iPtr+=2)
			{
			iPtr[0]=GLuint(y*waterGridSize[0]+x);
			iPtr[1]=GLuint((y-1)*waterGridSize[0]+x);
			}
	glUnmapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Create the water rendering shader: */
	dataItem->waterShader=linkVertexAndFragmentShader("WaterRenderingShader");
	GLint* ulPtr=dataItem->waterShaderUniforms;
	*(ulPtr++)=glGetUniformLocationARB(dataItem->waterShader,"quantitySampler");
	*(ulPtr++)=glGetUniformLocationARB(dataItem->waterShader,"bathymetrySampler");
	*(ulPtr++)=glGetUniformLocationARB(dataItem->waterShader,"modelviewGridMatrix");
	*(ulPtr++)=glGetUniformLocationARB(dataItem->waterShader,"tangentModelviewGridMatrix");
	*(ulPtr++)=glGetUniformLocationARB(dataItem->waterShader,"projectionModelviewGridMatrix");
	}

void WaterRenderer::render(const PTransform& projection,const OGTransform& modelview,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Calculate the required matrices: */
	PTransform projectionModelview=projection;
	projectionModelview*=modelview;
	
	/* Bind the water rendering shader: */
	glUseProgramObjectARB(dataItem->waterShader);
	const GLint* ulPtr=dataItem->waterShaderUniforms;
	
	/* Bind the water quantity texture: */
	glActiveTextureARB(GL_TEXTURE0_ARB);
	waterTable->bindQuantityTexture(contextData);
	glUniform1iARB(*(ulPtr++),0);
	
	/* Bind the bathymetry texture: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	waterTable->bindBathymetryTexture(contextData);
	glUniform1iARB(*(ulPtr++),1);
	
	/* Calculate and upload the vertex transformation from grid space to eye space: */
	PTransform modelviewGridTransform=gridTransform;
	modelviewGridTransform.leftMultiply(modelview);
	glUniformARB(*(ulPtr++),modelviewGridTransform);
	
	/* Calculate the transposed tangent plane transformation from grid space to eye space: */
	PTransform tangentModelviewGridTransform=tangentGridTransform;
	tangentModelviewGridTransform*=Geometry::invert(modelview);
	
	/* Transpose and upload the transposed tangent plane transformation: */
	const Scalar* tmvgtPtr=tangentModelviewGridTransform.getMatrix().getEntries();
	GLfloat tangentModelviewGridTransformMatrix[16];
	GLfloat* tmvgtmPtr=tangentModelviewGridTransformMatrix;
	for(int i=0;i<16;++i,++tmvgtPtr,++tmvgtmPtr)
		*tmvgtmPtr=GLfloat(*tmvgtPtr);
	glUniformMatrix4fvARB(*(ulPtr++),1,GL_FALSE,tangentModelviewGridTransformMatrix);
	
	/* Calculate and upload the vertex transformation from grid space to clip space: */
	PTransform projectionModelviewGridTransform=gridTransform;
	projectionModelviewGridTransform.leftMultiply(modelview);
	projectionModelviewGridTransform.leftMultiply(projection);
	glUniformARB(*(ulPtr++),projectionModelviewGridTransform);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Draw the surface: */
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	GLuint* indexPtr=0;
	for(unsigned int y=1;y<waterGridSize[1];++y,indexPtr+=waterGridSize[0]*2)
		glDrawElements(GL_QUAD_STRIP,waterGridSize[0]*2,GL_UNSIGNED_INT,indexPtr);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Unbind the water rendering shader: */
	glUseProgramObjectARB(0);
	}
