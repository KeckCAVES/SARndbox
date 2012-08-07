/***********************************************************************
Water2BathymetryUpdateShader - Shader to adjust the water surface height
after a change to the bathymetry.
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

uniform sampler2DRect oldBathymetrySampler;
uniform sampler2DRect newBathymetrySampler;
uniform sampler2DRect quantitySampler;

void main()
	{
	/* Calculate the old and new bathymetry elevations at the center of this cell: */
	float bOld=(texture2DRect(oldBathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r+
	            texture2DRect(oldBathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r+
	            texture2DRect(oldBathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r+
	            texture2DRect(oldBathymetrySampler,vec2(gl_FragCoord.xy)).r)*0.25;
	float bNew=(texture2DRect(newBathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r+
	            texture2DRect(newBathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r+
	            texture2DRect(newBathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r+
	            texture2DRect(newBathymetrySampler,vec2(gl_FragCoord.xy)).r)*0.25;
	
	/* Get the old quantity at the cell center: */
	vec3 q=texture2DRect(quantitySampler,gl_FragCoord.xy).rgb;
	
	/* Update the water surface height: */
	gl_FragColor=vec4(max(q.x-bOld,0.0)+bNew,q.yz,0.0);
	}
