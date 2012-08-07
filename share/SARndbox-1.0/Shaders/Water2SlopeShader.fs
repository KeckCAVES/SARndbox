/***********************************************************************
Water2SlopeShader - Shader to compute spatial partial derivatives of the
conserved quantities.
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
#extension GL_ARB_draw_buffers : enable

uniform vec2 cellSize;
uniform float theta;
uniform sampler2DRect bathymetrySampler;
uniform sampler2DRect quantitySampler;

vec3 minModSlope(in vec3 q0,in vec3 q1,in vec3 q2,in float cellSize)
	{
	/* Calculate the left, central, and right differences: */
	vec3 d01=(q1-q0)*(theta/cellSize);
	vec3 d02=(q2-q0)/(2.0*cellSize);
	vec3 d12=(q2-q1)*(theta/cellSize);
	
	/* Calculate the component-wise intervals: */
	vec3 dMin=min(min(d01,d02),d12);
	vec3 dMax=max(max(d01,d02),d12);
	
	/* Assemble the result vector: */
	vec3 result;
	result.x=dMin.x>0.0?dMin.x:dMax.x<0.0?dMax.x:0.0;
	result.y=dMin.y>0.0?dMin.y:dMax.y<0.0?dMax.y:0.0;
	result.z=dMin.z>0.0?dMin.z:dMax.z<0.0?dMax.z:0.0;
	
	return result;
	}

void main()
	{
	/* Get quantities at centers of this and neighboring cells: */
	vec3 q0=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).rgb;
	vec3 q1=texture2DRect(quantitySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).rgb;
	vec3 q2=texture2DRect(quantitySampler,gl_FragCoord.xy).rgb;
	vec3 q3=texture2DRect(quantitySampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).rgb;
	vec3 q4=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).rgb;
	
	/* Get the bathymetry elevation at the cell's corners: */
	float b0=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r;
	float b1=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r;
	float b2=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r;
	float b3=texture2DRect(bathymetrySampler,gl_FragCoord.xy).r;
	
	/* Calculate the x-direction slope: */
	vec3 slopeX=minModSlope(q1,q2,q3,cellSize.x);
	
	/* Check the calculated slope against the western and eastern face-centered bathymetry values: */
	float bw=(b0+b2)*0.5;
	if(q2.x-slopeX.x*cellSize.x*0.5<bw)
		slopeX.x=(q2.x-bw)/(cellSize.x*0.5);
	float be=(b1+b3)*0.5;
	if(q2.x+slopeX.x*cellSize.x*0.5<be)
		slopeX.x=(be-q2.x)/(cellSize.x*0.5);
	
	/* Store the adjusted x-direction slope in the result grid: */
	gl_FragData[0]=vec4(slopeX,0.0);
	
	/* Calculate the y-direction slope: */
	vec3 slopeY=minModSlope(q0,q2,q4,cellSize.y);
	
	/* Check the calculated slope against the southern and northern face-centered bathymetry values: */
	float bs=(b0+b1)*0.5;
	if(q2.x-slopeY.x*cellSize.y*0.5<bs)
		slopeY.x=(q2.x-bs)/(cellSize.y*0.5);
	float bn=(b2+b3)*0.5;
	if(q2.x+slopeY.x*cellSize.y*0.5<bn)
		slopeY.x=(bn-q2.x)/(cellSize.y*0.5);
	
	/* Store the adjusted y-direction slope in the result grid: */
	gl_FragData[1]=vec4(slopeY,0.0);
	}
