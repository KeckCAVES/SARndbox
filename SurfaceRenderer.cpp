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

#include "SurfaceRenderer.h"

#include <string>
#include <vector>
#include <iostream>
#include <Misc/PrintInteger.h>
#include <Misc/ThrowStdErr.h>
#include <GL/gl.h>
#include <GL/GLVertexArrayParts.h>
#include <GL/Extensions/GLARBFragmentShader.h>
#include <GL/Extensions/GLARBMultitexture.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/Extensions/GLARBTextureFloat.h>
#include <GL/Extensions/GLARBTextureRectangle.h>
#include <GL/Extensions/GLARBTextureRg.h>
#include <GL/Extensions/GLARBVertexBufferObject.h>
#include <GL/Extensions/GLARBVertexShader.h>
#include <GL/Extensions/GLEXTFramebufferObject.h>
#include <GL/GLLightTracker.h>
#include <GL/GLContextData.h>
#include <GL/GLGeometryVertex.h>

#include "WaterTable2.h"

namespace {

/****************
Helper functions:
****************/

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

/******************************************
Methods of class SurfaceRenderer::DataItem:
******************************************/

SurfaceRenderer::DataItem::DataItem(void)
	:vertexBuffer(0),indexBuffer(0),
	 depthTexture(0),depthTextureVersion(0),
	 depthShader(0),elevationShader(0),
	 contourLineFramebufferObject(0),contourLineDepthBufferObject(0),contourLineColorTextureObject(0),
	 heightMapShader(0),lightTrackerVersion(0),surfaceSettingsVersion(0),
	 globalAmbientHeightMapShader(0),shadowedIlluminatedHeightMapShader(0)
	{
	/* Check if all required extensions are supported: */
	bool supported=GLARBFragmentShader::isSupported();
	supported=supported&&GLARBMultitexture::isSupported();
	supported=supported&&GLARBShaderObjects::isSupported();
	supported=supported&&GLARBTextureRectangle::isSupported();
	supported=supported&&GLARBTextureRg::isSupported();
	supported=supported&&GLARBTextureFloat::isSupported();
	supported=supported&&GLARBVertexBufferObject::isSupported();
	supported=supported&&GLARBVertexShader::isSupported();
	supported=supported&&GLEXTFramebufferObject::isSupported();
	if(!supported)
		Misc::throwStdErr("SurfaceRenderer: Not all required extensions are supported by local OpenGL");
	
	/* Initialize all required extensions: */
	GLARBFragmentShader::initExtension();
	GLARBMultitexture::initExtension();
	GLARBShaderObjects::initExtension();
	GLARBTextureFloat::initExtension();
	GLARBTextureRectangle::initExtension();
	GLARBTextureRg::initExtension();
	GLARBVertexBufferObject::initExtension();
	GLARBVertexShader::initExtension();
	GLEXTFramebufferObject::initExtension();
	
	/* Allocate the buffers and textures: */
	glGenBuffersARB(1,&vertexBuffer);
	glGenBuffersARB(1,&indexBuffer);
	glGenTextures(1,&depthTexture);
	}

SurfaceRenderer::DataItem::~DataItem(void)
	{
	/* Release all allocated buffers, textures, and shaders: */
	glDeleteBuffersARB(1,&vertexBuffer);
	glDeleteBuffersARB(1,&indexBuffer);
	glDeleteTextures(1,&depthTexture);
	glDeleteObjectARB(depthShader);
	glDeleteObjectARB(elevationShader);
	glDeleteFramebuffersEXT(1,&contourLineFramebufferObject);
	glDeleteRenderbuffersEXT(1,&contourLineDepthBufferObject);
	glDeleteTextures(1,&contourLineColorTextureObject);
	glDeleteObjectARB(heightMapShader);
	glDeleteObjectARB(globalAmbientHeightMapShader);
	glDeleteObjectARB(shadowedIlluminatedHeightMapShader);
	}

/********************************
Methods of class SurfaceRenderer:
********************************/

void SurfaceRenderer::shaderSourceFileChanged(const IO::FileMonitor::Event& event)
	{
	/* Invalidate the single-pass surface shader: */
	++surfaceSettingsVersion;
	}

GLhandleARB SurfaceRenderer::createSinglePassSurfaceShader(const GLLightTracker& lt,GLint* uniformLocations) const
	{
	GLhandleARB result=0;
	
	std::vector<GLhandleARB> shaders;
	try
		{
		/*********************************************************************
		Assemble and compile the surface rendering vertex shader:
		*********************************************************************/
		
		/* Assemble the function and declaration strings: */
		std::string vertexFunctions="\
			#extension GL_ARB_texture_rectangle : enable\n";
		
		std::string vertexUniforms="\
			uniform sampler2DRect depthSampler; // Sampler for the depth image-space elevation texture\n\
			uniform mat4 depthProjection; // Transformation from depth image space to camera space\n";
		
		std::string vertexVaryings;
		
		/* Assemble the vertex shader's main function: */
		std::string vertexMain="\
			void main()\n\
				{\n\
				/* Get the vertex' depth image-space z coordinate from the texture: */\n\
				vec4 vertexDic=gl_Vertex;\n\
				vertexDic.z=texture2DRect(depthSampler,vertexDic.xy).r;\n\
				\n\
				/* Transform the vertex from depth image space to camera space: */\n\
				vec4 vertexCc=depthProjection*vertexDic;\n\
				\n";
		
		if(useHeightMap)
			{
			/* Add declarations for height mapping: */
			vertexUniforms+="\
				uniform vec4 basePlane; // Plane equation of the base plane\n\
				uniform vec2 heightColorMapTransformation; // Transformation from elevation to height color map texture coordinate\n";
			
			vertexVaryings+="\
				varying float heightColorMapTexCoord; // Texture coordinate for the height color map\n";
			
			/* Add height mapping code to vertex shader's main function: */
			vertexMain+="\
				/* Plug camera-space vertex into the base plane equation: */\n\
				float elevation=dot(basePlane,vertexCc)/vertexCc.w;\n\
				\n\
				/* Transform elevation to height color map texture coordinate: */\n\
				heightColorMapTexCoord=elevation*heightColorMapTransformation.x+heightColorMapTransformation.y;\n\
				\n";
			}
		
		if(illuminate)
			{
			/* Add declarations for illumination: */
			vertexUniforms+="\
				uniform mat4 tangentDepthProjection; // Transformation from depth image space to camera space for tangent planes\n";
			
			vertexVaryings+="\
				varying vec4 diffColor,specColor; // Diffuse and specular colors\n";
			
			/* Add illumination code to vertex shader's main function: */
			vertexMain+="\
				/* Calculate the vertex' tangent plane equation in depth image space: */\n\
				vec4 tangentDic;\n\
				tangentDic.x=texture2DRect(depthSampler,vec2(vertexDic.x-1.0,vertexDic.y)).r-texture2DRect(depthSampler,vec2(vertexDic.x+1.0,vertexDic.y)).r;\n\
				tangentDic.y=texture2DRect(depthSampler,vec2(vertexDic.x,vertexDic.y-1.0)).r-texture2DRect(depthSampler,vec2(vertexDic.x,vertexDic.y+1.0)).r;\n\
				tangentDic.z=2.0;\n\
				tangentDic.w=-dot(vertexDic.xyz,tangentDic.xyz)/vertexDic.w;\n\
				\n\
				/* Transform the vertex' tangent plane from depth image space to camera space: */\n\
				vec3 normalCc=(tangentDepthProjection*tangentDic).xyz;\n\
				\n\
				/* Transform vertex and normal to eye coordinates for illumination: */\n\
				vec4 vertexEc=gl_ModelViewMatrix*vertexCc;\n\
				vec3 normalEc=normalize(gl_NormalMatrix*normalCc);\n\
				\n\
				/* Initialize the color accumulators: */\n\
				diffColor=gl_LightModel.ambient*gl_FrontMaterial.ambient;\n\
				specColor=vec4(0.0,0.0,0.0,0.0);\n\
				\n";
			
			/* Call the appropriate light accumulation function for every enabled light source: */
			bool firstLight=true;
			for(int lightIndex=0;lightIndex<lt.getMaxNumLights();++lightIndex)
				if(lt.getLightState(lightIndex).isEnabled())
					{
					/* Create the light accumulation function: */
					vertexFunctions.push_back('\n');
					vertexFunctions+=lt.createAccumulateLightFunction(lightIndex);
					
					if(firstLight)
						{
						vertexMain+="\
							/* Call the light accumulation functions for all enabled light sources: */\n";
						firstLight=false;
						}
					
					/* Call the light accumulation function from vertex shader's main function: */
					vertexMain+="\
						accumulateLight";
					char liBuffer[12];
					vertexMain.append(Misc::print(lightIndex,liBuffer+11));
					vertexMain+="(vertexEc,normalEc,gl_FrontMaterial.ambient,gl_FrontMaterial.diffuse,gl_FrontMaterial.specular,gl_FrontMaterial.shininess,diffColor,specColor);\n";
					}
			if(!firstLight)
				vertexMain+="\
					\n";
			}
		
		if(waterTable!=0)
			{
			/* Add declarations for water handling: */
			vertexUniforms+="\
				uniform mat4 waterLevelTextureTransformation; // Transformation from camera space to water level texture coordinate space\n";
			vertexVaryings+="\
				varying vec2 waterLevelTexCoord; // Texture coordinate for water level texture\n";
			
			/* Add water handling code to vertex shader's main function: */
			vertexMain+="\
				/* Transform the vertex from camera space to water level texture coordinate space: */\n\
				vec4 wltc=waterLevelTextureTransformation*vertexCc;\n\
				waterLevelTexCoord=wltc.xy/wltc.w;\n\
				\n";
			}
		
		/* Finish the vertex shader's main function: */
		vertexMain+="\
				/* Transform vertex to clip coordinates: */\n\
				gl_Position=gl_ModelViewProjectionMatrix*vertexCc;\n\
				}\n";
		
		/* Compile the vertex shader: */
		shaders.push_back(glCompileVertexShaderFromStrings(7,vertexFunctions.c_str(),"\t\t\n",vertexUniforms.c_str(),"\t\t\n",vertexVaryings.c_str(),"\t\t\n",vertexMain.c_str()));
		
		/*********************************************************************
		Assemble and compile the surface rendering fragment shaders:
		*********************************************************************/
		
		/* Assemble the fragment shader's function declarations: */
		std::string fragmentDeclarations;
		
		/* Assemble the fragment shader's uniform and varying variables: */
		std::string fragmentUniforms;
		std::string fragmentVaryings;
		
		/* Assemble the fragment shader's main function: */
		std::string fragmentMain="\
			void main()\n\
				{\n";
		
		if(useHeightMap)
			{
			/* Add declarations for height mapping: */
			fragmentUniforms+="\
				uniform sampler1D heightColorMapSampler;\n";
			fragmentVaryings+="\
				varying float heightColorMapTexCoord; // Texture coordinate for the height color map\n";
			
			/* Add height mapping code to the fragment shader's main function: */
			fragmentMain+="\
				/* Get the fragment's color from the height color map: */\n\
				vec4 baseColor=texture1D(heightColorMapSampler,heightColorMapTexCoord);\n\
				\n";
			}
		else
			{
			fragmentMain+="\
				/* Set the surface's base color to white: */\n\
				vec4 baseColor=vec4(1.0,1.0,1.0,1.0);\n\
				\n";
			}
		
		if(drawContourLines)
			{
			/* Declare the contour line function: */
			fragmentDeclarations+="\
				void addContourLines(in vec2,inout vec4);\n";
			
			/* Compile the contour line shader: */
			shaders.push_back(compileFragmentShader("SurfaceAddContourLines"));
			
			/* Call contour line function from fragment shader's main function: */
			fragmentMain+="\
				/* Modulate the base color by contour line color: */\n\
				addContourLines(gl_FragCoord.xy,baseColor);\n\
				\n";
			}
		
		if(illuminate)
			{
			/* Declare the illumination function: */
			fragmentDeclarations+="\
				void illuminate(inout vec4);\n";
			
			/* Compile the illumination shader: */
			shaders.push_back(compileFragmentShader("SurfaceIlluminate"));
			
			/* Call illumination function from fragment shader's main function: */
			fragmentMain+="\
				/* Apply illumination to the base color: */\n\
				illuminate(baseColor);\n\
				\n";
			}
		
		if(waterTable!=0)
			{
			/* Declare the water handling functions: */
			fragmentDeclarations+="\
				void addWaterColor(in vec2,inout vec4);\n\
				void addWaterColorAdvected(inout vec4);\n";
			
			/* Compile the water handling shader: */
			shaders.push_back(compileFragmentShader("SurfaceAddWaterColor"));
			
			/* Call water coloring function from fragment shader's main function: */
			if(advectWaterTexture)
				{
				fragmentMain+="\
					/* Modulate the base color with water color: */\n\
					addWaterColorAdvected(baseColor);\n\
					\n";
				}
			else
				{
				fragmentMain+="\
					/* Modulate the base color with water color: */\n\
					addWaterColor(gl_FragCoord.xy,baseColor);\n\
					\n";
				}
			}
		
		/* Finish the fragment shader's main function: */
		fragmentMain+="\
			/* Assign the final color to the fragment: */\n\
			gl_FragColor=baseColor;\n\
			}\n";
		
		/* Compile the fragment shader: */
		shaders.push_back(glCompileFragmentShaderFromStrings(7,fragmentDeclarations.c_str(),"\t\t\n",fragmentUniforms.c_str(),fragmentVaryings.c_str(),"\t\t\n","\t\t\n",fragmentMain.c_str()));
		
		/* Link the shader program: */
		result=glLinkShader(shaders);
		
		/* Release all compiled shaders: */
		for(std::vector<GLhandleARB>::iterator shIt=shaders.begin();shIt!=shaders.end();++shIt)
			glDeleteObjectARB(*shIt);
		
		/*******************************************************************
		Query the shader program's uniform locations:
		*******************************************************************/
		
		GLint* ulPtr=uniformLocations;
		
		/* Query common uniform variables: */
		*(ulPtr++)=glGetUniformLocationARB(result,"depthSampler");
		*(ulPtr++)=glGetUniformLocationARB(result,"depthProjection");
		if(useHeightMap)
			{
			*(ulPtr++)=glGetUniformLocationARB(result,"basePlane");
			*(ulPtr++)=glGetUniformLocationARB(result,"heightColorMapTransformation");
			*(ulPtr++)=glGetUniformLocationARB(result,"heightColorMapSampler");
			}
		if(drawContourLines)
			{
			*(ulPtr++)=glGetUniformLocationARB(result,"pixelCornerElevationSampler");
			*(ulPtr++)=glGetUniformLocationARB(result,"contourLineFactor");
			}
		if(illuminate)
			{
			/* Query illumination uniform variables: */
			*(ulPtr++)=glGetUniformLocationARB(result,"tangentDepthProjection");
			}
		if(waterTable!=0)
			{
			/* Query water handling uniform variables: */
			*(ulPtr++)=glGetUniformLocationARB(result,"waterLevelTextureTransformation");
			*(ulPtr++)=glGetUniformLocationARB(result,"bathymetrySampler");
			*(ulPtr++)=glGetUniformLocationARB(result,"quantitySampler");
			*(ulPtr++)=glGetUniformLocationARB(result,"waterOpacity");
			*(ulPtr++)=glGetUniformLocationARB(result,"waterAnimationTime");
			}
		}
	catch(...)
		{
		/* Clean up and re-throw the exception: */
		for(std::vector<GLhandleARB>::iterator shIt=shaders.begin();shIt!=shaders.end();++shIt)
			glDeleteObjectARB(*shIt);
		throw;
		}
	
	return result;
	}

SurfaceRenderer::SurfaceRenderer(const unsigned int sSize[2],const SurfaceRenderer::PTransform& sDepthProjection,const SurfaceRenderer::Plane& sBasePlane)
	:depthProjection(sDepthProjection),
	 basePlane(sBasePlane),
	 usePreboundDepthTexture(false),
	 drawContourLines(true),contourLineFactor(1.0f),
	 useHeightMap(true),heightMapScale(1.0f),heightMapOffset(0.0f),
	 illuminate(false),waterTable(0),advectWaterTexture(false),surfaceSettingsVersion(1),
	 waterOpacity(2.0f),
	 depthImageVersion(1),
	 animationTime(0.0)
	{
	/* Monitor the external shader source files: */
	fileMonitor.addPath((std::string(SHADERDIR)+std::string("/SurfaceAddContourLines.fs")).c_str(),IO::FileMonitor::Modified,Misc::createFunctionCall(this,&SurfaceRenderer::shaderSourceFileChanged));
	fileMonitor.addPath((std::string(SHADERDIR)+std::string("/SurfaceIlluminate.fs")).c_str(),IO::FileMonitor::Modified,Misc::createFunctionCall(this,&SurfaceRenderer::shaderSourceFileChanged));
	fileMonitor.addPath((std::string(SHADERDIR)+std::string("/SurfaceAddWaterColor.fs")).c_str(),IO::FileMonitor::Modified,Misc::createFunctionCall(this,&SurfaceRenderer::shaderSourceFileChanged));
	fileMonitor.startPolling();
	
	/* Copy the depth image size: */
	for(int i=0;i<2;++i)
		size[i]=sSize[i];
	
	/* Check if the depth projection matrix retains right-handedness: */
	PTransform::Point p1=depthProjection.transform(PTransform::Point(0,0,0));
	PTransform::Point p2=depthProjection.transform(PTransform::Point(1,0,0));
	PTransform::Point p3=depthProjection.transform(PTransform::Point(0,1,0));
	PTransform::Point p4=depthProjection.transform(PTransform::Point(0,0,1));
	bool depthProjectionInverts=Geometry::cross(p2-p1,p3-p1)*(p4-p1)<Scalar(0);
	
	/* Convert the depth projection matrix to column-major OpenGL format: */
	GLfloat* dpmPtr=depthProjectionMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++dpmPtr)
			*dpmPtr=depthProjection.getMatrix()(i,j);
	
