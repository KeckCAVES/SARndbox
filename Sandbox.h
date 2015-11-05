/***********************************************************************
Sandbox - Vrui application to drive an augmented reality sandbox.
Copyright (c) 2012-2015 Oliver Kreylos

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

#ifndef SANDBOX_INCLUDED
#define SANDBOX_INCLUDED

#include <Threads/TripleBuffer.h>
#include <USB/Context.h>
#include <Geometry/Box.h>
#include <Geometry/ProjectiveTransformation.h>
#include <GL/gl.h>
#include <GL/GLColorMap.h>
#include <GL/GLMaterial.h>
#include <GL/GLObject.h>
#include <GL/GLGeometryVertex.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/TextFieldSlider.h>
#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Vrui/TransparentObject.h>
#include <Vrui/Application.h>
#include <Kinect/FrameBuffer.h>
#include <Kinect/FrameSource.h>

#include "RainMaker.h"

/* Forward declarations: */
namespace Misc {
template <class ParameterParam>
class FunctionCall;
}
class GLContextData;
namespace GLMotif {
class PopupMenu;
class PopupWindow;
class TextField;
}
namespace Vrui {
class Lightsource;
}
namespace Kinect {
class Camera;
}
class FrameFilter;
class SurfaceRenderer;
class WaterTable2;
typedef Misc::FunctionCall<GLContextData&> AddWaterFunction;

class Sandbox:public Vrui::Application,public GLObject
	{
	/* Embedded classes: */
	private:
	typedef Geometry::Box<double,3> Box; // Type for bounding boxes
	typedef Geometry::ProjectiveTransformation<double,3> PTransform; // Type for projective transformations
	
	class WaterTool;
	typedef Vrui::GenericToolFactory<WaterTool> WaterToolFactory;
	
	class WaterTool:public Vrui::Tool,public Vrui::Application::Tool<Sandbox>
		{
		friend class Vrui::GenericToolFactory<WaterTool>;
		
		/* Elements: */
		private:
		static WaterToolFactory* factory; // Pointer to the factory object for this class
		
		/* Constructors and destructors: */
		public:
		static WaterToolFactory* initClass(Vrui::ToolManager& toolManager);
		WaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
		virtual ~WaterTool(void);
		
		/* Methods from class Vrui::Tool: */
		virtual const Vrui::ToolFactory* getFactory(void) const;
		virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
		};
	
	class LocalWaterTool;
	typedef Vrui::GenericToolFactory<LocalWaterTool> LocalWaterToolFactory;
	
	class LocalWaterTool:public Vrui::Tool,public Vrui::Application::Tool<Sandbox>,public Vrui::TransparentObject
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
		
		/* Methods from class Vrui::TransparentObject: */
		virtual void glRenderActionTransparent(GLContextData& contextData) const;
		
		/* New methods: */
		void addWater(GLContextData& contextData) const; // Function to render geometry that adds water to the water table
		};
	
	struct DataItem:public GLObject::DataItem
		{
		/* Elements: */
		public:
		GLuint heightColorMapObject; // Color map for the height map shader
		unsigned int heightColorMapVersion; // Version number of height color map in texture object
		GLsizei shadowBufferSize[2]; // Size of the shadow rendering frame buffer
		GLuint shadowFramebufferObject; // Frame buffer object to render shadow maps
		GLuint shadowDepthTextureObject; // Depth texture for the shadow rendering frame buffer
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	friend class WaterTool;
	friend class LocalWaterTool;
	
	/* Elements: */
	private:
	USB::Context usbContext; // USB context for the Kinect camera device
	Kinect::Camera* camera; // The Kinect camera device
	unsigned int frameSize[2]; // Width and height of the camera's depth frames
	Kinect::FrameSource::IntrinsicParameters cameraIps; // Intrinsic parameters of the Kinect camera
	FrameFilter* frameFilter; // Processing object to filter raw depth frames from the Kinect camera
	bool pauseUpdates; // Pauses updates of the topography
	Threads::TripleBuffer<Kinect::FrameBuffer> filteredFrames; // Triple buffer for incoming filtered depth frames
	PTransform projectorTransform; // The calibrated projector transformation matrix
	GLMaterial surfaceMaterial; // Material properties to render the surface
	GLColorMap heightMap; // The height color map
	unsigned int heightMapVersion; // Version number of height map
	Box bbox; // Bounding box around the surface
	SurfaceRenderer* surfaceRenderer; // Renderer for the surface
	WaterTable2* waterTable; // Water flow simulation object
	double waterSpeed; // Relative speed of water flow simulation
	int waterMaxSteps; // Maximum number of water simulation steps per frame
	GLfloat rainStrength; // Amount of water deposited by rain tools and objects on each water simulation step
	FrameFilter* rmFrameFilter; // Second frame filter for the rain maker object
	RainMaker* rainMaker; // Object to detect objects floating above the sandbox to make rain
	Threads::TripleBuffer<RainMaker::BlobList> rainObjects; // Triple buffer of detected rain objects
	const AddWaterFunction* addWaterFunction; // Render function registered with the water table
	bool addWaterFunctionRegistered; // Flag if the water adding function is currently registered with the water table
	bool fixProjectorView; // Flag whether to allow viewpoint navigation or always render from the projector's point of view
	bool hillshade; // Flag whether to use augmented reality hill shading
	bool useShadows; // Flag whether to use shadows in augmented reality hill shading
	bool useHeightMap; // Flag whether to color map the surface by elevation
	SurfaceRenderer* waterRenderer; // A second surface renderer to render the water surface directly
	Vrui::Lightsource* sun; // An external fixed light source
	GLMotif::PopupMenu* mainMenu;
	GLMotif::ToggleButton* pauseUpdatesToggle;
	GLMotif::PopupWindow* waterControlDialog;
	GLMotif::TextFieldSlider* waterSpeedSlider;
	GLMotif::TextFieldSlider* waterMaxStepsSlider;
	GLMotif::TextField* frameRateTextField;
	GLMotif::TextFieldSlider* waterAttenuationSlider;
	int controlPipeFd; // File descriptor of an optional named pipe to send control commands to a running AR Sandbox
	
	/* Private methods: */
	void rawDepthFrameDispatcher(const Kinect::FrameBuffer& frameBuffer); // Callback receiving raw depth frames from the Kinect camera; forwards them to the frame filter and rain maker objects
	void receiveFilteredFrame(const Kinect::FrameBuffer& frameBuffer); // Callback receiving filtered depth frames from the filter object
	void receiveRainObjects(const RainMaker::BlobList& newRainObjects); // Callback receiving extracted rain objects from the rain maker
	void addWater(GLContextData& contextData) const; // Function to render geometry that adds water to the water table
	void pauseUpdatesCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void showWaterControlDialogCallback(Misc::CallbackData* cbData);
	void waterSpeedSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData);
	void waterMaxStepsSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData);
	void waterAttenuationSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData);
	GLMotif::PopupMenu* createMainMenu(void);
	GLMotif::PopupWindow* createWaterControlDialog(void);
	bool loadHeightColorMap(const char* heightColorMapFileName); // Loads a new height color map from a file of the given name
	
	/* Constructors and destructors: */
	public:
	Sandbox(int& argc,char**& argv);
	virtual ~Sandbox(void);
	
	/* Methods from Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void eventCallback(EventID eventId,Vrui::InputDevice::ButtonCallbackData* cbData);
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	};

#endif
