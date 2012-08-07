/***********************************************************************
Water2WaterUpdateShader - Shader to adjust the water surface height
based on the additive water texture.
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

uniform sampler2DRect bathymetrySampler;
uniform sampler2DRect quantitySampler;
uniform sampler2DRect waterSampler;

void main()
	{
	/* Calculate the bathymetry elevation at the center of this cell: */
	float b=(texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r+
	         texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r+
	         texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r+
	         texture2DRect(bathymetrySampler,vec2(gl_FragCoord.xy)).r)*0.25;
	
	/* Get the old quantity at the cell center: */
	vec3 q=texture2DRect(quantitySampler,gl_FragCoord.xy).rgb;
	
	/* Calculate the old and new water column heights: */
	float hOld=q.x-b;
	float hNew=max(hOld+texture2DRect(waterSampler,gl_FragCoord.xy).r,0.0);
	
	/* Update the water surface height: */
	q.x=hNew+b;
	
	/* Update the partial discharges: */
	q.yz=hNew==0.0?vec2(0.0,0.0):(hNew<hOld?q.yz*(hNew/hOld):q.yz); // New water is added with zero velocity; water is removed at current velocity
	
	/* Write the updated quantity: */
	gl_FragColor=vec4(q,0.0);
	}
