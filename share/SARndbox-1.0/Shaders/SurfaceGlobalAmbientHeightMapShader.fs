/***********************************************************************
SurfaceGlobalAmbientHeightMapShader - Shader to render the global
ambient component of a surface with topographic contour lines and a
height color map.
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

uniform sampler2DRect pixelCornerElevationSampler;
uniform float contourLineFactor;
uniform sampler1D heightColorMapSampler;
uniform sampler2DRect waterLevelSampler;
uniform float waterOpacity;

varying float heightColorMapTexCoord; // Texture coordinate for the height color map
varying vec2 waterLevelTexCoord; // Texture coordinate for water level texture
varying vec4 diffColor; // Diffuse color

void main(void)
	{
	/* Calculate the pixel quad's elevation extent by looking up in the elevation texture: */
	float corner0=texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y)).r;
	float corner1=texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).r;
	float corner2=texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).r;
	float corner3=texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y+1.0)).r;
	float min1=min(corner0,corner1);
	float min2=min(corner2,corner3);
	float minEl=min(min1,min2);
	float max1=max(corner0,corner1);
	float max2=max(corner2,corner3);
	float maxEl=max(max1,max2);
	
	/* Check if the elevation extent crosses a topographic contour boundary: */
	vec4 baseColor;
	if(floor(maxEl*contourLineFactor)!=floor(minEl*contourLineFactor))
		baseColor=vec4(0.0,0.0,0.0,1.0);
	else
		{
		/* Get the fragment's color from the height color map: */
		baseColor=texture1D(heightColorMapSampler,heightColorMapTexCoord);
		}
	
	/* Modulate the base color with the illuminated color: */
	baseColor=baseColor*diffColor;
	
	/* Check if the surface is under water: */
	float waterLevel=texture2DRect(waterLevelSampler,waterLevelTexCoord).r;
	float mixFactor=clamp(waterLevel*waterOpacity,0.0,1.0);
	baseColor=mix(baseColor,vec4(0.0,0.0,1.0,1.0),mixFactor);
	
	gl_FragColor=baseColor;
	}