	/* Calculate the tangent plane depth projection: */
	PTransform tangentDepthProjection=Geometry::invert(depthProjection);
	if(depthProjectionInverts)
		tangentDepthProjection.leftMultiply(PTransform::scale(PTransform::Scale(-1,-1,-1)));
	
	GLfloat* tdpmPtr=tangentDepthProjectionMatrix;
	for(int i=0;i<4;++i)
		for(int j=0;j<4;++j,++tdpmPtr)
			*tdpmPtr=tangentDepthProjection.getMatrix()(i,j);
	
	/* Convert the base plane to a homogeneous plane equation: */
	for(int i=0;i<3;++i)
		basePlaneEq[i]=GLfloat(basePlane.getNormal()[i]);
	basePlaneEq[3]=GLfloat(-basePlane.getOffset());
	
	/* Initialize the depth image: */
	depthImage=Kinect::FrameBuffer(size[0],size[1],size[1]*size[0]*sizeof(float));
	float* diPtr=static_cast<float*>(depthImage.getBuffer());
	for(unsigned int y=0;y<size[1];++y)
		for(unsigned int x=0;x<size[0];++x,++diPtr)
			*diPtr=0.0f;
	}

void SurfaceRenderer::initContext(GLContextData& contextData) const
	{
	/* Create a data item and add it to the context: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	/* Upload the grid of template vertices into the vertex buffer: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB,size[1]*size[0]*sizeof(Vertex),0,GL_STATIC_DRAW_ARB);
	Vertex* vPtr=static_cast<Vertex*>(glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	for(unsigned int y=0;y<size[1];++y)
		for(unsigned int x=0;x<size[0];++x,++vPtr)
			{
			vPtr->position[0]=float(x)+0.5f;
			vPtr->position[1]=float(y)+0.5f;
			vPtr->position[2]=0.0f;
			}
	glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	
	/* Upload the surface's triangle indices into the index buffer: */
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	glBufferDataARB(GL_ELEMENT_ARRAY_BUFFER_ARB,(size[1]-1)*size[0]*2*sizeof(GLuint),0,GL_STATIC_DRAW_ARB);
	GLuint* iPtr=static_cast<GLuint*>(glMapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
	for(unsigned int y=1;y<size[1];++y)
		for(unsigned int x=0;x<size[0];++x,iPtr+=2)
			{
			iPtr[0]=GLuint(y*size[0]+x);
			iPtr[1]=GLuint((y-1)*size[0]+x);
			}
	glUnmapBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Initialize the depth image texture: */
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_LUMINANCE32F_ARB,size[0],size[1],0,GL_LUMINANCE,GL_UNSIGNED_BYTE,0);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	
	/* Create the surface depth render shader: */
	{
	GLhandleARB vertexShader=compileVertexShader("SurfaceDepthShader");
	GLhandleARB fragmentShader=compileFragmentShader("SurfaceDepthShader");
	dataItem->depthShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->depthShaderUniforms[0]=glGetUniformLocationARB(dataItem->depthShader,"depthSampler");
	dataItem->depthShaderUniforms[1]=glGetUniformLocationARB(dataItem->depthShader,"projectionModelviewDepthProjection");
	}
	
	/* Create the surface elevation render shader: */
	{
	GLhandleARB vertexShader=compileVertexShader("SurfaceElevationShader");
	GLhandleARB fragmentShader=compileFragmentShader("SurfaceElevationShader");
	dataItem->elevationShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->elevationShaderUniforms[0]=glGetUniformLocationARB(dataItem->elevationShader,"depthSampler");
	dataItem->elevationShaderUniforms[1]=glGetUniformLocationARB(dataItem->elevationShader,"depthProjection");
	dataItem->elevationShaderUniforms[2]=glGetUniformLocationARB(dataItem->elevationShader,"basePlane");
	}
	
	/* Create the height map render shader: */
	dataItem->heightMapShader=createSinglePassSurfaceShader(*contextData.getLightTracker(),dataItem->heightMapShaderUniforms);
	dataItem->surfaceSettingsVersion=surfaceSettingsVersion;
	dataItem->lightTrackerVersion=contextData.getLightTracker()->getVersion();
	
	/* Create the global ambient height map render shader: */
	{
	GLhandleARB vertexShader=compileVertexShader("SurfaceGlobalAmbientHeightMapShader");
	GLhandleARB fragmentShader=compileFragmentShader("SurfaceGlobalAmbientHeightMapShader");
	dataItem->globalAmbientHeightMapShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->globalAmbientHeightMapShaderUniforms[0]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"depthSampler");
	dataItem->globalAmbientHeightMapShaderUniforms[1]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"depthProjection");
	dataItem->globalAmbientHeightMapShaderUniforms[2]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"basePlane");
	dataItem->globalAmbientHeightMapShaderUniforms[3]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"pixelCornerElevationSampler");
	dataItem->globalAmbientHeightMapShaderUniforms[4]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"contourLineFactor");
	dataItem->globalAmbientHeightMapShaderUniforms[5]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"heightColorMapSampler");
	dataItem->globalAmbientHeightMapShaderUniforms[6]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"heightColorMapTransformation");
	dataItem->globalAmbientHeightMapShaderUniforms[7]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"waterLevelSampler");
	dataItem->globalAmbientHeightMapShaderUniforms[8]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"waterLevelTextureTransformation");
	dataItem->globalAmbientHeightMapShaderUniforms[9]=glGetUniformLocationARB(dataItem->globalAmbientHeightMapShader,"waterOpacity");
	}
	
	/* Create the shadowed illuminated height map render shader: */
	{
	GLhandleARB vertexShader=compileVertexShader("SurfaceShadowedIlluminatedHeightMapShader");
	GLhandleARB fragmentShader=compileFragmentShader("SurfaceShadowedIlluminatedHeightMapShader");
	dataItem->shadowedIlluminatedHeightMapShader=glLinkShader(vertexShader,fragmentShader);
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[0]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"depthSampler");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[1]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"depthProjection");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[2]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"tangentDepthProjection");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[3]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"basePlane");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[4]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"pixelCornerElevationSampler");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[5]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"contourLineFactor");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[6]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"heightColorMapSampler");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[7]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"heightColorMapTransformation");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[8]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"waterLevelSampler");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[9]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"waterLevelTextureTransformation");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[10]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"waterOpacity");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[11]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"shadowTextureSampler");
	dataItem->shadowedIlluminatedHeightMapShaderUniforms[12]=glGetUniformLocationARB(dataItem->shadowedIlluminatedHeightMapShader,"shadowProjection");
	}
	}

