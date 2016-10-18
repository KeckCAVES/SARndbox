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

#include "ShaderHelper.h"

#include <string>
#include <GL/gl.h>
#include <GL/Extensions/GLARBFragmentShader.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/Extensions/GLARBVertexShader.h>

#include "Config.h"

GLhandleARB compileVertexShader(const char* vertexShaderFileName)
	{
	/* Construct the full shader source file name: */
	std::string fullShaderFileName=CONFIG_SHADERDIR;
	fullShaderFileName.push_back('/');
	fullShaderFileName.append(vertexShaderFileName);
	fullShaderFileName.append(".vs");
	
	/* Compile and return the vertex shader: */
	return glCompileVertexShaderFromFile(fullShaderFileName.c_str());
	}

GLhandleARB compileFragmentShader(const char* fragmentShaderFileName)
	{
	/* Construct the full shader source file name: */
	std::string fullShaderFileName=CONFIG_SHADERDIR;
	fullShaderFileName.push_back('/');
	fullShaderFileName.append(fragmentShaderFileName);
	fullShaderFileName.append(".fs");
	
	/* Compile and return the fragment shader: */
	return glCompileFragmentShaderFromFile(fullShaderFileName.c_str());
	}

GLhandleARB linkVertexAndFragmentShader(const char* shaderFileName)
	{
	/* Compile the vertex and fragment shaders: */
	GLhandleARB vertexShader=compileVertexShader(shaderFileName);
	GLhandleARB fragmentShader=compileFragmentShader(shaderFileName);
	
	/* Link the shader program: */
	GLhandleARB shaderProgram=glLinkShader(vertexShader,fragmentShader);
	
	/* Release the compiled shaders (won't get deleted until shader program is released): */
	glDeleteObjectARB(vertexShader);
	glDeleteObjectARB(fragmentShader);
	
	return shaderProgram;
	}
