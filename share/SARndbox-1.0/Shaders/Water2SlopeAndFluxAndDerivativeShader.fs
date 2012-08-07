/***********************************************************************
Water2SlopeAndFluxAndDerivativeShader - Shader to compute the temporal
derivative of the conserved quantities directly from spatial partial
derivatives, bypassing the separate partial flux computation.
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
uniform float g;
uniform float epsilon;
uniform sampler2DRect bathymetrySampler;
uniform sampler2DRect quantitySampler;

vec3 calcSlope(in vec3 q0,in vec3 q1,in vec3 q2,in float cellSize,in float b0,in float b1)
	{
	/* Calculate the left, central, and right differences: */
	vec3 d01=(q1-q0)*(theta/cellSize);
	vec3 d02=(q2-q0)/(2.0*cellSize);
	vec3 d12=(q2-q1)*(theta/cellSize);
	
	/* Calculate the component-wise intervals: */
	vec3 dMin=min(min(d01,d02),d12);
	vec3 dMax=max(max(d01,d02),d12);
	
	/* Calculate the minmod-limited slope: */
	vec3 slope;
	slope.x=dMin.x>0.0?dMin.x:dMax.x<0.0?dMax.x:0.0;
	slope.y=dMin.y>0.0?dMin.y:dMax.y<0.0?dMax.y:0.0;
	slope.z=dMin.z>0.0?dMin.z:dMax.z<0.0?dMax.z:0.0;
	
	/* Check the calculated slope against the left and right face-centered bathymetry values: */
	if(q1.x-slope.x*cellSize*0.5<b0)
		slope.x=(q1.x-b0)/(cellSize*0.5);
	if(q1.x+slope.x*cellSize*0.5<b1)
		slope.x=(b1-q1.x)/(cellSize*0.5);
	
	/* Return the adjusted slope: */
	return slope;
	}

vec2 calcUv(inout vec3 q,in float h)
	{
	/* Calculate velocity using a desingularizing division operator: */
	float h4=h*h*h*h;
	vec2 uv=q.yz*(1.41421356237309*h/sqrt(h4+max(h4,epsilon)));
	
	/* Recalculate discharge based on desingularized velocity: */
	q.yz=uv*h;
	
	return uv;
	}

float calcPartialFluxX(in vec3 qe,in vec3 qw,in float bew,out vec3 fluxX)
	{
	/* Calculate one-sided water column heights: */
	float he=max(qe.x-bew,0.0);
	float hw=max(qw.x-bew,0.0);
	
	/* Calculate one-sided velocities: */
	vec2 uve=calcUv(qe,he);
	vec2 uvw=calcUv(qw,hw);
	
	/* Calculate one-sided x-direction flux quadratures: */
	vec3 fe=vec3(qe.y,uve.x*qe.y+0.5*g*he*he,uve.y*qe.y);
	vec3 fw=vec3(qw.y,uvw.x*qw.y+0.5*g*hw*hw,uvw.y*qw.y);
	
	/* Calculate one-sided local speeds of propagation: */
	float sghe=sqrt(g*he);
	float sghw=sqrt(g*hw);
	float ae=min(min(uve.x-sghe,uvw.x-sghw),0.0);
	float aw=max(max(uve.x+sghe,uvw.x+sghw),0.0);
	
	/* Calculate complete x-direction flux: */
	fluxX=aw-ae!=0.0?((fe*aw-fw*ae)+(qw-qe)*(aw*ae))/(aw-ae):vec3(0.0);
	
	/* Return maximum possible step size: */
	return 0.5*cellSize.x/max(-ae,aw);
	}

float calcPartialFluxY(in vec3 qn,in vec3 qs,in float bns,out vec3 fluxY)
	{
	/* Calculate one-sided water column heights: */
	float hn=max(qn.x-bns,0.0);
	float hs=max(qs.x-bns,0.0);
	
	/* Calculate one-sided velocities: */
	vec2 uvn=calcUv(qn,hn);
	vec2 uvs=calcUv(qs,hs);
	
	/* Calculate one-sided y-direction flux quadratures: */
	vec3 fn=vec3(qn.z,uvn.x*qn.z,uvn.y*qn.z+0.5*g*hn*hn);
	vec3 fs=vec3(qs.z,uvs.x*qs.z,uvs.y*qs.z+0.5*g*hs*hs);
	
	/* Calculate one-sided local speeds of propagation: */
	float sghn=sqrt(g*hn);
	float sghs=sqrt(g*hs);
	float an=min(min(uvn.y-sghn,uvs.y-sghs),0.0);
	float as=max(max(uvn.y+sghn,uvs.y+sghs),0.0);
	
	/* Calculate complete y-direction flux: */
	fluxY=as-an!=0.0?((fn*as-fs*an)+(qs-qn)*(as*an))/(as-an):vec3(0.0);
	
	/* Return maximum possible step size: */
	return 0.5*cellSize.y/max(-an,as);
	}