void SurfaceRenderer::setUsePreboundDepthTexture(bool newUsePreboundDepthTexture)
	{
	usePreboundDepthTexture=newUsePreboundDepthTexture;
	}

void SurfaceRenderer::setDrawContourLines(bool newDrawContourLines)
	{
	drawContourLines=newDrawContourLines;
	++surfaceSettingsVersion;
	}

void SurfaceRenderer::setContourLineDistance(GLfloat newContourLineDistance)
	{
	/* Set the new contour line factor: */
	contourLineFactor=1.0f/newContourLineDistance;
	}

void SurfaceRenderer::setUseHeightMap(bool newUseHeightMap)
	{
	useHeightMap=newUseHeightMap;
	++surfaceSettingsVersion;
	}

void SurfaceRenderer::setHeightMapRange(GLsizei newHeightMapSize,GLfloat newMinElevation,GLfloat newMaxElevation)
	{
	/* Calculate the new height map elevation scaling and offset coefficients: */
	GLdouble hms=GLdouble(newHeightMapSize-1)/((newMaxElevation-newMinElevation)*GLdouble(newHeightMapSize));
	GLdouble hmo=0.5/GLdouble(newHeightMapSize)-hms*newMinElevation;
	
	heightMapScale=GLfloat(hms);
	heightMapOffset=GLfloat(hmo);
	}

