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

#include "Sandbox.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <Misc/SelfDestructPointer.h>
#include <Misc/FunctionCalls.h>
#include <Misc/FileNameExtensions.h>
#include <IO/File.h>
#include <IO/ValueSource.h>
#include <Math/Math.h>
#include <Math/Constants.h>
#include <Geometry/Point.h>
#include <Geometry/AffineCombiner.h>
#include <Geometry/HVector.h>
#include <Geometry/Plane.h>
#include <Geometry/GeometryValueCoders.h>
#include <Geometry/OutputOperators.h>
#include <GL/gl.h>
#include <GL/GLMaterialTemplates.h>
#include <GL/GLPrintError.h>
#include <GL/GLColorMap.h>
#include <GL/GLLightTracker.h>
#include <GL/Extensions/GLEXTFramebufferObject.h>
#include <GL/Extensions/GLARBTextureRectangle.h>
#include <GL/Extensions/GLARBTextureFloat.h>
#include <GL/Extensions/GLARBTextureRg.h>
#include <GL/Extensions/GLARBDepthTexture.h>
#include <GL/Extensions/GLARBShaderObjects.h>
#include <GL/Extensions/GLARBVertexShader.h>
#include <GL/Extensions/GLARBFragmentShader.h>
#include <GL/Extensions/GLARBMultitexture.h>
#include <GL/GLContextData.h>
#include <GL/GLGeometryWrappers.h>
#include <GL/GLTransformationWrappers.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/WidgetManager.h>
#include <GLMotif/PopupMenu.h>
#include <GLMotif/Menu.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/Margin.h>
#include <GLMotif/Label.h>
#include <GLMotif/TextField.h>
#include <Vrui/Vrui.h>
#include <Vrui/Lightsource.h>
#include <Vrui/LightsourceManager.h>
#include <Vrui/Viewer.h>
#include <Vrui/ToolManager.h>
#include <Vrui/DisplayState.h>
#include <Vrui/OpenFile.h>
#include <Kinect/Camera.h>

#define SAVEDEPTH 0

#if SAVEDEPTH
#include <Images/RGBImage.h>
#include <Images/WriteImageFile.h>
#endif

#include "FrameFilter.h"
#include "SurfaceRenderer.h"
#include "WaterTable2.h"

/*******************************************
Static elements of class Sandbox::WaterTool:
*******************************************/

Sandbox::WaterToolFactory* Sandbox::WaterTool::factory=0;

/***********************************
Methods of class Sandbox::WaterTool:
***********************************/

Sandbox::WaterToolFactory* Sandbox::WaterTool::initClass(Vrui::ToolManager& toolManager)
	{
	/* Create the tool factory: */
	factory=new WaterToolFactory("WaterTool","Manage Water",0,toolManager);
	
	/* Set up the tool class' input layout: */
	factory->setNumButtons(2);
	factory->setButtonFunction(0,"Rain");
	factory->setButtonFunction(1,"Dry");
	
	/* Register and return the class: */
	toolManager.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
	return factory;
	}

Sandbox::WaterTool::WaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment)
	{
	}

Sandbox::WaterTool::~WaterTool(void)
	{
	}

const Vrui::ToolFactory* Sandbox::WaterTool::getFactory(void) const
	{
	return factory;
	}

void Sandbox::WaterTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	GLfloat waterAmount=application->rainStrength;
	if(!cbData->newButtonState)
		waterAmount=-waterAmount;
	if(buttonSlotIndex==1)
		waterAmount=-waterAmount;
	application->waterTable->setWaterDeposit(application->waterTable->getWaterDeposit()+waterAmount);
	}

/************************************************
Static elements of class Sandbox::LocalWaterTool:
************************************************/

Sandbox::LocalWaterToolFactory* Sandbox::LocalWaterTool::factory=0;

/****************************************
Methods of class Sandbox::LocalWaterTool:
****************************************/

Sandbox::LocalWaterToolFactory* Sandbox::LocalWaterTool::initClass(Vrui::ToolManager& toolManager)
	{
	/* Create the tool factory: */
	factory=new LocalWaterToolFactory("LocalWaterTool","Add Water Locally",0,toolManager);
	
	/* Set up the tool class' input layout: */
	factory->setNumButtons(2);
	factory->setButtonFunction(0,"Rain");
	factory->setButtonFunction(1,"Dry");
	
	/* Register and return the class: */
	toolManager.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
	return factory;
	}

Sandbox::LocalWaterTool::LocalWaterTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment),
	 addWaterFunction(0),
	 adding(0.0f)
	{
	}

Sandbox::LocalWaterTool::~LocalWaterTool(void)
	{
	}

void Sandbox::LocalWaterTool::initialize(void)
	{
	/* Register a render function with the water table: */
	if(application->waterTable!=0)
		{
		addWaterFunction=Misc::createFunctionCall(this,&Sandbox::LocalWaterTool::addWater);
		application->waterTable->addRenderFunction(addWaterFunction);
		}
	}

void Sandbox::LocalWaterTool::deinitialize(void)
	{
	/* Unregister the render function from the water table: */
	if(application->waterTable!=0)
		application->waterTable->removeRenderFunction(addWaterFunction);
	delete addWaterFunction;
	addWaterFunction=0;
	}

const Vrui::ToolFactory* Sandbox::LocalWaterTool::getFactory(void) const
	{
	return factory;
	}

void Sandbox::LocalWaterTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	GLfloat waterAmount=application->rainStrength;
	if(!cbData->newButtonState)
		waterAmount=-waterAmount;
	if(buttonSlotIndex==1)
		waterAmount=-waterAmount;
	adding+=waterAmount;
	}

void Sandbox::LocalWaterTool::glRenderActionTransparent(GLContextData& contextData) const
	{
	glPushAttrib(GL_ENABLE_BIT|GL_POLYGON_BIT);
	
	/* Go to navigational coordinates: */
	glPushMatrix();
	glLoadMatrix(Vrui::getDisplayState(contextData).modelviewNavigational);
	
	/* Get the current rain disk position and size in camera coordinates: */
	Vrui::Point rainPos=Vrui::getInverseNavigationTransformation().transform(getButtonDevicePosition(0));
	Vrui::Scalar rainRadius=Vrui::getPointPickDistance()*Vrui::Scalar(3);
	
	/* Construct the rain cylinder: */
	Vrui::Vector z=application->waterTable->getBaseTransform().inverseTransform(Vrui::Vector(0,0,1));
	Vrui::Vector x=Geometry::normal(z);
	Vrui::Vector y=Geometry::cross(z,x);
	x.normalize();
	y.normalize();
	
	/* Set the rain cylinder's material: */
	GLfloat diffuseCol[4]={0.0f,0.0f,1.0f,0.333f};
	glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE,diffuseCol);
	
	/* Render the back faces of the rain cylinder: */
	glCullFace(GL_FRONT);
	glBegin(GL_QUAD_STRIP);
	for(int i=0;i<=32;++i)
		{
		Vrui::Scalar angle=Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi*Vrui::Scalar(i)/Vrui::Scalar(32);
		glNormal(x*Math::cos(angle)+y*Math::sin(angle));
		glVertex(rainPos+x*(Math::cos(angle)*rainRadius)+y*(Math::sin(angle)*rainRadius));
		glVertex(rainPos+x*(Math::cos(angle)*rainRadius)+y*(Math::sin(angle)*rainRadius)-z*Vrui::Scalar(50));
		}
	glEnd();
	
	/* Render the front faces of the rain cylinder: */
	glCullFace(GL_BACK);
	glBegin(GL_QUAD_STRIP);
	for(int i=0;i<=32;++i)
		{
		Vrui::Scalar angle=Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi*Vrui::Scalar(i)/Vrui::Scalar(32);
		glNormal(x*Math::cos(angle)+y*Math::sin(angle));
		glVertex(rainPos+x*(Math::cos(angle)*rainRadius)+y*(Math::sin(angle)*rainRadius));
		glVertex(rainPos+x*(Math::cos(angle)*rainRadius)+y*(Math::sin(angle)*rainRadius)-z*Vrui::Scalar(50));
		}
	glEnd();
	glBegin(GL_POLYGON);
	glNormal(z);
	for(int i=0;i<32;++i)
		{
		Vrui::Scalar angle=Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi*Vrui::Scalar(i)/Vrui::Scalar(32);
		glVertex(rainPos+x*(Math::cos(angle)*rainRadius)+y*(Math::sin(angle)*rainRadius));
		}
	glEnd();
	
	glPopMatrix();
	glPopAttrib();
	}

