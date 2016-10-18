/***********************************************************************
LocalWaterTool - Tool class to locally add or remove water from an
augmented reality sandbox.
Copyright (c) 2012-2013 Oliver Kreylos

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

#ifndef LOCALWATERTOOL_INCLUDED
#define LOCALWATERTOOL_INCLUDED

#include <GL/gl.h>
#include <GL/GLObject.h>
#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Vrui/TransparentObject.h>
#include <Vrui/Application.h>

/* Forward declarations: */
namespace Misc {
template <class ParameterParam>
class FunctionCall;
}
class GLContextData;
typedef Misc::FunctionCall<GLContextData&> AddWaterFunction;
class Sandbox;
class LocalWaterTool;
typedef Vrui::GenericToolFactory<LocalWaterTool> LocalWaterToolFactory;

class LocalWaterTool:public Vrui::Tool,public Vrui::Application::Tool<Sandbox>,public GLObject,public Vrui::TransparentObject
	{
	friend class Vrui::GenericToolFactory<LocalWaterTool>;
	
	/* Elements: */
	private:
	static LocalWaterToolFactory* factory; // Pointer to the factory object for this class
	
	const AddWaterFunction* addWaterFunction; // Render function registered with the water table
	GLfloat adding; // Amount of data added or removed from the water table
	
	/* Constructors and destructors: */
	public:
	static LocalWaterToolFactory* initClass(Vrui::ToolManager& toolManager);
	LocalWaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
	virtual ~LocalWaterTool(void);
	
	/* Methods from class Vrui::Tool: */
	virtual void initialize(void);
	virtual void deinitialize(void);
	virtual const Vrui::ToolFactory* getFactory(void) const;
	virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
	
	/* Methods from class GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* Methods from class Vrui::TransparentObject: */
	virtual void glRenderActionTransparent(GLContextData& contextData) const;
	
	/* New methods: */
	void addWater(GLContextData& contextData) const; // Function to render geometry that adds water to the water table
	};

#endif