void SurfaceRenderer::setIlluminate(bool newIlluminate)
	{
	illuminate=newIlluminate;
	++surfaceSettingsVersion;
	}

void SurfaceRenderer::setWaterTable(WaterTable2* newWaterTable)
	{
	waterTable=newWaterTable;
	++surfaceSettingsVersion;
	}

void SurfaceRenderer::setAdvectWaterTexture(bool newAdvectWaterTexture)
	{
	advectWaterTexture=false; // newAdvectWaterTexture;
	++surfaceSettingsVersion;
	}

void SurfaceRenderer::setWaterOpacity(GLfloat newWaterOpacity)
	{
	/* Set the new opacity factor: */
	waterOpacity=newWaterOpacity;
	}

void SurfaceRenderer::setDepthImage(const Kinect::FrameBuffer& newDepthImage)
	{
	/* Update the depth image: */
	depthImage=newDepthImage;
	++depthImageVersion;
	}

void SurfaceRenderer::setAnimationTime(double newAnimationTime)
	{
	/* Set the new animation time: */
	animationTime=newAnimationTime;
	
	/* Poll the file monitor: */
	fileMonitor.processEvents();
	}

void SurfaceRenderer::glRenderDepthOnly(const SurfaceRenderer::PTransform& modelviewProjection,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the depth rendering shader: */
	glUseProgramObjectARB(dataItem->depthShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		/* Bind the depth image texture: */
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(dataItem->depthShaderUniforms[0],0);
	
	/* Upload the combined projection, modelview, and depth projection matrix: */
	PTransform pmvdp=modelviewProjection;
	pmvdp*=depthProjection;
	GLfloat pmvdpMatrix[16];
	GLfloat* pmvdpPtr=pmvdpMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++pmvdpPtr)
			*pmvdpPtr=GLfloat(pmvdp.getMatrix()(i,j));
	glUniformMatrix4fvARB(dataItem->depthShaderUniforms[1],1,GL_FALSE,pmvdpMatrix);
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the depth rendering shader: */
	glUseProgramObjectARB(0);
	}