void Sandbox::LocalWaterTool::addWater(GLContextData& contextData) const
	{
	if(adding!=0.0f)
		{
		glPushAttrib(GL_ENABLE_BIT);
		glDisable(GL_CULL_FACE);
		
		/* Get the current rain disk position and size in camera coordinates: */
		Vrui::Point rainPos=Vrui::getInverseNavigationTransformation().transform(getButtonDevicePosition(0));
		Vrui::Scalar rainRadius=Vrui::getPointPickDistance()*Vrui::Scalar(3);
		
		/* Render the rain disk: */
		Vrui::Vector z=application->waterTable->getBaseTransform().inverseTransform(Vrui::Vector(0,0,1));
		Vrui::Vector x=Geometry::normal(z);
		Vrui::Vector y=Geometry::cross(z,x);
		x*=rainRadius/Geometry::mag(x);
		y*=rainRadius/Geometry::mag(y);
		
		glVertexAttrib1fARB(1,adding);
		glBegin(GL_POLYGON);
		for(int i=0;i<32;++i)
			{
			Vrui::Scalar angle=Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi*Vrui::Scalar(i)/Vrui::Scalar(32);
			glVertex(rainPos+x*Math::cos(angle)+y*Math::sin(angle));
			}
		glEnd();
		
		glPopAttrib();
		}
	}

/**********************************
Methods of class Sandbox::DataItem:
**********************************/

Sandbox::DataItem::DataItem(void)
	:heightColorMapObject(0),heightColorMapVersion(0),
	 shadowFramebufferObject(0),shadowDepthTextureObject(0)
	{
	/* Check if all required extensions are supported: */
	bool supported=GLEXTFramebufferObject::isSupported();
	supported=supported&&GLARBTextureRectangle::isSupported();
	supported=supported&&GLARBTextureFloat::isSupported();
	supported=supported&&GLARBTextureRg::isSupported();
	supported=supported&&GLARBDepthTexture::isSupported();
	supported=supported&&GLARBShaderObjects::isSupported();
	supported=supported&&GLARBVertexShader::isSupported();
	supported=supported&&GLARBFragmentShader::isSupported();
	supported=supported&&GLARBMultitexture::isSupported();
	if(!supported)
		Misc::throwStdErr("Sandbox: Not all required extensions are supported by local OpenGL");
	
	/* Initialize all required extensions: */
	GLEXTFramebufferObject::initExtension();
	GLARBTextureRectangle::initExtension();
	GLARBTextureFloat::initExtension();
	GLARBTextureRg::initExtension();
	GLARBDepthTexture::initExtension();
	GLARBShaderObjects::initExtension();
	GLARBVertexShader::initExtension();
	GLARBFragmentShader::initExtension();
	GLARBMultitexture::initExtension();
	}

Sandbox::DataItem::~DataItem(void)
	{
	/* Delete all shaders, buffers, and texture objects: */
	glDeleteTextures(1,&heightColorMapObject);
	glDeleteFramebuffersEXT(1,&shadowFramebufferObject);
	glDeleteTextures(1,&shadowDepthTextureObject);
	}

/************************
Methods of class Sandbox:
************************/

void Sandbox::rawDepthFrameDispatcher(const Kinect::FrameBuffer& frameBuffer)
	{
	/* Pass the received frame to the frame filter and the rain maker's frame filter: */
	if(frameFilter!=0&&!pauseUpdates)
		frameFilter->receiveRawFrame(frameBuffer);
	if(rmFrameFilter!=0)
		rmFrameFilter->receiveRawFrame(frameBuffer);
	}

void Sandbox::receiveFilteredFrame(const Kinect::FrameBuffer& frameBuffer)
	{
	/* Put the new frame into the frame input buffer: */
	filteredFrames.postNewValue(frameBuffer);
	
	/* Wake up the foreground thread: */
	Vrui::requestUpdate();
	}

void Sandbox::receiveRainObjects(const RainMaker::BlobList& newRainObjects)
	{
	/* Put the new object list into the object list buffer: */
	rainObjects.postNewValue(newRainObjects);
	
	/* Don't wake up the foreground thread; do it when a new filtered frame arrives: */
	// Vrui::requestUpdate();
	}

void Sandbox::addWater(GLContextData& contextData) const
	{
	/* Check if the most recent rain object list is not empty: */
	if(!rainObjects.getLockedValue().empty())
		{
		/* Render all rain objects into the water table: */
		glPushAttrib(GL_ENABLE_BIT);
		glDisable(GL_CULL_FACE);
		
		/* Create a local coordinate frame to render rain disks: */
		Vrui::Vector z=waterTable->getBaseTransform().inverseTransform(Vrui::Vector(0,0,1));
		Vrui::Vector x=Geometry::normal(z);
		Vrui::Vector y=Geometry::cross(z,x);
		x.normalize();
		y.normalize();
		
		glVertexAttrib1fARB(1,rainStrength/waterSpeed);
		for(RainMaker::BlobList::const_iterator roIt=rainObjects.getLockedValue().begin();roIt!=rainObjects.getLockedValue().end();++roIt)
			{
			/* Render the rain object: */
			glBegin(GL_POLYGON);
			for(int i=0;i<32;++i)
				{
				Vrui::Scalar angle=Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi*Vrui::Scalar(i)/Vrui::Scalar(32);
				glVertex(roIt->centroid+x*(Math::cos(angle)*roIt->radius)+y*(Math::sin(angle)*roIt->radius));
				}
			glEnd();
			}
		
		glPopAttrib();
		}
	}

void Sandbox::pauseUpdatesCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData)
	{
	pauseUpdates=cbData->set;
	}

void Sandbox::showWaterControlDialogCallback(Misc::CallbackData* cbData)
	{
	Vrui::popupPrimaryWidget(waterControlDialog);
	}

void Sandbox::waterSpeedSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData)
	{
	waterSpeed=cbData->value;
	}

void Sandbox::waterMaxStepsSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData)
	{
	waterMaxSteps=int(Math::floor(cbData->value+0.5));
	}

void Sandbox::waterAttenuationSliderCallback(GLMotif::TextFieldSlider::ValueChangedCallbackData* cbData)
	{
	waterTable->setAttenuation(GLfloat(1.0-cbData->value));
	}

