/***********************************************************************
DEMTool - Tool class to load a digital elevation model into an augmented
reality sandbox to colorize the sand surface based on distance to the
DEM.
Copyright (c) 2013-2015 Oliver Kreylos

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

#include "DEMTool.h"

#include <Misc/StandardValueCoders.h>
#include <Misc/ConfigurationFile.h>
#include <Geometry/GeometryValueCoders.h>
#include <Vrui/OpenFile.h>

#include "Sandbox.h"

/*******************************
Methods of class DEMToolFactory:
*******************************/

DEMToolFactory::DEMToolFactory(Vrui::ToolManager& toolManager)
	:ToolFactory("DEMTool",toolManager),
	 demSelectionHelper(Vrui::getWidgetManager(),"",".grid",Vrui::openDirectory("."))
	{
	/* Initialize tool layout: */
	layout.setNumButtons(1);
	
	#if 0
	/* Insert class into class hierarchy: */
	ToolFactory* toolFactory=toolManager.loadClass("Tool");
	toolFactory->addChildClass(this);
	addParentClass(toolFactory);
	#endif
	
	/* Set tool class' factory pointer: */
	DEMTool::factory=this;
	}

DEMToolFactory::~DEMToolFactory(void)
	{
	/* Reset tool class' factory pointer: */
	DEMTool::factory=0;
	}

const char* DEMToolFactory::getName(void) const
	{
	return "Show DEM";
	}

const char* DEMToolFactory::getButtonFunction(int) const
	{
	return "Toggle DEM";
	}

Vrui::Tool* DEMToolFactory::createTool(const Vrui::ToolInputAssignment& inputAssignment) const
	{
	return new DEMTool(this,inputAssignment);
	}

void DEMToolFactory::destroyTool(Vrui::Tool* tool) const
	{
	delete tool;
	}

/********************************
Static elements of class DEMTool:
********************************/

DEMToolFactory* DEMTool::factory=0;

/************************
Methods of class DEMTool:
************************/

void DEMTool::loadDEMFile(const char* demFileName)
	{
	/* Load the selected DEM file: */
	load(demFileName);
	
	OGTransform demT;
	if(haveDemTransform)
		demT=demTransform;
	else
		{
		/* Calculate an appropriate DEM transformation to fit the DEM into the sandbox's domain: */
		const Scalar* demBox=getDemBox();
		Scalar demSx=demBox[2]-demBox[0];
		Scalar demSy=demBox[3]-demBox[1];
		Scalar boxSx=application->bbox.getSize(0);
		Scalar boxSy=application->bbox.getSize(1);
		
		/* Shift the DEM's center to the box's center: */
		Point demCenter;
		demCenter[0]=Math::mid(demBox[0],demBox[2]);
		demCenter[1]=Math::mid(demBox[1],demBox[3]);
		demCenter[2]=Scalar(calcAverageElevation());
		demT=OGTransform::translateFromOriginTo(demCenter);
		
		/* Determine whether the DEM should be rotated: */
		Scalar scale=Math::min(demSx/boxSx,demSy/boxSy);
		Scalar scaleRot=Math::min(demSx/boxSy,demSy/boxSx);
		
		if(scale<scaleRot)
			{
			/* Scale and rotate DEM: */
			demT*=OGTransform::rotate(OGTransform::Rotation::rotateZ(Math::rad(Scalar(90))));
			scale=scaleRot;
			}
		
		/* Scale DEM without rotation: */
		demT*=OGTransform::scale(scale);
		}
	
	/* Shift the DEM vertically: */
	demT*=OGTransform::translate(Vector(0,0,demVerticalShift/demVerticalScale));
	
	/* Set the DEM transformation: */
	setTransform(demT*OGTransform(application->boxTransform),demVerticalScale,demT.getOrigin()[2]);
	}

void DEMTool::loadDEMFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData)
	{
	/* Load the selected DEM file: */
	loadDEMFile(cbData->selectedDirectory->getPath(cbData->selectedFileName).c_str());
	}

DEMToolFactory* DEMTool::initClass(Vrui::ToolManager& toolManager)
	{
	/* Create the tool factory: */
	factory=new DEMToolFactory(toolManager);
	
	/* Register and return the class: */
	toolManager.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
	return factory;
	}

DEMTool::DEMTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment),
	 haveDemTransform(false),demTransform(OGTransform::identity),
	 demVerticalShift(0),demVerticalScale(1)
	{
	}

DEMTool::~DEMTool(void)
	{
	}

void DEMTool::configure(const Misc::ConfigurationFileSection& configFileSection)
	{
	/* Query DEM file name: */
	demFileName=configFileSection.retrieveString("./demFileName",demFileName);
	
	/* Read the DEM transformation: */
	if(configFileSection.hasTag("./demTransform"))
		{
		haveDemTransform=true;
		demTransform=configFileSection.retrieveValue<OGTransform>("./demTransform",demTransform);
		}
	
	demVerticalShift=configFileSection.retrieveValue<Scalar>("./demVerticalShift",demVerticalShift);
	demVerticalScale=configFileSection.retrieveValue<Scalar>("./demVerticalScale",demVerticalScale);
	}

void DEMTool::initialize(void)
	{
	/* Bring up a file selection dialog if there is no pre-configured DEM file: */
	if(demFileName.empty())
		{
		/* Load a DEM file: */
		factory->demSelectionHelper.loadFile("Load DEM File...",this,&DEMTool::loadDEMFileCallback);
		}
	else
		{
		/* Load the configured DEM file: */
		loadDEMFile(demFileName.c_str());
		}
	}

const Vrui::ToolFactory* DEMTool::getFactory(void) const
	{
	return factory;
	}

void DEMTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	if(cbData->newButtonState)
		{
		/* Toggle this DEM tool as the active one: */
		application->toggleDEM(this);
		}
	}
