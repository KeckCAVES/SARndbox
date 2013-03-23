/***********************************************************************
Water2FluxAndDerivativeShader - Shader to compute the temporal
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
uniform float g;
uniform float epsilon;
uniform sampler2DRect bathymetrySampler;
uniform sampler2DRect quantitySampler;
uniform sampler2DRect slopeXSampler;
uniform sampler2DRect slopeYSampler;

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
	float hn=max(qn.x-bns);
	float hs=max(qs.x-bns);
	
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
	/* Calculate one-sided quantities required for partial flux computations: */
	vec3 q0n=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).rgb+texture2DRect(slopeYSampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).rgb*(cellSize.y*0.5);
	vec3 q1e=texture2DRect(quantitySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).rgb+texture2DRect(slopeXSampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).rgb*(cellSize.x*0.5);
	vec3 q2=texture2DRect(quantitySampler,gl_FragCoord.xy).rgb;
	vec3 qx2=texture2DRect(slopeXSampler,gl_FragCoord.xy).rgb*(cellSize.x*0.5);
	vec3 q2w=q2-qx2;
	vec3 q2e=q2+qx2;
	vec3 qy2=texture2DRect(slopeYSampler,gl_FragCoord.xy).rgb*(cellSize.y*0.5);
	vec3 q2s=q2-qy2;
	vec3 q2n=q2+qy2;
	vec3 q3w=texture2DRect(quantitySampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).rgb-texture2DRect(slopeXSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).rgb*(cellSize.x*0.5);
	vec3 q4s=texture2DRect(quantitySampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).rgb-texture2DRect(slopeYSampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).rgb*(cellSize.y*0.5);
	
	/* Get the bathymetry elevation at the cell's corners: */
	float b0=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y-1.0)).r;
	float b1=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x,gl_FragCoord.y-1.0)).r;
	float b2=texture2DRect(bathymetrySampler,vec2(gl_FragCoord.x-1.0,gl_FragCoord.y)).r;
	float b3=texture2DRect(bathymetrySampler,gl_FragCoord.xy).r;
	
	/* Calculate the bathymetry elevation at the cell's face centers: */
	float bw=(b0+b2)*0.5;
	float be=(b1+b3)*0.5;
	float bs=(b0+b1)*0.5;
	float bn=(b2+b3)*0.5;
	
	/* Calculate partial fluxes across the cell's faces and the maximum possible step size for this cell: */
	vec3 fluxXw,fluxXe,fluxYs,fluxYn;
	gl_FragData[1]=min(min(calcPartialFluxX(q1e,q2w,bw,fluxXw),
	                       calcPartialFluxX(q2e,q3w,be,fluxXe)),
	                   min(calcPartialFluxY(q0n,q2s,bs,fluxYs),
	                       calcPartialFluxY(q2n,q4s,bn,fluxYn)));
	
	/* Calculate the water column height at the cell center: */
	float bc=(bw+be)*0.5;
	float h=max(texture2DRect(quantitySampler,gl_FragCoord.xy).r-bc,0.0);
	
	/* Calculate equation source terms at the cell center: */
	vec3 source=vec3(0.0,-g*h*(be-bw)/cellSize.x,-g*h*(bn-bs)/cellSize.y);
	
	/* Calculate the temporal derivative: */
	gl_FragData[0]=vec4(source-(fluxXe-fluxXw)/cellSize.x-(fluxYn-fluxYs)/cellSize.y,0.0);
	}