GLMotif::PopupMenu* Sandbox::createMainMenu(void)
	{
	/* Create a popup shell to hold the main menu: */
	GLMotif::PopupMenu* mainMenuPopup=new GLMotif::PopupMenu("MainMenuPopup",Vrui::getWidgetManager());
	mainMenuPopup->setTitle("AR Sandbox");
	
	/* Create the main menu itself: */
	GLMotif::Menu* mainMenu=new GLMotif::Menu("MainMenu",mainMenuPopup,false);
	
	/* Create a button to pause topography updates: */
	pauseUpdatesToggle=new GLMotif::ToggleButton("PauseUpdatesToggle",mainMenu,"Pause Topography");
	pauseUpdatesToggle->setToggle(false);
	pauseUpdatesToggle->getValueChangedCallbacks().add(this,&Sandbox::pauseUpdatesCallback);
	
	if(waterTable!=0)
		{
		/* Create a button to show the water control dialog: */
		GLMotif::Button* showWaterControlDialogButton=new GLMotif::Button("ShowWaterControlDialogButton",mainMenu,"Show Water Simulation Control");
		showWaterControlDialogButton->getSelectCallbacks().add(this,&Sandbox::showWaterControlDialogCallback);
		}
	
	/* Finish building the main menu: */
	mainMenu->manageChild();
	
	return mainMenuPopup;
	}

GLMotif::PopupWindow* Sandbox::createWaterControlDialog(void)
	{
	const GLMotif::StyleSheet& ss=*Vrui::getWidgetManager()->getStyleSheet();
	
	/* Create a popup window shell: */
	GLMotif::PopupWindow* waterControlDialogPopup=new GLMotif::PopupWindow("WaterControlDialogPopup",Vrui::getWidgetManager(),"Water Simulation Control");
	waterControlDialogPopup->setCloseButton(true);
	waterControlDialogPopup->setResizableFlags(true,false);
	waterControlDialogPopup->popDownOnClose();
	
	GLMotif::RowColumn* waterControlDialog=new GLMotif::RowColumn("WaterControlDialog",waterControlDialogPopup,false);
	waterControlDialog->setOrientation(GLMotif::RowColumn::VERTICAL);
	waterControlDialog->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	waterControlDialog->setNumMinorWidgets(2);
	
	new GLMotif::Label("WaterSpeedLabel",waterControlDialog,"Speed");
	
	waterSpeedSlider=new GLMotif::TextFieldSlider("WaterSpeedSlider",waterControlDialog,8,ss.fontHeight*10.0f);
	waterSpeedSlider->getTextField()->setFieldWidth(7);
	waterSpeedSlider->getTextField()->setPrecision(4);
	waterSpeedSlider->getTextField()->setFloatFormat(GLMotif::TextField::SMART);
	waterSpeedSlider->setSliderMapping(GLMotif::TextFieldSlider::EXP10);
	waterSpeedSlider->setValueRange(0.001,10.0,0.05);
	waterSpeedSlider->getSlider()->addNotch(0.0f);
	waterSpeedSlider->setValue(waterSpeed);
	waterSpeedSlider->getValueChangedCallbacks().add(this,&Sandbox::waterSpeedSliderCallback);
	
	new GLMotif::Label("WaterMaxStepsLabel",waterControlDialog,"Max Steps");
	
	waterMaxStepsSlider=new GLMotif::TextFieldSlider("WaterMaxStepsSlider",waterControlDialog,8,ss.fontHeight*10.0f);
	waterMaxStepsSlider->getTextField()->setFieldWidth(7);
	waterMaxStepsSlider->getTextField()->setPrecision(0);
	waterMaxStepsSlider->getTextField()->setFloatFormat(GLMotif::TextField::FIXED);
	waterMaxStepsSlider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	waterMaxStepsSlider->setValueType(GLMotif::TextFieldSlider::UINT);
	waterMaxStepsSlider->setValueRange(0,200,1);
	waterMaxStepsSlider->setValue(waterMaxSteps);
	waterMaxStepsSlider->getValueChangedCallbacks().add(this,&Sandbox::waterMaxStepsSliderCallback);
	
	new GLMotif::Label("FrameRateLabel",waterControlDialog,"Frame Rate");
	
	GLMotif::Margin* frameRateMargin=new GLMotif::Margin("FrameRateMargin",waterControlDialog,false);
	frameRateMargin->setAlignment(GLMotif::Alignment::LEFT);
	
	frameRateTextField=new GLMotif::TextField("FrameRateTextField",frameRateMargin,8);
	frameRateTextField->setFieldWidth(7);
	frameRateTextField->setPrecision(2);
	frameRateTextField->setFloatFormat(GLMotif::TextField::FIXED);
	frameRateTextField->setValue(0.0);
	
	frameRateMargin->manageChild();
	
	new GLMotif::Label("WaterAttenuationLabel",waterControlDialog,"Attenuation");
	
	waterAttenuationSlider=new GLMotif::TextFieldSlider("WaterAttenuationSlider",waterControlDialog,8,ss.fontHeight*10.0f);
	waterAttenuationSlider->getTextField()->setFieldWidth(7);
	waterAttenuationSlider->getTextField()->setPrecision(5);
	waterAttenuationSlider->getTextField()->setFloatFormat(GLMotif::TextField::SMART);
	waterAttenuationSlider->setSliderMapping(GLMotif::TextFieldSlider::EXP10);
	waterAttenuationSlider->setValueRange(0.001,1.0,0.01);
	waterAttenuationSlider->getSlider()->addNotch(Math::log10(1.0-double(waterTable->getAttenuation())));
	waterAttenuationSlider->setValue(1.0-double(waterTable->getAttenuation()));
	waterAttenuationSlider->getValueChangedCallbacks().add(this,&Sandbox::waterAttenuationSliderCallback);
	
	waterControlDialog->manageChild();
	
	return waterControlDialogPopup;
	}

bool Sandbox::loadHeightColorMap(const char* heightColorMapFileName)
	{
	std::vector<GLColorMap::Color> heightMapColors;
	std::vector<GLdouble> heightMapKeys;
	IO::ValueSource heightMapSource(Vrui::openFile(heightColorMapFileName));
	if(Misc::hasCaseExtension(heightColorMapFileName,".cpt"))
		{
		heightMapSource.setPunctuation("\n");
		heightMapSource.skipWs();
		while(!heightMapSource.eof())
			{
			/* Read the next color map key value: */
			heightMapKeys.push_back(GLdouble(heightMapSource.readNumber()));
			
			/* Read the next color map color value: */
			GLColorMap::Color color;
			for(int i=0;i<3;++i)
				color[i]=GLColorMap::Color::Scalar(heightMapSource.readNumber()/255.0);
			color[3]=GLColorMap::Color::Scalar(1);
			heightMapColors.push_back(color);
			if(!heightMapSource.isLiteral('\n'))
				return false;
			}
		}
	else
		{
		heightMapSource.setPunctuation(",\n");
		heightMapSource.skipWs();
		while(!heightMapSource.eof())
			{
			/* Read the next color map key value: */
			heightMapKeys.push_back(GLdouble(heightMapSource.readNumber()));
			if(!heightMapSource.isLiteral(','))
				return false;
			
			/* Read the next color map color value: */
			GLColorMap::Color color;
			for(int i=0;i<3;++i)
				color[i]=GLColorMap::Color::Scalar(heightMapSource.readNumber());
			color[3]=GLColorMap::Color::Scalar(1);
			heightMapColors.push_back(color);
			if(!heightMapSource.isLiteral('\n'))
				return false;
			}
		}
	
	/* Update the height color map: */
	heightMap=GLColorMap(heightMapKeys.size(),&heightMapColors[0],&heightMapKeys[0]);
	++heightMapVersion;
	
	return true;
	}

