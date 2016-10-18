/***********************************************************************
WaterRenderingShader - Shader to render the water level surface of a
water table.
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

#extension GL_ARB_texture_rectangle : enable

uniform sampler2DRect quantitySampler; // Sampler for the quantity (water level + momentum) texture
uniform sampler2DRect bathymetrySampler; // Sampler for the bathymetry texture
uniform mat4 modelviewGridMatrix; // Vertex transformation from grid space to eye space
uniform mat4 tangentModelviewGridMatrix; // Tangend plane transformation from grid space to eye space
uniform mat4 projectionModelviewGridMatrix; // Vertex transformation from grid space to clip space

varying vec4 color; // Color value for Goraud shading

void accumulateLight0(in vec4 vertexEc,in vec3 normalEc,in vec4 ambient,in vec4 diffuse,in vec4 specular,in float shininess,inout vec4 ambientDiffuseAccum,inout vec4 specularAccum)
	{
	/* Compute the light direction (works both for directional and point lights): */
	vec3 lightDirEc=gl_LightSource[0].position.xyz*vertexEc.w-vertexEc.xyz*gl_LightSource[0].position.w;
	float lightDist=length(lightDirEc);
	lightDirEc=normalize(lightDirEc);
	
	/* Calculate the spot light angle: */
	float sl=-dot(lightDirEc,normalize(gl_LightSource[0].spotDirection));
	
	/* Check if the point is inside the spot light's cone: */
	if(sl>=gl_LightSource[0].spotCosCutoff)
		{
		/* Calculate the source attenuation factor: */
		float att=1.0/((gl_LightSource[0].quadraticAttenuation*lightDist+gl_LightSource[0].linearAttenuation)*lightDist+gl_LightSource[0].constantAttenuation);
		
		/* Calculate the spot light attenuation factor: */
		att*=pow(sl,gl_LightSource[0].spotExponent);
		
		/* Calculate per-source ambient light term: */
		ambientDiffuseAccum+=(gl_LightSource[0].ambient*ambient)*att;
		
		/* Compute the diffuse lighting angle: */
		float nl=dot(normalEc,lightDirEc);
		if(nl>0.0)
			{
			/* Calculate per-source diffuse light term: */
			ambientDiffuseAccum+=(gl_LightSource[0].diffuse*diffuse)*(nl*att);
			
			/* Compute the eye direction: */
			vec3 eyeDirEc=normalize(-vertexEc.xyz);
			
			/* Compute the specular lighting angle: */
			float nhv=max(dot(normalEc,normalize(eyeDirEc+lightDirEc)),0.0);
			
			/* Calculate per-source specular lighting term: */
			specularAccum+=(gl_LightSource[0].specular*specular)*(pow(nhv,shininess)*att);
			}
		}
	}

void main()
	{
	/* Get the vertex' grid-space z coordinate from the quantity texture: */
	vec4 vertexGc=gl_Vertex;
	vertexGc.z=texture2DRect(quantitySampler,vertexGc.xy).r;
	
	/* Get the bathymetry elevation at the same location: */
	float bathy=(texture2DRect(bathymetrySampler,vertexGc.xy-vec2(1.0,1.0)).r
	            +texture2DRect(bathymetrySampler,vertexGc.xy-vec2(1.0,0.0)).r
	            +texture2DRect(bathymetrySampler,vertexGc.xy-vec2(0.0,1.0)).r
	            +texture2DRect(bathymetrySampler,vertexGc.xy-vec2(0.0,0.0)).r)*0.25;
	
	/* Calculate the vertex' grid-space tangent plane equation: */
	vec4 tangentGc;
	tangentGc.x=texture2DRect(quantitySampler,vec2(vertexGc.x-1.0,vertexGc.y)).r-texture2DRect(quantitySampler,vec2(vertexGc.x+1.0,vertexGc.y)).r;
	tangentGc.y=texture2DRect(quantitySampler,vec2(vertexGc.x,vertexGc.y-1.0)).r-texture2DRect(quantitySampler,vec2(vertexGc.x,vertexGc.y+1.0)).r;
	tangentGc.z=2.0;
	tangentGc.w=-dot(vertexGc.xyz,tangentGc.xyz)/vertexGc.w;
	
	/* Transform the vertex and its tangent plane from grid space to eye space for illumination: */
	vec4 vertexEc=modelviewGridMatrix*vertexGc;
	vec3 normalEc=normalize((tangentModelviewGridMatrix*tangentGc).xyz);
	
	/* Initialize the vertex color accumulators: */
	vec4 diffColor=gl_LightModel.ambient*gl_FrontMaterial.ambient;
	vec4 specColor=vec4(0.0,0.0,0.0,0.0);
	
	/* Call the light accumulation functions for all enabled light sources: */
	accumulateLight0(vertexEc,normalEc,gl_FrontMaterial.ambient,gl_FrontMaterial.diffuse,gl_FrontMaterial.specular,gl_FrontMaterial.shininess,diffColor,specColor);
	
	/* Assign the interpolated vertex color: */
	color=diffColor+specColor;
	color.a=vertexGc.z-bathy;
	
	/* Transform vertex directly from grid space to clip space: */
	gl_Position=projectionModelviewGridMatrix*vertexGc;
	}
