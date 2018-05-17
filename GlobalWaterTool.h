/***********************************************************************
GlobalWaterTool - Tool class to globally add or remove water from an
augmented reality sandbox.
Copyright (c) 2012-2018 Oliver Kreylos

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

#ifndef GLOBALWATERTOOL_INCLUDED
#define GLOBALWATERTOOL_INCLUDED

#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Vrui/Application.h>

/* Forward declarations: */
class Sandbox;
class GlobalWaterTool;
typedef Vrui::GenericToolFactory<GlobalWaterTool> GlobalWaterToolFactory;

class GlobalWaterTool:public Vrui::Tool,public Vrui::Application::Tool<Sandbox>
	{
	friend class Vrui::GenericToolFactory<GlobalWaterTool>;
	
	/* Elements: */
	private:
	static GlobalWaterToolFactory* factory; // Pointer to the factory object for this class
	float waterAmounts[2]; // Water amount added to the global water renderer when either tool button was pressed
	
	/* Constructors and destructors: */
	public:
	static GlobalWaterToolFactory* initClass(Vrui::ToolManager& toolManager);
	GlobalWaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
	virtual ~GlobalWaterTool(void);
	
	/* Methods from class Vrui::Tool: */
	virtual const Vrui::ToolFactory* getFactory(void) const;
	virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
	};

#endif
