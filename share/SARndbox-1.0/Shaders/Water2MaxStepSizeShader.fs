/***********************************************************************
Water2MaxStepSizeShader - Shader to compute the maximum step size for a
subsequent Runge-Kutta integration step by reducing the maximum step
size texture.
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

uniform vec2 fullTextureSize;
uniform sampler2DRect maxStepSizeSampler;

void main()
	{
	/* Calculate the base position of a 2x2 tile of pixels: */
	vec2 frag=gl_FragCoord.xy*2.0-vec2(0.5,0.5);
	
	/* Accumulate the minimum value of the 2x2 tile: */
	float maxStepSize=texture2DRect(maxStepSizeSampler,frag).r;
	if(frag.x<fullTextureSize.x)
		maxStepSize=min(maxStepSize,texture2DRect(maxStepSizeSampler,vec2(frag.x+1.0,frag.y)).r);
	if(frag.y<fullTextureSize.y)
		maxStepSize=min(maxStepSize,texture2DRect(maxStepSizeSampler,vec2(frag.x,frag.y+1.0)).r);
	if(frag.x<fullTextureSize.x&&frag.y<fullTextureSize.y)
		maxStepSize=min(maxStepSize,texture2DRect(maxStepSizeSampler,vec2(frag.x+1.0,frag.y+1.0)).r);
	
	/* Assign the maximum of the 2x2 tile to the result frame buffer: */
	gl_FragData[0]=vec4(maxStepSize,0.0,0.0,0.0);
	}