void SurfaceRenderer::glRenderElevation(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the elevation shader: */
	glUseProgramObjectARB(dataItem->elevationShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(dataItem->elevationShaderUniforms[0],0);
	
	/* Upload the depth projection matrix: */
	glUniformMatrix4fvARB(dataItem->elevationShaderUniforms[1],1,GL_FALSE,depthProjectionMatrix);
	
	/* Upload the base plane equation: */
	glUniformARB<4>(dataItem->elevationShaderUniforms[2],1,basePlaneEq);
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the elevation shader: */
	glUseProgramObjectARB(0);
	}

void SurfaceRenderer::glPrepareContourLines(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/*********************************************************************
	Prepare the half-pixel-offset frame buffer for subsequent per-fragment
	Marching Squares contour line extraction.
	*********************************************************************/
	
	/* Query the current viewport: */
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT,viewport);
	
	/* Save the currently-bound frame buffer and clear color: */
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	GLfloat currentClearColor[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE,currentClearColor);
	
	/* Check if the contour line frame buffer needs to be created: */
	if(dataItem->contourLineFramebufferObject==0)
		{
		/* Reset the frame buffer size: */
		for(int i=0;i<2;++i)
			dataItem->contourLineFramebufferSize[i]=0;
		
		/* Create and bind the frame buffer object: */
		glGenFramebuffersEXT(1,&dataItem->contourLineFramebufferObject);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->contourLineFramebufferObject);
		
		/* Create a depth buffer for topographic contour line rendering: */
		glGenRenderbuffersEXT(1,&dataItem->contourLineDepthBufferObject);
		
		/* Generate a color texture object for topographic contour line rendering: */
		glGenTextures(1,&dataItem->contourLineColorTextureObject);
		}
	else
		{
		/* Bind the frame buffer object: */
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->contourLineFramebufferObject);
		}
	
	/* Check if the contour line frame buffer needs to be resized: */
	if(dataItem->contourLineFramebufferSize[0]!=viewport[2]+1||dataItem->contourLineFramebufferSize[1]!=viewport[3]+1)
		{
		/* Update the frame buffer size: */
		bool mustAttachBuffers=dataItem->contourLineFramebufferSize[0]==0&&dataItem->contourLineFramebufferSize[1]==0;
		for(int i=0;i<2;++i)
			dataItem->contourLineFramebufferSize[i]=viewport[2+i]+1;
		
		/* Resize the topographic contour line rendering depth buffer: */
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,dataItem->contourLineDepthBufferObject);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH_COMPONENT,dataItem->contourLineFramebufferSize[0],dataItem->contourLineFramebufferSize[1]);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,0);
		
		/* Resize the topographic contour line rendering color texture: */
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->contourLineColorTextureObject);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_R32F,dataItem->contourLineFramebufferSize[0],dataItem->contourLineFramebufferSize[1],0,GL_LUMINANCE,GL_UNSIGNED_BYTE,0);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		
		if(mustAttachBuffers)
			{
			/* Attach the depth render buffer and color texture object to the contour line frame buffer: */
			glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,dataItem->contourLineDepthBufferObject);
			glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,dataItem->contourLineColorTextureObject,0);
			glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
			glReadBuffer(GL_NONE);
			}
		}
	
	/* Extend the viewport to render the corners of the final pixels: */
	glViewport(0,0,viewport[2]+1,viewport[3]+1);
	glClearColor(0.0f,0.0f,0.0f,1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	
	/* Adjust the projection matrix to render the corners of the final pixels: */
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	GLdouble proj[16];
	glGetDoublev(GL_PROJECTION_MATRIX,proj);
	GLdouble xs=GLdouble(viewport[2])/GLdouble(viewport[2]+1);
	GLdouble ys=GLdouble(viewport[3])/GLdouble(viewport[3]+1);
	for(int j=0;j<4;++j)
		{
		proj[j*4+0]*=xs;
		proj[j*4+1]*=ys;
		}
	glLoadIdentity();
	glMultMatrixd(proj);
	
	/*********************************************************************
	Render the surface's elevation into the half-pixel offset frame
	buffer.
	*********************************************************************/
	
	/* Bind the elevation shader: */
	glUseProgramObjectARB(dataItem->elevationShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(dataItem->elevationShaderUniforms[0],0);
	
	/* Upload the depth projection matrix: */
	glUniformMatrix4fvARB(dataItem->elevationShaderUniforms[1],1,GL_FALSE,depthProjectionMatrix);
	
	/* Upload the base plane equation: */
	glUniformARB<4>(dataItem->elevationShaderUniforms[2],1,basePlaneEq);
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the elevation shader: */
	glUseProgramObjectARB(0);
	
	/*********************************************************************
	Restore previous OpenGL state.
	*********************************************************************/
	
	/* Restore the original viewport and projection matrix: */
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
	
	/* Restore the original clear color and frame buffer binding: */
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	glClearColor(currentClearColor[0],currentClearColor[1],currentClearColor[2],currentClearColor[3]);
	}

void SurfaceRenderer::glRenderSinglePass(GLuint heightColorMapTexture,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Check if contour line rendering is enabled: */
	if(drawContourLines)
		{
		/* Run the first rendering pass to create a half-pixel offset texture of surface elevations: */
		glPrepareContourLines(contextData);
		}
	else if(dataItem->contourLineFramebufferObject!=0)
		{
		/* Delete the contour line rendering frame buffer: */
		glDeleteFramebuffersEXT(1,&dataItem->contourLineFramebufferObject);
		dataItem->contourLineFramebufferObject=0;
		glDeleteRenderbuffersEXT(1,&dataItem->contourLineDepthBufferObject);
		dataItem->contourLineDepthBufferObject=0;
		glDeleteTextures(1,&dataItem->contourLineColorTextureObject);
		dataItem->contourLineColorTextureObject=0;
		}
	
	/* Check if the single-pass surface shader is outdated: */
	if(dataItem->surfaceSettingsVersion!=surfaceSettingsVersion||dataItem->lightTrackerVersion!=contextData.getLightTracker()->getVersion())
		{
		/* Rebuild the shader: */
		try
			{
			GLhandleARB newShader=createSinglePassSurfaceShader(*contextData.getLightTracker(),dataItem->heightMapShaderUniforms);
			glDeleteObjectARB(dataItem->heightMapShader);
			dataItem->heightMapShader=newShader;
			}
		catch(std::runtime_error err)
			{
			std::cerr<<"Caught exception "<<err.what()<<" while rebuilding surface shader"<<std::endl;
			}
		
		/* Mark the shader as up-to-date: */
		dataItem->surfaceSettingsVersion=surfaceSettingsVersion;
		dataItem->lightTrackerVersion=contextData.getLightTracker()->getVersion();
		}
	
	/* Bind the single-pass surface shader: */
	glUseProgramObjectARB(dataItem->heightMapShader);
	const GLint* ulPtr=dataItem->heightMapShaderUniforms;
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(*(ulPtr++),0);
	
	/* Upload the depth projection matrix: */
	glUniformMatrix4fvARB(*(ulPtr++),1,GL_FALSE,depthProjectionMatrix);
	
	if(useHeightMap)
		{
		/* Upload the base plane equation: */
		glUniformARB<4>(*(ulPtr++),1,basePlaneEq);
		
		/* Upload the height color map texture coordinate transformation: */
		glUniform2fARB(*(ulPtr++),heightMapScale,heightMapOffset);
		
		/* Bind the height color map texture: */
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_1D,heightColorMapTexture);
		glUniform1iARB(*(ulPtr++),1);
		}
	
	if(drawContourLines)
		{
		/* Bind the pixel corner elevation texture: */
		glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->contourLineColorTextureObject);
		glUniform1iARB(*(ulPtr++),2);
		
		/* Upload the contour line distance factor: */
		glUniform1fARB(*(ulPtr++),contourLineFactor);
		}
	
	if(illuminate)
		{
		/* Upload the tangent-plane depth projection matrix: */
		glUniformMatrix4fvARB(*(ulPtr++),1,GL_FALSE,tangentDepthProjectionMatrix);
		}
	
	if(waterTable!=0)
		{
		/* Upload the water table texture coordinate matrix: */
		glUniformMatrix4fvARB(*(ulPtr++),1,GL_FALSE,waterTable->getWaterTextureMatrix());
		
		/* Bind the bathymetry texture: */
		glActiveTextureARB(GL_TEXTURE3_ARB);
		waterTable->bindBathymetryTexture(contextData);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
		glUniform1iARB(*(ulPtr++),3);
		
		/* Bind the quantities texture: */
		glActiveTextureARB(GL_TEXTURE4_ARB);
		waterTable->bindQuantityTexture(contextData);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
		glUniform1iARB(*(ulPtr++),4);
		
		/* Upload the water opacity factor: */
		glUniform1fARB(*(ulPtr++),waterOpacity);
		
		/* Upload the water animation time: */
		glUniform1fARB(*(ulPtr++),GLfloat(animationTime));
		}
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(waterTable!=0)
		{
		glActiveTextureARB(GL_TEXTURE4_ARB);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		glActiveTextureARB(GL_TEXTURE3_ARB);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP);
		glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	if(drawContourLines)
		{
		glActiveTextureARB(GL_TEXTURE2_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	if(useHeightMap)
		{
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glBindTexture(GL_TEXTURE_1D,0);
		}
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the height map shader: */
	glUseProgramObjectARB(0);
	}

void SurfaceRenderer::glRenderGlobalAmbientHeightMap(GLuint heightColorMapTexture,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Check if contour line rendering is enabled: */
	if(drawContourLines)
		{
		/* Run the first rendering pass to create a half-pixel offset texture of surface elevations: */
		glPrepareContourLines(contextData);
		}
	else if(dataItem->contourLineFramebufferObject!=0)
		{
		/* Delete the contour line rendering frame buffer: */
		glDeleteFramebuffersEXT(1,&dataItem->contourLineFramebufferObject);
		dataItem->contourLineFramebufferObject=0;
		glDeleteRenderbuffersEXT(1,&dataItem->contourLineDepthBufferObject);
		dataItem->contourLineDepthBufferObject=0;
		glDeleteTextures(1,&dataItem->contourLineColorTextureObject);
		dataItem->contourLineColorTextureObject=0;
		}
	
	/* Bind the global ambient height map shader: */
	glUseProgramObjectARB(dataItem->globalAmbientHeightMapShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(dataItem->globalAmbientHeightMapShaderUniforms[0],0);
	
	/* Upload the depth projection matrix: */
	glUniformMatrix4fvARB(dataItem->globalAmbientHeightMapShaderUniforms[1],1,GL_FALSE,depthProjectionMatrix);
	
	/* Upload the base plane equation: */
	glUniformARB<4>(dataItem->globalAmbientHeightMapShaderUniforms[2],1,basePlaneEq);
	
	/* Bind the pixel corner elevation texture: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->contourLineColorTextureObject);
	glUniform1iARB(dataItem->globalAmbientHeightMapShaderUniforms[3],1);
	
	/* Upload the contour line distance factor: */
	glUniform1fARB(dataItem->globalAmbientHeightMapShaderUniforms[4],contourLineFactor);
	
	/* Bind the height color map texture: */
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_1D,heightColorMapTexture);
	glUniform1iARB(dataItem->globalAmbientHeightMapShaderUniforms[5],2);
	
	/* Upload the height color map texture coordinate transformation: */
	glUniform2fARB(dataItem->globalAmbientHeightMapShaderUniforms[6],heightMapScale,heightMapOffset);
	
	if(waterTable!=0)
		{
		/* Bind the water level texture: */
		glActiveTextureARB(GL_TEXTURE3_ARB);
		//waterTable->bindWaterLevelTexture(contextData);
		glUniform1iARB(dataItem->globalAmbientHeightMapShaderUniforms[7],3);
		
		/* Upload the water table texture coordinate matrix: */
		glUniformMatrix4fvARB(dataItem->globalAmbientHeightMapShaderUniforms[8],1,GL_FALSE,waterTable->getWaterTextureMatrix());
		
		/* Upload the water opacity factor: */
		glUniform1fARB(dataItem->globalAmbientHeightMapShaderUniforms[9],waterOpacity);
		}
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(waterTable!=0)
		{
		glActiveTextureARB(GL_TEXTURE3_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_1D,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the global ambient height map shader: */
	glUseProgramObjectARB(0);
	}

void SurfaceRenderer::glRenderShadowedIlluminatedHeightMap(GLuint heightColorMapTexture,GLuint shadowTexture,const SurfaceRenderer::PTransform& shadowProjection,GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	/* Bind the shadowed illuminated height map shader: */
	glUseProgramObjectARB(dataItem->shadowedIlluminatedHeightMapShader);
	
	/* Bind the vertex and index buffers: */
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBuffer);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,dataItem->indexBuffer);
	
	/* Set up the depth image texture: */
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->depthTexture);
		
		/* Check if the texture is outdated: */
		if(dataItem->depthTextureVersion!=depthImageVersion)
			{
			/* Upload the new depth texture: */
			glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,size[0],size[1],GL_LUMINANCE,GL_FLOAT,depthImage.getBuffer());
			
			/* Mark the depth texture as current: */
			dataItem->depthTextureVersion=depthImageVersion;
			}
		}
	glUniform1iARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[0],0);
	
	/* Upload the depth projection matrix: */
	glUniformMatrix4fvARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[1],1,GL_FALSE,depthProjectionMatrix);
	
	/* Upload the tangent-plane depth projection matrix: */
	glUniformMatrix4fvARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[2],1,GL_FALSE,tangentDepthProjectionMatrix);
	
	/* Upload the base plane equation: */
	glUniformARB<4>(dataItem->shadowedIlluminatedHeightMapShaderUniforms[3],1,basePlaneEq);
	
	/* Bind the pixel corner elevation texture: */
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,dataItem->contourLineColorTextureObject);
	glUniform1iARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[4],1);
	
	/* Upload the contour line distance factor: */
	glUniform1fARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[5],contourLineFactor);
	
	/* Bind the height color map texture: */
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_1D,heightColorMapTexture);
	glUniform1iARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[6],2);
	
	/* Upload the height color map texture coordinate transformation: */
	glUniform2fARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[7],heightMapScale,heightMapOffset);
	
	if(waterTable!=0)
		{
		/* Bind the water level texture: */
		glActiveTextureARB(GL_TEXTURE3_ARB);
		//waterTable->bindWaterLevelTexture(contextData);
		glUniform1iARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[8],3);
		
		/* Upload the water table texture coordinate matrix: */
		glUniformMatrix4fvARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[9],1,GL_FALSE,waterTable->getWaterTextureMatrix());
		
		/* Upload the water opacity factor: */
		glUniform1fARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[10],waterOpacity);
		}
	
	/* Bind the shadow texture: */
	glActiveTextureARB(GL_TEXTURE4_ARB);
	glBindTexture(GL_TEXTURE_2D,shadowTexture);
	glUniform1iARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[11],4);
	
	/* Upload the combined shadow viewport, shadow projection and modelview, and depth projection matrix: */
	PTransform spdp(1.0);
	spdp.getMatrix()(0,0)=0.5;
	spdp.getMatrix()(0,3)=0.5;
	spdp.getMatrix()(1,1)=0.5;
	spdp.getMatrix()(1,3)=0.5;
	spdp.getMatrix()(2,2)=0.5;
	spdp.getMatrix()(2,3)=0.5;
	spdp*=shadowProjection;
	spdp*=depthProjection;
	GLfloat spdpMatrix[16];
	GLfloat* spdpPtr=spdpMatrix;
	for(int j=0;j<4;++j)
		for(int i=0;i<4;++i,++spdpPtr)
			*spdpPtr=GLfloat(spdp.getMatrix()(i,j));
	glUniformMatrix4fvARB(dataItem->shadowedIlluminatedHeightMapShaderUniforms[12],1,GL_FALSE,spdpMatrix);
	
	/* Draw the surface: */
	typedef GLGeometry::Vertex<void,0,void,0,void,float,3> Vertex;
	GLVertexArrayParts::enable(Vertex::getPartsMask());
	glVertexPointer(static_cast<const Vertex*>(0));
	for(unsigned int y=1;y<size[1];++y)
		glDrawElements(GL_QUAD_STRIP,size[0]*2,GL_UNSIGNED_INT,static_cast<const GLuint*>(0)+(y-1)*size[0]*2);
	GLVertexArrayParts::disable(Vertex::getPartsMask());
	
	/* Unbind all textures and buffers: */
	if(waterTable!=0)
		{
		glActiveTextureARB(GL_TEXTURE3_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glActiveTextureARB(GL_TEXTURE2_ARB);
	glBindTexture(GL_TEXTURE_1D,0);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
	if(!usePreboundDepthTexture)
		{
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
		}
	glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
	glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB,0);
	
	/* Unbind the shadowed illuminated height map shader: */
	glUseProgramObjectARB(0);
	}
