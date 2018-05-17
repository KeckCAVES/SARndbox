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

#include "GlobalWaterTool.h"

#include <Vrui/ToolManager.h>

#include "WaterTable2.h"
#include "Sandbox.h"

/****************************************
Static elements of class GlobalWaterTool:
****************************************/

GlobalWaterToolFactory* GlobalWaterTool::factory=0;

/********************************
Methods of class GlobalWaterTool:
********************************/

GlobalWaterToolFactory* GlobalWaterTool::initClass(Vrui::ToolManager& toolManager)
	{
	/* Create the tool factory: */
	factory=new GlobalWaterToolFactory("GlobalWaterTool","Manage Water",0,toolManager);
	
	/* Set up the tool class' input layout: */
	factory->setNumButtons(2);
	factory->setButtonFunction(0,"Rain");
	factory->setButtonFunction(1,"Dry");
	
	/* Register and return the class: */
	toolManager.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
	return factory;
	}

GlobalWaterTool::GlobalWaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment)
	{
	}

GlobalWaterTool::~GlobalWaterTool(void)
	{
	}

const Vrui::ToolFactory* GlobalWaterTool::getFactory(void) const
	{
	return factory;
	}

void GlobalWaterTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	/* Calculate the amount of water to add/remove: */
	float waterAmount;
	if(cbData->newButtonState) // Button was just pressed
		{
		/* Add fixed amount of water/second: */
		waterAmount=application->waterSpeed>0.0?application->rainStrength/application->waterSpeed:0.0f;
		
		/* Invert water amount for drain button: */
		if(buttonSlotIndex==1)
			waterAmount=-waterAmount;
		
		/* Remember amount for button release event: */
		waterAmounts[buttonSlotIndex]=waterAmount;
		}
	else // Button was just released
		{
		/* Invert the water amount added when button was pressed: */
		waterAmount=-waterAmounts[buttonSlotIndex];
		}
	
	/* Add water amount to water table: */
	application->waterTable->setWaterDeposit(application->waterTable->getWaterDeposit()+waterAmount);
	}