Sandbox::Sandbox(int& argc,char**& argv)
	:Vrui::Application(argc,argv),
	 camera(0),
	 frameFilter(0),pauseUpdates(false),
	 surfaceMaterial(GLMaterial::Color(0.8f,0.8f,0.8f),GLMaterial::Color(1.0f,1.0f,1.0f),25.0f),
	 heightMapVersion(0),
	 surfaceRenderer(0),
	 waterTable(0),waterSpeed(1.0),waterMaxSteps(30),rainStrength(0.25f),
	 rmFrameFilter(0),rainMaker(0),addWaterFunction(0),addWaterFunctionRegistered(false),
	 fixProjectorView(false),hillshade(false),useShadows(false),useHeightMap(false),
	 waterRenderer(0),
	 sun(0),
	 mainMenu(0),pauseUpdatesToggle(0),waterControlDialog(0),
	 waterSpeedSlider(0),waterMaxStepsSlider(0),frameRateTextField(0),waterAttenuationSlider(0),
	 controlPipeFd(-1)
	{
	/* Initialize the custom tool classes: */
	WaterTool::initClass(*Vrui::getToolManager());
	LocalWaterTool::initClass(*Vrui::getToolManager());
	addEventTool("Pause Topography",0,0);
	
	/* Process command line parameters: */
	bool printHelp=false;
	int cameraIndex=0;
	std::string sandboxLayoutFileName=CONFIGDIR;
	sandboxLayoutFileName.push_back('/');
	sandboxLayoutFileName.append("BoxLayout.txt");
	bool useHeightMap=true;
	std::string heightColorMapFileName=CONFIGDIR;
	heightColorMapFileName.push_back('/');
	heightColorMapFileName.append(DEFAULTHEIGHTCOLORMAPNAME);
	double elevationMin=-1000.0;
	double elevationMax=1000.0;
	unsigned int validMin=0;
	unsigned int validMax=2047;
	int numAveragingSlots=30;
	unsigned int minNumSamples=10;
	unsigned int maxVariance=2;
	float hysteresis=0.1f;
	bool useContourLines=true;
	GLfloat contourLineSpacing=0.75f;
	unsigned int wtSize[2];
	wtSize[0]=640U;
	wtSize[1]=480U;
	GLfloat waterOpacity=2.0f;
	bool renderWaterSurface=false;
	double rainElevationMin=-1000.0;
	double rainElevationMax=1000.0;
	double evaporationRate=0.0;
	const char* controlPipeName=0;
	for(int i=1;i<argc;++i)
		{
		if(argv[i][0]=='-')
			{
			if(strcasecmp(argv[i]+1,"h")==0)
				printHelp=true;
			else if(strcasecmp(argv[i]+1,"c")==0)
				{
				++i;
				cameraIndex=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"slf")==0)
				{
				++i;
				sandboxLayoutFileName=argv[i];
				}
			else if(strcasecmp(argv[i]+1,"er")==0)
				{
				++i;
				elevationMin=atof(argv[i]);
				++i;
				elevationMax=atof(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"nas")==0)
				{
				++i;
				numAveragingSlots=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"sp")==0)
				{
				++i;
				minNumSamples=atoi(argv[i]);
				++i;
				maxVariance=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"he")==0)
				{
				++i;
				hysteresis=float(atof(argv[i]));
				}
			else if(strcasecmp(argv[i]+1,"nhm")==0)
				{
				useHeightMap=false;
				}
			else if(strcasecmp(argv[i]+1,"hcm")==0)
				{
				++i;
				heightColorMapFileName=argv[i];
				}
			else if(strcasecmp(argv[i]+1,"ncl")==0)
				{
				useContourLines=false;
				}
			else if(strcasecmp(argv[i]+1,"cls")==0)
				{
				++i;
				contourLineSpacing=GLfloat(atof(argv[i]));
				}
			else if(strcasecmp(argv[i]+1,"wts")==0)
				{
				for(int j=0;j<2;++j)
					{
					++i;
					wtSize[j]=(unsigned int)(atoi(argv[i]));
					}
				}
			else if(strcasecmp(argv[i]+1,"ws")==0)
				{
				++i;
				waterSpeed=atof(argv[i]);
				++i;
				waterMaxSteps=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"wo")==0)
				{
				++i;
				waterOpacity=GLfloat(atof(argv[i]));
				}
			else if(strcasecmp(argv[i]+1,"rer")==0)
				{
				++i;
				rainElevationMin=atof(argv[i]);
				++i;
				rainElevationMax=atof(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"rs")==0)
				{
				++i;
				rainStrength=GLfloat(atof(argv[i]));
				}
			else if(strcasecmp(argv[i]+1,"evr")==0)
				{
				++i;
				evaporationRate=atof(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"fpv")==0)
				fixProjectorView=true;
			else if(strcasecmp(argv[i]+1,"hs")==0)
				hillshade=true;
			else if(strcasecmp(argv[i]+1,"us")==0)
				useShadows=true;
			else if(strcasecmp(argv[i]+1,"uhm")==0)
				useHeightMap=true;
			else if(strcasecmp(argv[i]+1,"rws")==0)
				renderWaterSurface=true;
			else if(strcasecmp(argv[i]+1,"cp")==0)
				{
				++i;
				controlPipeName=argv[i];
				}
			}
		}
	
	if(printHelp)
		{
		std::cout<<"Usage: SARndbox [option 1] ... [option n]"<<std::endl;
		std::cout<<"  Options:"<<std::endl;
		std::cout<<"  -h"<<std::endl;
		std::cout<<"     Prints this help message"<<std::endl;
		std::cout<<"  -c <camera index>"<<std::endl;
		std::cout<<"     Selects the local Kinect camera of the given index (0: first camera"<<std::endl;
		std::cout<<"     on USB bus)"<<std::endl;
		std::cout<<"     Default: 0"<<std::endl;
		std::cout<<"  -slf <sandbox layout file name>"<<std::endl;
		std::cout<<"     Loads the sandbox layout file of the given name"<<std::endl;
		std::cout<<"     Default: "<<CONFIGDIR<<"/BoxLayout.txt"<<std::endl;
		std::cout<<"  -er <min elevation> <max elevation>"<<std::endl;
		std::cout<<"     Sets the range of valid sand surface elevations relative to the"<<std::endl;
		std::cout<<"     ground plane in cm"<<std::endl;
		std::cout<<"     Default: Range of elevation color map"<<std::endl;
		std::cout<<"  -nas <num averaging slots>"<<std::endl;
		std::cout<<"     Sets the number of averaging slots in the frame filter; latency is"<<std::endl;
		std::cout<<"     <num averaging slots> * 1/30 s"<<std::endl;
		std::cout<<"     Default: 30"<<std::endl;
		std::cout<<"  -sp <min num samples> <max variance>"<<std::endl;
		std::cout<<"     Sets the frame filter parameters minimum number of valid samples"<<std::endl;
		std::cout<<"     and maximum sample variance before convergence"<<std::endl;
		std::cout<<"     Default: 10 2"<<std::endl;
		std::cout<<"  -he <hysteresis envelope>"<<std::endl;
		std::cout<<"     Sets the size of the hysteresis envelope used for jitter removal"<<std::endl;
		std::cout<<"     Default: 0.1"<<std::endl;
		std::cout<<"  -nhm"<<std::endl;
		std::cout<<"     Disables elevation color mapping"<<std::endl;
		std::cout<<"  -hcm <elevation color map file name>"<<std::endl;
		std::cout<<"     Sets the name of the elevation color map"<<std::endl;
		std::cout<<"     Default: "<<CONFIGDIR<<"/"<<DEFAULTHEIGHTCOLORMAPNAME<<std::endl;
		std::cout<<"  -ncl"<<std::endl;
		std::cout<<"     Disables topographic contour lines"<<std::endl;
		std::cout<<"  -cls <contour line spacing>"<<std::endl;
		std::cout<<"     Sets the elevation distance between adjacent topographic contour"<<std::endl;
		std::cout<<"     lines in cm"<<std::endl;
		std::cout<<"     Default: 0.75"<<std::endl;
		std::cout<<"  -wts <water grid width> <water grid height>"<<std::endl;
		std::cout<<"     Sets the width and height of the water flow simulation grid"<<std::endl;
		std::cout<<"     Default: 640 480"<<std::endl;
		std::cout<<"  -ws <water speed> <water max steps>"<<std::endl;
		std::cout<<"     Sets the relative speed of the water simulation and the maximum"<<std::endl;
		std::cout<<"     number of simulation steps per frame"<<std::endl;
		std::cout<<"     Default: 1.0 30"<<std::endl;
		std::cout<<"  -wo <water opacity>"<<std::endl;
		std::cout<<"     Sets the water depth at which water appears opaque in cm"<<std::endl;
		std::cout<<"     Default: 2.0"<<std::endl;
		std::cout<<"  -rer <min rain elevation> <max rain elevation>"<<std::endl;
		std::cout<<"     Sets the elevation range of the rain cloud level relative to the"<<std::endl;
		std::cout<<"     ground plane in cm"<<std::endl;
		std::cout<<"     Default: Above range of elevation color map"<<std::endl;
		std::cout<<"  -rs <rain strength>"<<std::endl;
		std::cout<<"     Sets the strength of global or local rainfall in cm/s"<<std::endl;
		std::cout<<"     Default: 0.25"<<std::endl;
		std::cout<<"  -evr <evaporation rate>"<<std::endl;
		std::cout<<"     Water evaporation rate in cm/s"<<std::endl;
		std::cout<<"     Default: 0.0"<<std::endl;
		std::cout<<"  -fpv"<<std::endl;
		std::cout<<"     Fixes the navigation transformation so that Kinect camera and"<<std::endl;
		std::cout<<"     projector are aligned, as defined by the projector calibration file"<<std::endl;
		std::cout<<"  -hs"<<std::endl;
		std::cout<<"     Enables hill shading"<<std::endl;
		std::cout<<"  -us"<<std::endl;
		std::cout<<"     Enables shadows"<<std::endl;
		std::cout<<"  -uhm"<<std::endl;
		std::cout<<"     Enables elevation color mapping"<<std::endl;
		std::cout<<"  -rws"<<std::endl;
		std::cout<<"     Renders water surface as geometric surface"<<std::endl;
		std::cout<<"  -cp <control pipe name>"<<std::endl;
		std::cout<<"     Sets the name of a named POSIX pipe from which to read control commands"<<std::endl;
		}
	
	/* Enable background USB event handling: */
	usbContext.startEventHandling();
	
	/* Open the Kinect camera device: */
	camera=new Kinect::Camera(usbContext,cameraIndex);
	camera->setCompressDepthFrames(true);
	camera->setSmoothDepthFrames(false);
	for(int i=0;i<2;++i)
		frameSize[i]=camera->getActualFrameSize(Kinect::FrameSource::DEPTH)[i];
	
	/* Get the camera's per-pixel depth correction parameters: */
	Misc::SelfDestructPointer<Kinect::FrameSource::DepthCorrection> depthCorrection(camera->getDepthCorrectionParameters());
	
	/* Get the camera's intrinsic parameters: */
	cameraIps=camera->getIntrinsicParameters();
	
	/* Read the sandbox layout file: */
	Geometry::Plane<double,3> basePlane;
	Geometry::Point<double,3> basePlaneCorners[4];
	{
	IO::ValueSource layoutSource(Vrui::openFile(sandboxLayoutFileName.c_str()));
	layoutSource.skipWs();
	std::string s=layoutSource.readLine();
	basePlane=Misc::ValueCoder<Geometry::Plane<double,3> >::decode(s.c_str(),s.c_str()+s.length());
	basePlane.normalize();
	for(int i=0;i<4;++i)
		{
		layoutSource.skipWs();
		s=layoutSource.readLine();
		basePlaneCorners[i]=Misc::ValueCoder<Geometry::Point<double,3> >::decode(s.c_str(),s.c_str()+s.length());
		}
	}
	
	/* Load the height color map: */
	if(!loadHeightColorMap(heightColorMapFileName.c_str()))
		Misc::throwStdErr("Sandbox::Sandbox: Format error in height color map %s",heightColorMapFileName.c_str());
	
	/* Limit the valid elevation range to the extent of the height color map: */
	if(elevationMin<heightMap.getScalarRangeMin())
		elevationMin=heightMap.getScalarRangeMin();
	if(elevationMax>heightMap.getScalarRangeMax())
		elevationMax=heightMap.getScalarRangeMax();
	
	/* Create the frame filter object: */
	frameFilter=new FrameFilter(frameSize,numAveragingSlots,cameraIps.depthProjection,basePlane);
	frameFilter->setDepthCorrection(*depthCorrection);
	frameFilter->setValidElevationInterval(cameraIps.depthProjection,basePlane,elevationMin,elevationMax);
	frameFilter->setStableParameters(minNumSamples,maxVariance);
	frameFilter->setHysteresis(hysteresis);
	frameFilter->setSpatialFilter(true);
	frameFilter->setOutputFrameFunction(Misc::createFunctionCall(this,&Sandbox::receiveFilteredFrame));
	
	/* Limit the valid rain elevation range to above the valid elevation range: */
	if(rainElevationMin<elevationMax)
		rainElevationMin=elevationMax;
	if(rainElevationMax<rainElevationMin)
		rainElevationMax=rainElevationMin;
	
	/* Create the rain maker object: */
	rainMaker=new RainMaker(frameSize,camera->getActualFrameSize(Kinect::FrameSource::COLOR),cameraIps.depthProjection,cameraIps.colorProjection,basePlane,rainElevationMin,rainElevationMax,20);
	rainMaker->setDepthIsFloat(true);
	rainMaker->setOutputBlobsFunction(Misc::createFunctionCall(this,&Sandbox::receiveRainObjects));
	
	/* Create a second frame filter for the rain maker: */
	rmFrameFilter=new FrameFilter(frameSize,10,cameraIps.depthProjection,basePlane);
	rmFrameFilter->setDepthCorrection(*depthCorrection);
	rmFrameFilter->setValidElevationInterval(cameraIps.depthProjection,basePlane,rainElevationMin,rainElevationMax);
	rmFrameFilter->setStableParameters(5,3);
	rmFrameFilter->setRetainValids(false);
	rmFrameFilter->setInstableValue(2047.0f);
	rmFrameFilter->setSpatialFilter(false);
	rmFrameFilter->setOutputFrameFunction(Misc::createFunctionCall(rainMaker,&RainMaker::receiveRawDepthFrame));
	
	/* Start streaming depth frames: */
	camera->startStreaming(Misc::createFunctionCall(rainMaker,&RainMaker::receiveRawColorFrame),Misc::createFunctionCall(this,&Sandbox::rawDepthFrameDispatcher));
	
	/* Load the projector transformation: */
	if(fixProjectorView)
		{
		std::string transformFileName=CONFIGDIR;
		transformFileName.push_back('/');
		transformFileName.append("ProjectorMatrix.dat");
		try
			{
			IO::FilePtr transformFile=Vrui::openFile(transformFileName.c_str(),IO::File::ReadOnly);
			transformFile->setEndianness(Misc::LittleEndian);
			double pt[16];
			transformFile->read(pt,16);
			projectorTransform=PTransform::fromRowMajor(pt);
			}
		catch(std::runtime_error err)
			{
			std::cerr<<"Cannot fix projector view due to exception "<<err.what()<<std::endl;
			fixProjectorView=false;
			}
		}
	
	/* Calculate a bounding box around all potential surfaces: */
	bbox=Box::empty;
	for(int i=0;i<4;++i)
		{
		bbox.addPoint(basePlane.project(basePlaneCorners[i])+basePlane.getNormal()*elevationMin);
		bbox.addPoint(basePlane.project(basePlaneCorners[i])+basePlane.getNormal()*elevationMax);
		}
	
	/* Initialize the water flow simulator: */
	waterTable=new WaterTable2(wtSize[0],wtSize[1],basePlane,basePlaneCorners);
	waterTable->setElevationRange(elevationMin,rainElevationMax);
	waterTable->setWaterDeposit(evaporationRate);
	
	/* Register a render function with the water table: */
	addWaterFunction=Misc::createFunctionCall(this,&Sandbox::addWater);
	waterTable->addRenderFunction(addWaterFunction);
	addWaterFunctionRegistered=true;
	
	/* Initialize the surface renderer: */
	surfaceRenderer=new SurfaceRenderer(frameSize,cameraIps.depthProjection,basePlane);
	surfaceRenderer->setUseHeightMap(useHeightMap);
	surfaceRenderer->setHeightMapRange(heightMap.getNumEntries(),heightMap.getScalarRangeMin(),heightMap.getScalarRangeMax());
	surfaceRenderer->setDrawContourLines(useContourLines);
	surfaceRenderer->setContourLineDistance(contourLineSpacing);
	if(hillshade)
		surfaceRenderer->setIlluminate(true);
	if(waterTable!=0&&waterSpeed>0.0)
		{
		if(renderWaterSurface)
			{
			/* Create a second surface renderer to render the water surface directly: */
			SurfaceRenderer::PTransform waterTransform(1);
			WaterTable2::Box wd=waterTable->getDomain();
			waterTransform.getMatrix()(0,0)=SurfaceRenderer::Scalar(wd.max[0]-wd.min[0])/SurfaceRenderer::Scalar(wtSize[0]);
			waterTransform.getMatrix()(0,3)=SurfaceRenderer::Scalar(wd.min[0]);
			waterTransform.getMatrix()(1,1)=SurfaceRenderer::Scalar(wd.max[1]-wd.min[1])/SurfaceRenderer::Scalar(wtSize[1]);
			waterTransform.getMatrix()(1,3)=SurfaceRenderer::Scalar(wd.min[1]);
			waterTransform.getMatrix()(2,3)=SurfaceRenderer::Scalar(-0.01);
			waterTransform.leftMultiply(Geometry::invert(waterTable->getBaseTransform()));
			waterRenderer=new SurfaceRenderer(wtSize,waterTransform,basePlane);
			waterRenderer->setUsePreboundDepthTexture(true);
			waterRenderer->setUseHeightMap(false);
			waterRenderer->setDrawContourLines(false);
			waterRenderer->setIlluminate(true);
			}
		else
			{
			surfaceRenderer->setWaterTable(waterTable);
			surfaceRenderer->setAdvectWaterTexture(true);
			surfaceRenderer->setWaterOpacity(waterOpacity);
			}
		}
	
	#if 0
	/* Create a fixed-position light source: */
	sun=Vrui::getLightsourceManager()->createLightsource(true);
	for(int i=0;i<Vrui::getNumViewers();++i)
		Vrui::getViewer(i)->setHeadlightState(false);
	sun->enable();
	sun->getLight().position=GLLight::Position(1,0,1,0);
	#endif
	
	/* Create the GUI: */
	mainMenu=createMainMenu();
	Vrui::setMainMenu(mainMenu);
	if(waterTable!=0)
		waterControlDialog=createWaterControlDialog();
	
	if(controlPipeName!=0)
		{
		/* Open the control pipe in non-blocking mode: */
		controlPipeFd=open(controlPipeName,O_RDONLY|O_NONBLOCK);
		if(controlPipeFd<0)
			std::cerr<<"Unable to open control pipe "<<controlPipeName<<"; ignoring"<<std::endl;
		}
	
	/* Initialize the navigation transformation: */
	Vrui::Point::AffineCombiner cc;
	for(int i=0;i<4;++i)
		cc.addPoint(Vrui::Point(basePlane.project(basePlaneCorners[i])));
	Vrui::Point c=cc.getPoint();
	Vrui::Scalar maxDist(0);
	for(int i=0;i<4;++i)
		{
		Vrui::Scalar dist=Geometry::dist(Vrui::Point(basePlane.project(basePlaneCorners[i])),c);
		if(maxDist<dist)
			maxDist=dist;
		}
	Vrui::setNavigationTransformation(c,maxDist,Geometry::normal(Vrui::Vector(basePlane.getNormal())));
	}

