/***********************************************************************
ShaderHelper - Helper functions to create GLSL shaders from text files.
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

#ifndef SHADERHELPER_INCLUDED
#define SHADERHELPER_INCLUDED

#include <GL/gl.h>
#include <GL/Extensions/GLARBShaderObjects.h>

GLhandleARB compileVertexShader(const char* vertexShaderFileName); // Returns a handle to a vertex shader compiled from the given source file in the SARndbox's shader directory
GLhandleARB compileFragmentShader(const char* fragmentShaderFileName); // Returns a handle to a fragment shader compiled from the given source file in the SARndbox's shader directory
GLhandleARB linkVertexAndFragmentShader(const char* shaderFileName); // Returns a handle to a shader program linked from a vertex shader and a fragment shader compiled from the given source files in the SARndbox's shader directory

#endif
