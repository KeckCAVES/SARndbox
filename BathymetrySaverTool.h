/***********************************************************************
BathymetrySaverTool - Tool to save the current bathymetry grid of an
augmented reality sandbox to a file or network socket.
Copyright (c) 2016 Oliver Kreylos

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

#ifndef BATHYMETRYSAVERTOOL_INCLUDED
#define BATHYMETRYSAVERTOOL_INCLUDED

#include <string>
#include <GL/gl.h>
#include <Vrui/Tool.h>
#include <Vrui/Application.h>

#include "Types.h"

/* Forward declarations: */
class WaterTable2;
class Sandbox;
class BathymetrySaverTool;

class BathymetrySaverToolFactory:public Vrui::ToolFactory
	{
	friend class BathymetrySaverTool;
	
	/* Embedded classes: */
	private:
	struct Configuration // Structure containing tool settings
		{
		/* Elements: */
		public:
		std::string saveFileName; // Name of file to which to save the bathymetry grid
		bool postUpdate; // Flag whether to post an update message to a web server after saving the bathymetry grid
		std::string postUpdateHostName; // Name of web server to which to send update messages
		int postUpdatePort; // TCP port number of web server to which to send update messages
		std::string postUpdatePage; // Name of page on web server to which update messages are posted
		std::string postUpdateMessage; // The message to send to the web server
		double gridScale; // Overall scale factor to applied to grids on export
		
		/* Constructors and destructors: */
		Configuration(void); // Creates default configuration
		
		/* Methods: */
		void read(const Misc::ConfigurationFileSection& cfs); // Overrides configuration from configuration file section
		void write(Misc::ConfigurationFileSection& cfs) const; // Writes configuration to configuration file section
		};
	
	/* Elements: */
	private:
	Configuration configuration; // Default configuration for all tools
	WaterTable2* waterTable; // Pointer to water table object from which to request bathymetry grids
	GLsizei gridSize[2]; // Width and height of the water table's bathymetry grid
	GLfloat cellSize[2]; // Width and height of each water table cell
	
	/* Constructors and destructors: */
	public:
	BathymetrySaverToolFactory(WaterTable2* sWaterTable,Vrui::ToolManager& toolManager);
	virtual ~BathymetrySaverToolFactory(void);
	
	/* Methods from Vrui::ToolFactory: */
	virtual const char* getName(void) const;
	virtual const char* getButtonFunction(int buttonSlotIndex) const;
	virtual Vrui::Tool* createTool(const Vrui::ToolInputAssignment& inputAssignment) const;
	virtual void destroyTool(Vrui::Tool* tool) const;
	};

class BathymetrySaverTool:public Vrui::Tool,public Vrui::Application::Tool<Sandbox>
	{
	friend class BathymetrySaverToolFactory;
	
	/* Elements: */
	private:
	static BathymetrySaverToolFactory* factory; // Pointer to the factory object for this class
	BathymetrySaverToolFactory::Configuration configuration; // Configuration of this tool
	GLfloat* bathymetryBuffer; // Bathymetry grid buffer
	bool requestPending; // Flag if this tool has a pending request to retrieve a bathymetry grid
	
	/* Private methods: */
	void writeDEMFile(void) const; // Writes the bathymetry grid to a file in USGS DEM format
	void postUpdate(void) const; // Sends an update message to a web server
	
	/* Constructors and destructors: */
	public:
	static BathymetrySaverToolFactory* initClass(WaterTable2* sWaterTable,Vrui::ToolManager& toolManager);
	BathymetrySaverTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
	virtual ~BathymetrySaverTool(void);
	
	/* Methods from class Vrui::Tool: */
	virtual void configure(const Misc::ConfigurationFileSection& configFileSection);
	virtual void storeState(Misc::ConfigurationFileSection& configFileSection) const;
	virtual const Vrui::ToolFactory* getFactory(void) const;
	virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
	virtual void frame(void);
	};

#endif