Sandbox::~Sandbox(void)
	{
	/* Stop streaming depth frames: */
	camera->stopStreaming();
	delete camera;
	delete frameFilter;
	
	/* Delete helper objects: */
	delete surfaceRenderer;
	delete waterTable;
	delete rmFrameFilter;
	delete rainMaker;
	delete addWaterFunction;
	delete waterRenderer;
	
	delete mainMenu;
	delete waterControlDialog;
	
	close(controlPipeFd);
	}

void Sandbox::frame(void)
	{
	/* Check if the filtered frame has been updated: */
	if(filteredFrames.lockNewValue())
		{
		/* Update the surface renderer's depth image: */
		surfaceRenderer->setDepthImage(filteredFrames.getLockedValue());
		}
	
	/* Lock the most recent rain object list: */
	rainObjects.lockNewValue();
	#if 0
	bool registerWaterFunction=!rainObjects.getLockedValue().empty();
	if(addWaterFunctionRegistered!=registerWaterFunction)
		{
		if(registerWaterFunction)
			waterTable->addRenderFunction(addWaterFunction);
		else
			waterTable->removeRenderFunction(addWaterFunction);
		addWaterFunctionRegistered=registerWaterFunction;
		}
	#endif
	
	/* Update the surface renderer: */
	surfaceRenderer->setAnimationTime(Vrui::getApplicationTime());
	
	/* Check if there is a control command on the control pipe: */
	if(controlPipeFd>=0)
		{
		/* Try reading a chunk of data (will fail with EAGAIN if no data due to non-blocking access): */
		char command[1024];
		ssize_t readResult=read(controlPipeFd,command,sizeof(command)-1);
		if(readResult>0)
			{
			command[readResult]='\0';
			
			/* Extract the command: */
			char* cPtr;
			for(cPtr=command;*cPtr!='\0'&&!isspace(*cPtr);++cPtr)
				;
			char* commandEnd=cPtr;
			
			/* Find the beginning of an optional command parameter: */
			while(*cPtr!='\0'&&isspace(*cPtr))
				++cPtr;
			char* parameter=cPtr;
			
			/* Find the end of the optional parameter list: */
			while(*cPtr!='\0')
				++cPtr;
			while(cPtr>parameter&&isspace(cPtr[-1]))
				--cPtr;
			*cPtr='\0';
			
			/* Parse the command: */
			*commandEnd='\0';
			if(strcasecmp(command,"waterSpeed")==0)
				{
				waterSpeed=atof(parameter);
				if(waterSpeedSlider!=0)
					waterSpeedSlider->setValue(waterSpeed);
				}
			else if(strcasecmp(command,"waterMaxSteps")==0)
				{
				waterMaxSteps=atoi(parameter);
				if(waterMaxStepsSlider!=0)
					waterMaxStepsSlider->setValue(waterMaxSteps);
				}
			else if(strcasecmp(command,"waterAttenuation")==0)
				{
				double attenuation=atof(parameter);
				if(waterTable!=0)
					waterTable->setAttenuation(GLfloat(1.0-attenuation));
				if(waterAttenuationSlider!=0)
					waterAttenuationSlider->setValue(attenuation);
				}
			else if(strcasecmp(command,"colorMap")==0)
				{
				try
					{
					if(!loadHeightColorMap(parameter))
						std::cerr<<"Format error in height color map "<<parameter<<std::endl;
					}
				catch(std::runtime_error err)
					{
					std::cerr<<"Cannot read height color map "<<parameter<<" due to exception "<<err.what()<<std::endl;
					}
				}
			}
		}
	
	if(frameRateTextField!=0&&Vrui::getWidgetManager()->isVisible(waterControlDialog))
		{
		/* Update the frame rate display: */
		frameRateTextField->setValue(1.0/Vrui::getCurrentFrameTime());
		}
	
	if(pauseUpdates)
		Vrui::scheduleUpdate(Vrui::getApplicationTime()+1.0/30.0);
	}

