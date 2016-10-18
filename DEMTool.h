/***********************************************************************
DEMTool - Tool class to load a digital elevation model into an augmented
reality sandbox to colorize the sand surface based on distance to the
DEM.
Copyright (c) 2013-2016 Oliver Kreylos

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

#ifndef DEMTOOL_INCLUDED
#define DEMTOOL_INCLUDED

#include <string>
#include <GL/gl.h>
#include <GL/GLObject.h>
#include <GLMotif/FileSelectionHelper.h>
#include <Vrui/Tool.h>
#include <Vrui/Application.h>

#include "Types.h"
#include "DEM.h"

/* Forward declarations: */
class Sandbox;
class DEMTool;

class DEMToolFactory:public Vrui::ToolFactory
	{
	friend class DEMTool;
	
	/* Elements: */
	private:
	GLMotif::FileSelectionHelper demSelectionHelper; // Helper object to load DEMs from files
	
	/* Constructors and destructors: */
	public:
	DEMToolFactory(Vrui::ToolManager& toolManager);
	virtual ~DEMToolFactory(void);
	
	/* Methods from Vrui::ToolFactory: */
	virtual const char* getName(void) const;
	virtual const char* getButtonFunction(int buttonSlotIndex) const;
	virtual Vrui::Tool* createTool(const Vrui::ToolInputAssignment& inputAssignment) const;
	virtual void destroyTool(Vrui::Tool* tool) const;
	};

class DEMTool:public DEM,public Vrui::Tool,public Vrui::Application::Tool<Sandbox>
	{
	friend class DEMToolFactory;
	
	/* Elements: */
	private:
	static DEMToolFactory* factory; // Pointer to the factory object for this class
	std::string demFileName; // Name of DEM file to load
	bool haveDemTransform; // Flag if the tool's configuration file section specified a DEM transformation
	OGTransform demTransform; // The transformation to apply to the DEM
	Scalar demVerticalShift; // Extra vertical shift to apply to DEM in sandbox coordinate units
	Scalar demVerticalScale; // The vertical exaggeration to apply to the DEM
	
	/* Private methods: */
	void loadDEMFile(const char* demFileName); // Loads a DEM from a file
	void loadDEMFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData); // Called when the user selects a DEM file to load
	
	/* Constructors and destructors: */
	public:
	static DEMToolFactory* initClass(Vrui::ToolManager& toolManager);
	DEMTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
	virtual ~DEMTool(void);
	
	/* Methods from class Vrui::Tool: */
	virtual void configure(const Misc::ConfigurationFileSection& configFileSection);
	virtual void initialize(void);
	virtual const Vrui::ToolFactory* getFactory(void) const;
	virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
	};

#endif