void main()
	{
	/* Calculate face-centered bathymetry elevations required for partial flux computations: */
	float b00=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r;
	float b10=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r;
	float b01=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r;
	float b11=texture2DRect(bathymetrySampler,gl_FragCoord.xy).r;
	float b0=(texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-2.0)).r+texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-2.0)).r)*0.5;
	float b1=(b00+b10)*0.5;
	float b2=(texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-2.0,gl_FragCoord.y-1.0)).r+texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-2.0,gl_FragCoord.y)).r)*0.5;
	float b3=(b00+b01)*0.5;
	float b4=(b10+b11)*0.5;
	float b5=(texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y-1.0)).r+texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).r)*0.5;
	float b6=(b01+b11)*0.5;
	float b7=(texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y+1.0)).r+texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).r)*0.5;
	
	/* Get quantities required for partial flux computations: */
	vec3 q1=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).rgb;
	vec3 q3=texture2DRect(quantitySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).rgb;
	vec3 q4=texture2DRect(quantitySampler,gl_FragCoord.xy).rgb;
	vec3 q5=texture2DRect(quantitySampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).rgb;
	vec3 q7=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).rgb;
	
	/* Calculate one-sided quantities required for partial flux computations: */
	vec3 q1n=q1+calcSlope(texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-2.0)).rgb,q1,q4,cellSize.y,b0,b1)*(cellSize.y*0.5);
	vec3 q3e=q3+calcSlope(texture2DRect(quantitySampler,vec2(gl_FragCoord.x-2.0,gl_FragCoord.y)).rgb,q3,q4,cellSize.x,b2,b3)*(cellSize.x*0.5);
	vec3 q4x=calcSlope(q3,q4,q5,cellSize.x,b3,b4)*(cellSize.x*0.5);
	vec3 q4w=q4-q4x;
	vec3 q4e=q4+q4x;
	vec3 q4y=calcSlope(q1,q4,q7,cellSize.y,b1,b6)*(cellSize.y*0.5);
	vec3 q4s=q4-q4y;
	vec3 q4n=q4+q4y;
	vec3 q5w=q5-calcSlope(q4,q5,texture2DRect(quantitySampler,vec2(gl_FragCoord.x+2.0,gl_FragCoord.y)).rgb,cellSize.x,b4,b5)*(cellSize.x*0.5);
	vec3 q7s=q7-calcSlope(q4,q7,texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y+2.0)).rgb,cellSize.y,b6,b7)*(cellSize.y*0.5);
	
	/* Calculate partial fluxes across the cell's faces and the maximum possible step size for this cell: */
	vec3 fluxXw,fluxXe,fluxYs,fluxYn;
	gl_FragData[1]=vec4(min(min(calcPartialFluxX(q3e,q4w,b3,fluxXw),
	                           calcPartialFluxX(q4e,q5w,b4,fluxXe)),
	                       min(calcPartialFluxY(q1n,q4s,b1,fluxYs),
	                           calcPartialFluxY(q4n,q7s,b6,fluxYn))),
	                    0.0,0.0,0.0);
	
	/* Calculate the water column height at the cell center: */
	float h=max(q4.x-(b3+b4)*0.5,0.0);
	
	/* Calculate equation source terms at the cell center: */
	vec3 source=vec3(0.0,-g*h*(b4-b3)/cellSize.x,-g*h*(b6-b1)/cellSize.y);
	
	/* Calculate the temporal derivative: */
	gl_FragData[0]=vec4(source-(fluxXe-fluxXw)/cellSize.x-(fluxYn-fluxYs)/cellSize.y,0.0);
	}