void Sandbox::display(GLContextData& contextData) const
	{
	/* Get the data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
	if(waterTable!=0)
		{
		/* Update the water table's bathymetry: */
		waterTable->updateBathymetry(*surfaceRenderer,contextData);
		
		/* Run the water flow simulation's second pass: */
		GLfloat totalTimeStep=GLfloat(Vrui::getFrameTime()*waterSpeed);
		unsigned int numSteps=0;
		while(numSteps<waterMaxSteps&&totalTimeStep>1.0e-8f)
			{
			/* Run with a self-determined time step to maintain stability: */
			waterTable->setMaxStepSize(totalTimeStep);
			GLfloat timeStep=waterTable->runSimulationStep(contextData);
			totalTimeStep-=timeStep;
			++numSteps;
			}
		// if(totalTimeStep>1.0e-8f)
		//	std::cout<<"Ran out of time by "<<totalTimeStep<<std::endl;
		}
	
	if(fixProjectorView)
		{
		/* Install the projector transformation: */
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadMatrix(projectorTransform);
		glMultMatrix(Geometry::invert(Vrui::getDisplayState(contextData).modelviewNavigational));
		glMatrixMode(GL_MODELVIEW);
		}
	
	/* Check if the height color map texture is outdated: */
	if(dataItem->heightColorMapVersion!=heightMapVersion)
		{
		/* Upload the height color map as a 1D texture: */
		glBindTexture(GL_TEXTURE_1D,dataItem->heightColorMapObject);
		glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
		glTexImage1D(GL_TEXTURE_1D,0,GL_RGB8,heightMap.getNumEntries(),0,GL_RGBA,GL_FLOAT,heightMap.getColors());
		glBindTexture(GL_TEXTURE_1D,0);
		
		dataItem->heightColorMapVersion=heightMapVersion;
		}
	
	if(hillshade)
		{
		/* Set the surface material: */
		glMaterial(GLMaterialEnums::FRONT,surfaceMaterial);
		
		if(useShadows)
			{
			/* Set up OpenGL state: */
			glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_ENABLE_BIT|GL_POLYGON_BIT);
			
			GLLightTracker& lt=*contextData.getLightTracker();
			
			/* Save the currently-bound frame buffer and viewport: */
			GLint currentFrameBuffer;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
			GLint currentViewport[4];
			glGetIntegerv(GL_VIEWPORT,currentViewport);
			
			/*******************************************************************
			First rendering pass: Global ambient illumination only
			*******************************************************************/
			
			/* Draw the surface mesh: */
			surfaceRenderer->glRenderGlobalAmbientHeightMap(dataItem->heightColorMapObject,contextData);
			
			/*******************************************************************
			Second rendering pass: Add local illumination for every light source
			*******************************************************************/
			
			/* Enable additive rendering: */
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE,GL_ONE);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
			
			for(int lightSourceIndex=0;lightSourceIndex<lt.getMaxNumLights();++lightSourceIndex)
				if(lt.getLightState(lightSourceIndex).isEnabled())
					{
					/***************************************************************
					First step: Render to the light source's shadow map
					***************************************************************/
					
					/* Set up OpenGL state to render to the shadow map: */
					glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->shadowFramebufferObject);
					glViewport(0,0,dataItem->shadowBufferSize[0],dataItem->shadowBufferSize[1]);
					glDepthMask(GL_TRUE);
					glClear(GL_DEPTH_BUFFER_BIT);
					glCullFace(GL_FRONT);
					
					/*************************************************************
					Calculate the shadow projection matrix:
					*************************************************************/
					
					/* Get the light source position in eye space: */
					Geometry::HVector<float,3> lightPosEc;
					glGetLightfv(GL_LIGHT0+lightSourceIndex,GL_POSITION,lightPosEc.getComponents());
					
					/* Transform the light source position to camera space: */
					Vrui::ONTransform::HVector lightPosCc=Vrui::getDisplayState(contextData).modelviewNavigational.inverseTransform(Vrui::ONTransform::HVector(lightPosEc));
					
					/* Calculate the direction vector from the center of the bounding box to the light source: */
					Vrui::Point bboxCenter=Geometry::mid(bbox.min,bbox.max);
					Vrui::Vector lightDirCc=Vrui::Vector(lightPosCc.getComponents())-Vrui::Vector(bboxCenter.getComponents())*lightPosCc[3];
					
					/* Build a transformation that aligns the light direction with the positive z axis: */
					Vrui::ONTransform shadowModelview=Vrui::ONTransform::rotate(Vrui::Rotation::rotateFromTo(lightDirCc,Vrui::Vector(0,0,1)));
					shadowModelview*=Vrui::ONTransform::translateToOriginFrom(bboxCenter);
					
					/* Create a projection matrix, based on whether the light is positional or directional: */
					PTransform shadowProjection(0.0);
					if(lightPosEc[3]!=0.0f)
						{
						/* Modify the modelview transformation such that the light source is at the origin: */
						shadowModelview.leftMultiply(Vrui::ONTransform::translate(Vrui::Vector(0,0,-lightDirCc.mag())));
						
						/***********************************************************
						Create a perspective projection:
						***********************************************************/
						
						/* Calculate the perspective bounding box of the surface bounding box in eye space: */
						Box pBox=Box::empty;
						for(int i=0;i<8;++i)
							{
							Vrui::Point bc=shadowModelview.transform(bbox.getVertex(i));
							pBox.addPoint(Vrui::Point(-bc[0]/bc[2],-bc[1]/bc[2],-bc[2]));
							}
						
						/* Upload the frustum matrix: */
						double l=pBox.min[0]*pBox.min[2];
						double r=pBox.max[0]*pBox.min[2];
						double b=pBox.min[1]*pBox.min[2];
						double t=pBox.max[1]*pBox.min[2];
						double n=pBox.min[2];
						double f=pBox.max[2];
						shadowProjection.getMatrix()(0,0)=2.0*n/(r-l);
						shadowProjection.getMatrix()(0,2)=(r+l)/(r-l);
						shadowProjection.getMatrix()(1,1)=2.0*n/(t-b);
						shadowProjection.getMatrix()(1,2)=(t+b)/(t-b);
						shadowProjection.getMatrix()(2,2)=-(f+n)/(f-n);
						shadowProjection.getMatrix()(2,3)=-2.0*f*n/(f-n);
						shadowProjection.getMatrix()(3,2)=-1.0;
						}
					else
						{
						/***********************************************************
						Create a perspective projection:
						***********************************************************/
						
						/* Transform the bounding box with the modelview transformation: */
						Box bboxEc=bbox;
						bboxEc.transform(shadowModelview);
						
						/* Upload the ortho matrix: */
						double l=bboxEc.min[0];
						double r=bboxEc.max[0];
						double b=bboxEc.min[1];
						double t=bboxEc.max[1];
						double n=-bboxEc.max[2];
						double f=-bboxEc.min[2];
						shadowProjection.getMatrix()(0,0)=2.0/(r-l);
						shadowProjection.getMatrix()(0,3)=-(r+l)/(r-l);
						shadowProjection.getMatrix()(1,1)=2.0/(t-b);
						shadowProjection.getMatrix()(1,3)=-(t+b)/(t-b);
						shadowProjection.getMatrix()(2,2)=-2.0/(f-n);
						shadowProjection.getMatrix()(2,3)=-(f+n)/(f-n);
						shadowProjection.getMatrix()(3,3)=1.0;
						}
					
					/* Multiply the shadow modelview matrix onto the shadow projection matrix: */
					shadowProjection*=shadowModelview;
					
					/* Draw the surface into the shadow buffer: */
					surfaceRenderer->glRenderDepthOnly(shadowProjection,contextData);
					
					/* Reset OpenGL state: */
					glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
					glViewport(currentViewport[0],currentViewport[1],currentViewport[2],currentViewport[3]);
					glCullFace(GL_BACK);
					glDepthMask(GL_FALSE);
					
					#if SAVEDEPTH
					/* Save the depth image: */
					{
					glBindTexture(GL_TEXTURE_2D,dataItem->shadowDepthTextureObject);
					GLfloat* depthTextureImage=new GLfloat[dataItem->shadowBufferSize[1]*dataItem->shadowBufferSize[0]];
					glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,depthTextureImage);
					glBindTexture(GL_TEXTURE_2D,0);
					Images::RGBImage dti(dataItem->shadowBufferSize[0],dataItem->shadowBufferSize[1]);
					GLfloat* dtiPtr=depthTextureImage;
					Images::RGBImage::Color* ciPtr=dti.modifyPixels();
					for(int y=0;y<dataItem->shadowBufferSize[1];++y)
						for(int x=0;x<dataItem->shadowBufferSize[0];++x,++dtiPtr,++ciPtr)
							{
							GLColor<GLfloat,3> tc(*dtiPtr,*dtiPtr,*dtiPtr);
							*ciPtr=tc;
							}
					delete[] depthTextureImage;
					Images::writeImageFile(dti,"DepthImage.png");
					}
					#endif
					
					/* Draw the surface using the shadow texture: */
					surfaceRenderer->glRenderShadowedIlluminatedHeightMap(dataItem->heightColorMapObject,dataItem->shadowDepthTextureObject,shadowProjection,contextData);
					}
			
			/* Reset OpenGL state: */
			glPopAttrib();
			}
		else
			{
			/* Render the illuminated surface with height map: */
			surfaceRenderer->glRenderSinglePass(dataItem->heightColorMapObject,contextData);
			}
		}
	else
		{
		/* Render the surface with height map: */
		surfaceRenderer->glRenderSinglePass(dataItem->heightColorMapObject,contextData);
		}
	
	if(waterRenderer!=0)
		{
		/* Bind the water surface texture: */
		waterTable->bindQuantityTexture(contextData);
		
		/* Draw the water surface: */
		glMaterialAmbientAndDiffuse(GLMaterialEnums::FRONT,GLColor<GLfloat,4>(0.4f,0.5f,0.8f));
		waterRenderer->glRenderSinglePass(0,contextData);
		}
	
	if(fixProjectorView)
		{
		/* Go back to regular navigation space: */
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		}
	}

