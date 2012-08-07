/***********************************************************************
SurfaceAddContourLines - Shader fragment to add topographic contour
lines extracted from a half-pixel offset 2D elevation map to a surface's
base color.
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

void addContourLines(in vec2 fragCoord,inout vec4 baseColor)
	{
	#if 0
	
	/*********************************************************************
	Simple algorithm: draws contour line if the area of a pixel crosses at
	least one contour line. Always draws 4-connected lines.
	*********************************************************************/
	
	/* Calculate the elevation of each pixel corner by evaluating the half-pixel offset elevation texture: */
	float corner0=texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x,fragCoord.y)).r;
	float corner1=texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x+1.0,fragCoord.y)).r;
	float corner2=texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x,fragCoord.y+1.0)).r;
	float corner3=texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x+1.0,fragCoord.y+1.0)).r;
	
	/* Calculate the elevation range of the pixel's area: */
	float elMin=min(min(corner0,corner1),min(corner2,corner3));
	float elMax=max(max(corner0,corner1),max(corner2,corner3));
	
	/* Check if the pixel's area crosses at least one contour line: */
	if(floor(elMin*contourLineFactor)!=floor(elMax*contourLineFactor))
		{
		/* Topographic contour lines are rendered in black: */
		baseColor=vec4(0.0,0.0,0.0,1.0);
		}
	
	#else
	
	/*********************************************************************
	More complicated algorithm: draws thinnest possible contour lines by
	removing redundant 4-connected pixels.
	*********************************************************************/
	
	/* Calculate the contour line interval containing each pixel corner by evaluating the half-pixel offset elevation texture: */
	float corner0=floor(texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x,fragCoord.y)).r*contourLineFactor);
	float corner1=floor(texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x+1.0,fragCoord.y)).r*contourLineFactor);
	float corner2=floor(texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x,fragCoord.y+1.0)).r*contourLineFactor);
	float corner3=floor(texture2DRect(pixelCornerElevationSampler,vec2(fragCoord.x+1.0,fragCoord.y+1.0)).r*contourLineFactor);
	
	/* Find all pixel edges that cross at least one contour line: */
	int edgeMask=0;
	int numEdges=0;
	if(corner0!=corner1)
		{
		edgeMask+=1;
		++numEdges;
		}
	if(corner2!=corner3)
		{
		edgeMask+=2;
		++numEdges;
		}
	if(corner0!=corner2)
		{
		edgeMask+=4;
		++numEdges;
		}
	if(corner1!=corner3)
		{
		edgeMask+=8;
		++numEdges;
		}
	
	/* Check for all cases in which the pixel should be colored as a topographic contour line: */
	if(numEdges>2||edgeMask==3||edgeMask==12||(numEdges==2&&mod(floor(fragCoord.x)+floor(fragCoord.y),2.0)==0.0))
		{
		/* Topographic contour lines are rendered in black: */
		baseColor=vec4(0.0,0.0,0.0,1.0);
		}
	
	#endif
	}
