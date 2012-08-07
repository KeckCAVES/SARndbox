/***********************************************************************
SurfaceShadowedIlluminatedHeightMapShader - Shader to render an
illuminated surface with topographic contour lines and a height color
map.
Copyright (c) 2012 Oliver Kreylos

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

#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect depthSampler; // Sampler for the depth image-space elevation texture
uniform mat4 depthProjection; // Transformation from depth image space to camera space
uniform mat4 tangentDepthProjection; // Transformation from depth image space to camera space for tangent planes
uniform vec4 basePlane; // Plane equation of the base plane
uniform vec2 heightColorMapTransformation; // Transformation from elevation to height color map texture coordinate
uniform mat4 waterLevelTextureTransformation; // Transformation from camera space to water level texture coordinate space
uniform mat4 shadowProjection; // Transformation from camera space to shadow texture coordinate space

varying float heightColorMapTexCoord; // Texture coordinate for the height color map
varying vec2 waterLevelTexCoord; // Texture coordinate for water level texture
varying vec4 diffColor,specColor; // Diffuse and specular colors
varying vec4 vertexSc; // Vertex position in shadow texture coordinates

void main()
	{
	/* Get the vertex' depth image-space z coordinate from the texture: */
	vec4 vertexDic=gl_Vertex;
	vertexDic.z=texture2DRect(depthSampler,vertexDic.xy).r;
	
	/* Transform the vertex from depth image space to shadow texture coordinate space: */
	vertexSc=shadowProjection*vertexDic;
	
	/* Transform the vertex from depth image space to camera space: */
	vec4 vertexCc=depthProjection*vertexDic;
	
	/* Plug camera-space vertex into the base plane equation: */
	float elevation=dot(basePlane,vertexCc)/vertexCc.w;
	
	/* Transform elevation to height color map texture coordinate: */
	heightColorMapTexCoord=elevation*heightColorMapTransformation.x+heightColorMapTransformation.y;
	
	/* Transform the vertex from camera space to water level texture coordinate space: */
	vec4 wltc=waterLevelTextureTransformation*vertexCc;
	waterLevelTexCoord=wltc.xy/wltc.w;
	
	/* Calculate the vertex' tangent plane equation in depth image space: */
	vec4 tangentDic;
	tangentDic.x=texture2DRect(depthSampler,vec2(vertexDic.x+1.0,vertexDic.y)).r-texture2DRect(depthSampler,vec2(vertexDic.x-1.0,vertexDic.y)).r;
	tangentDic.y=texture2DRect(depthSampler,vec2(vertexDic.x,vertexDic.y+1.0)).r-texture2DRect(depthSampler,vec2(vertexDic.x,vertexDic.y-1.0)).r;
	tangentDic.z=-2.0;
	tangentDic.w=-dot(vertexDic.xyz,tangentDic.xyz)/vertexDic.w;
	
	/* Transform the vertex' tangent plane from depth image space to camera space: */
	vec3 normalCc=(tangentDepthProjection*tangentDic).xyz;
	
	/* Transform vertex and normal to eye coordinates for illumination: */
	vec4 vertexEc=gl_ModelViewMatrix*vertexCc;
	vec3 normalEc=normalize(gl_NormalMatrix*normalCc);
	
	/* Compute the light direction (works both for directional and point lights): */
	vec3 lightDirEc=gl_LightSource[0].position.xyz*vertexEc.w-vertexEc.xyz*gl_LightSource[0].position.w;
	float lightDistEc=length(lightDirEc);
	lightDirEc/=lightDistEc;
	
	/* Calculate ambient lighting term: */
	diffColor=gl_LightSource[0].ambient*gl_FrontMaterial.ambient;
	specColor=vec4(0.0,0.0,0.0,0.0);
	
	/* Compute the diffuse lighting angle: */
	float nl=dot(normalEc,lightDirEc);
	if(nl>0.0)
		{
		/* Calculate diffuse lighting term: */
		diffColor+=(gl_LightSource[0].diffuse*gl_FrontMaterial.diffuse)*nl;
		
		/* Compute the eye direction: */
		vec3 eyeDirEc=normalize(-vertexEc.xyz);
		
		/* Compute the specular lighting angle: */
		float nhv=max(dot(normalEc,normalize(eyeDirEc+lightDirEc)),0.0);
		
		/* Calculate per-source specular lighting term: */
		specColor+=(gl_LightSource[0].specular*gl_FrontMaterial.specular)*pow(nhv,gl_FrontMaterial.shininess);
		}
	
	/* Attenuate the per-source light terms: */
	float att=1.0/((gl_LightSource[0].quadraticAttenuation*lightDistEc+gl_LightSource[0].linearAttenuation)*lightDistEc+gl_LightSource[0].constantAttenuation);
	diffColor*=att;
	specColor*=att;
	
	/* Transform vertex to clip coordinates: */
	gl_Position=gl_ModelViewProjectionMatrix*vertexCc;
	}