void Sandbox::eventCallback(Vrui::Application::EventID eventId,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	if(cbData->newButtonState)
		{
		switch(eventId)
			{
			case 0:
				/* Invert the current pause setting: */
				pauseUpdates=!pauseUpdates;
				
				/* Update the main menu toggle: */
				pauseUpdatesToggle->setToggle(pauseUpdates);
				
				break;
			}
		}
	}

void Sandbox::initContext(GLContextData& contextData) const
	{
	/* Create a data item and add it to the context: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	/* Create the height color map texture object: */
	glGenTextures(1,&dataItem->heightColorMapObject);
	
	{
	/* Save the currently bound frame buffer: */
	GLint currentFrameBuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT,&currentFrameBuffer);
	
	/* Set the default shadow buffer size: */
	dataItem->shadowBufferSize[0]=1024;
	dataItem->shadowBufferSize[1]=1024;
	
	/* Generate the shadow rendering frame buffer: */
	glGenFramebuffersEXT(1,&dataItem->shadowFramebufferObject);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,dataItem->shadowFramebufferObject);
	
	/* Generate a depth texture for shadow rendering: */
	glGenTextures(1,&dataItem->shadowDepthTextureObject);
	glBindTexture(GL_TEXTURE_2D,dataItem->shadowDepthTextureObject);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE_ARB,GL_COMPARE_R_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC_ARB,GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_TEXTURE_MODE_ARB,GL_INTENSITY);
	glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24_ARB,dataItem->shadowBufferSize[0],dataItem->shadowBufferSize[1],0,GL_DEPTH_COMPONENT,GL_UNSIGNED_BYTE,0);
	glBindTexture(GL_TEXTURE_2D,0);
	
	/* Attach the depth texture to the frame buffer object: */
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,dataItem->shadowDepthTextureObject,0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,currentFrameBuffer);
	} 
	}

VRUI_APPLICATION_RUN(Sandbox)
