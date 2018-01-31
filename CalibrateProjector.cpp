/***********************************************************************
CalibrateProjector - Utility to calculate the calibration transformation
of a projector into a Kinect-captured 3D space.
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

#include "CalibrateProjector.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <Misc/FunctionCalls.h>
#include <IO/ValueSource.h>
#include <IO/CSVSource.h>
#include <IO/File.h>
#include <IO/OpenFile.h>
#include <Cluster/OpenPipe.h>
#include <Math/Math.h>
#include <Math/Constants.h>
#include <Math/Interval.h>
#include <Geometry/GeometryValueCoders.h>
#include <GL/gl.h>
#include <GL/GLGeometryWrappers.h>
#include <GL/GLTransformationWrappers.h>
#include <Vrui/Vrui.h>
#include <Vrui/VRScreen.h>
#include <Vrui/ToolManager.h>
#include <Vrui/DisplayState.h>
#include <Vrui/OpenFile.h>
#include <Kinect/DirectFrameSource.h>
#include <Kinect/OpenDirectFrameSource.h>
#include <Kinect/Camera.h>
#include <Kinect/MultiplexedFrameSource.h>

#include "Config.h"

/********************************************************
Static elements of class CalibrateProjector::CaptureTool:
********************************************************/

CalibrateProjector::CaptureToolFactory* CalibrateProjector::CaptureTool::factory=0;

/************************************************
Methods of class CalibrateProjector::CaptureTool:
************************************************/

CalibrateProjector::CaptureTool::CaptureTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment)
	{
	}

CalibrateProjector::CaptureTool::~CaptureTool(void)
	{
	}

const Vrui::ToolFactory* CalibrateProjector::CaptureTool::getFactory(void) const
	{
	return factory;
	}

void CalibrateProjector::CaptureTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	/* Start capturing a depth frame if the button was just pressed: */
	if(cbData->newButtonState)
		{
		if(buttonSlotIndex==0)
			application->startTiePointCapture();
		else
			application->startBackgroundCapture();
		}
	}

/***********************************
Methods of class CalibrateProjector:
***********************************/

void CalibrateProjector::depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer)
	{
	/* Forward depth frame to the sphere extractor: */
	diskExtractor->submitFrame(frameBuffer);
	
	/* Forward depth frame to the projector: */
	projector->setDepthFrame(frameBuffer);
	
	#if KINECT_CONFIG_USE_SHADERPROJECTOR
	/* Update application state: */
	Vrui::requestUpdate();
	#endif
	}

#if !KINECT_CONFIG_USE_SHADERPROJECTOR

void CalibrateProjector::meshStreamingCallback(const Kinect::MeshBuffer& meshBuffer)
	{
	/* Update application state: */
	Vrui::requestUpdate();
	}

#endif

void CalibrateProjector::backgroundCaptureCompleteCallback(Kinect::DirectFrameSource&)
	{
	/* Reset the background capture flag: */
	std::cout<<" done"<<std::endl;
	capturingBackground=false;
	
	/* Enable background removal: */
	dynamic_cast<Kinect::DirectFrameSource*>(camera)->setRemoveBackground(true);
	
	/* Wake up the foreground thread: */
	Vrui::requestUpdate();
	}

void CalibrateProjector::diskExtractionCallback(const Kinect::DiskExtractor::DiskList& disks)
	{
	/* Store the new disk list in the triple buffer: */
	Kinect::DiskExtractor::DiskList& newList=diskList.startNewValue();
	newList=disks;
	diskList.postNewValue();
	
	/* Wake up the main thread: */
	Vrui::requestUpdate();
	}

CalibrateProjector::CalibrateProjector(int& argc,char**& argv)
	:Vrui::Application(argc,argv),
	 numTiePointFrames(60),numBackgroundFrames(120),
	 camera(0),diskExtractor(0),projector(0),
	 capturingBackground(false),capturingTiePoint(false),numCaptureFrames(0),
	 tiePointIndex(0),
	 haveProjection(false),projection(4,4)
	{
	/* Register the custom tool class: */
	CaptureToolFactory* toolFactory1=new CaptureToolFactory("CaptureTool","Capture",0,*Vrui::getToolManager());
	toolFactory1->setNumButtons(2);
	toolFactory1->setButtonFunction(0,"Capture Tie Point");
	toolFactory1->setButtonFunction(1,"Capture Background");
	Vrui::getToolManager()->addClass(toolFactory1,Vrui::ToolManager::defaultToolFactoryDestructor);
	
	/* Process command line parameters: */
	bool printHelp=false;
	std::string sandboxLayoutFileName=CONFIG_CONFIGDIR;
	sandboxLayoutFileName.push_back('/');
	sandboxLayoutFileName.append(CONFIG_DEFAULTBOXLAYOUTFILENAME);
	projectionMatrixFileName=CONFIG_CONFIGDIR;
	projectionMatrixFileName.push_back('/');
	projectionMatrixFileName.append(CONFIG_DEFAULTPROJECTIONMATRIXFILENAME);
	Kinect::MultiplexedFrameSource* remoteSource=0;
	int cameraIndex=0;
	imageSize[0]=1024;
	imageSize[1]=768;
	numTiePoints[0]=4;
	numTiePoints[1]=3;
	int blobMergeDepth=2;
	const char* tiePointFileName=0;
	for(int i=1;i<argc;++i)
		{
		if(argv[i][0]=='-')
			{
			if(strcasecmp(argv[i]+1,"h")==0)
				printHelp=true;
			else if(strcasecmp(argv[i]+1,"slf")==0)
				{
				++i;
				if(i<argc)
					sandboxLayoutFileName=argv[i];
				}
			else if(strcasecmp(argv[i]+1,"r")==0)
				{
				i+=2;
				if(i<argc)
					{
					/* Open a connection to a remote Kinect server: */
					remoteSource=Kinect::MultiplexedFrameSource::create(Cluster::openTCPPipe(Vrui::getClusterMultiplexer(),argv[i-1],atoi(argv[i])));
					}
				}
			else if(strcasecmp(argv[i]+1,"c")==0)
				{
				++i;
				if(i<argc)
					cameraIndex=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"s")==0)
				{
				if(i+2<argc)
					{
					for(int j=0;j<2;++j)
						{
						++i;
						imageSize[j]=atoi(argv[i]);
						}
					}
				}
			else if(strcasecmp(argv[i]+1,"tp")==0)
				{
				if(i+2<argc)
					{
					for(int j=0;j<2;++j)
						{
						++i;
						numTiePoints[j]=atoi(argv[i]);
						}
					}
				}
			else if(strcasecmp(argv[i]+1,"bmd")==0)
				{
				++i;
				if(i<argc)
					blobMergeDepth=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"tpf")==0)
				{
				++i;
				if(i<argc)
					tiePointFileName=argv[i];
				}
			else if(strcasecmp(argv[i]+1,"pmf")==0)
				{
				++i;
				if(i<argc)
					projectionMatrixFileName=argv[i];
				}
			}
		}
	
	if(printHelp)
		{
		std::cout<<"Usage: CalibrateProjector [option 1] ... [option n]"<<std::endl;
		std::cout<<"  Options:"<<std::endl;
		std::cout<<"  -h"<<std::endl;
		std::cout<<"     Prints this help message"<<std::endl;
		std::cout<<"  -slf <sandbox layout file name>"<<std::endl;
		std::cout<<"     Loads the sandbox layout file of the given name"<<std::endl;
		std::cout<<"     Default: "<<CONFIG_CONFIGDIR<<'/'<<CONFIG_DEFAULTBOXLAYOUTFILENAME<<std::endl;
		std::cout<<"  -r <server host name> <server port number>"<<std::endl;
		std::cout<<"     Connects to a remote 3D video server on the given host name /"<<std::endl;
		std::cout<<"     port number"<<std::endl;
		std::cout<<"     Default: <empty>"<<std::endl;
		std::cout<<"  -c <camera index>"<<std::endl;
		std::cout<<"     Selects the 3D camera of the given index on the local USB bus or"<<std::endl;
		std::cout<<"     on the remote 3D video server (0: first camera)"<<std::endl;
		std::cout<<"     Default: 0"<<std::endl;
		std::cout<<"  -s <projector image width> <projector image height>"<<std::endl;
		std::cout<<"     Sets the width and height of the projector image in pixels. This"<<std::endl;
		std::cout<<"     must match the actual resolution of the projector."<<std::endl;
		std::cout<<"     Default: 1024 768"<<std::endl;
		std::cout<<"  -tp <grid width> <grid height>"<<std::endl;
		std::cout<<"     Sets the number of tie points to be collected before a calibration"<<std::endl;
		std::cout<<"     is computed."<<std::endl;
		std::cout<<"     Default: 4 3"<<std::endl;
		std::cout<<"  -bmd <mamximum blob merge depth distance>"<<std::endl;
		std::cout<<"     Maximum depth distance between adjacent pixels in the same blob."<<std::endl;
		std::cout<<"     Default: 1"<<std::endl;
		std::cout<<"  -tpf <tie point file name>"<<std::endl;
		std::cout<<"     Reads initial calibration tie points from a CSV file"<<std::endl;
		std::cout<<"  -pmf <projection matrix file name>"<<std::endl;
		std::cout<<"     Saves the calibration matrix to the file of the given name"<<std::endl;
		std::cout<<"     Default: "<<CONFIG_CONFIGDIR<<'/'<<CONFIG_DEFAULTPROJECTIONMATRIXFILENAME<<std::endl;
		}
	
	/* Read the sandbox layout file: */
	{
	IO::ValueSource layoutSource(Vrui::openFile(sandboxLayoutFileName.c_str()));
	layoutSource.skipWs();
	std::string s=layoutSource.readLine();
	basePlane=Misc::ValueCoder<OPlane>::decode(s.c_str(),s.c_str()+s.length());
	basePlane.normalize();
	for(int i=0;i<4;++i)
		{
		layoutSource.skipWs();
		s=layoutSource.readLine();
		basePlaneCorners[i]=basePlane.project(Misc::ValueCoder<OPoint>::decode(s.c_str(),s.c_str()+s.length()));
		}
	}
	
	/* Calculate the transformation from camera space to sandbox space: */
	{
	ONTransform::Vector z=basePlane.getNormal();
	ONTransform::Vector x=(basePlaneCorners[1]-basePlaneCorners[0])+(basePlaneCorners[3]-basePlaneCorners[2]);
	x.orthogonalize(z);
	ONTransform::Vector y=z^x;
	boxTransform=ONTransform::rotate(Geometry::invert(ONTransform::Rotation::fromBaseVectors(x,y)));
	ONTransform::Point center=Geometry::mid(Geometry::mid(basePlaneCorners[0],basePlaneCorners[1]),Geometry::mid(basePlaneCorners[2],basePlaneCorners[3]));
	boxTransform*=ONTransform::translateToOriginFrom(basePlane.project(center));
	}
	
	/* Calculate a bounding box around the sandbox area: */
	bbox=Box::empty;
	for(int i=0;i<4;++i)
		bbox.addPoint(boxTransform.transform(basePlaneCorners[i]));
	
	if(tiePointFileName!=0)
		{
		/* Read the tie point file: */
		IO::CSVSource tiePointFile(IO::openFile(tiePointFileName));
		while(!tiePointFile.eof())
			{
			/* Read the tie point: */
			TiePoint tp;
			for(int i=0;i<2;++i)
				tp.p[i]=tiePointFile.readField<double>();
			for(int i=0;i<3;++i)
				tp.o[i]=tiePointFile.readField<double>();
			
			tiePoints.push_back(tp);
			}
		
		if(tiePoints.size()>=size_t(numTiePoints[0]*numTiePoints[1]))
			{
			/* Calculate an initial calibration: */
			calcCalibration();
			}
		}
	
	/* Open the requested 3D video source: */
	if(remoteSource!=0)
		{
		/* Open the camera of selected index on the remote server: */
		camera=remoteSource->getStream(cameraIndex);
		}
	else
		{
		/* Open the camera of selected index on the local USB bus: */
		Kinect::DirectFrameSource* directCamera=Kinect::openDirectFrameSource(cameraIndex);
		camera=directCamera;
		
		/* Set some camera type-specific parameters: */
		directCamera->setBackgroundRemovalFuzz(1);
		
		/* Check if the camera is a first-generation Kinect: */
		Kinect::Camera* kinectV1=dynamic_cast<Kinect::Camera*>(directCamera);
		if(kinectV1!=0)
			{
			/* Set Kinect v1-specific parameters: */
			kinectV1->setCompressDepthFrames(true);
			kinectV1->setSmoothDepthFrames(false);
			}
		}
	
	/* Create a disk extractor for the 3D video source: */
	diskExtractor=new Kinect::DiskExtractor(camera->getActualFrameSize(Kinect::FrameSource::DEPTH),camera->getDepthCorrectionParameters(),camera->getIntrinsicParameters());
	diskExtractor->setMaxBlobMergeDist(blobMergeDepth);
	diskExtractor->setMinNumPixels(250);
	diskExtractor->setDiskRadius(6.0);
	diskExtractor->setDiskRadiusMargin(1.10);
	diskExtractor->setDiskFlatness(1.0);
	
	/* Create a projector for the 3D video source: */
	projector=new Kinect::ProjectorType(*camera);
	projector->setTriangleDepthRange(blobMergeDepth);
	
	/* Reset the projector's extrinsic parameters: */
	projector->setExtrinsicParameters(Kinect::FrameSource::ExtrinsicParameters::identity);
	
	#if KINECT_CONFIG_USE_PROJECTOR2
	
	/* Disable color mapping and illumination on the projector: */
	projector->setMapTexture(false);
	projector->setIlluminate(false);
	
	#endif
	
	/* Start streaming from the 3D video source and extracting disks: */
	diskExtractor->startStreaming(Misc::createFunctionCall(this,&CalibrateProjector::diskExtractionCallback));
	#if !KINECT_CONFIG_USE_SHADERPROJECTOR
	projector->startStreaming(Misc::createFunctionCall(this,&CalibrateProjector::meshStreamingCallback));
	#endif
	camera->startStreaming(Misc::createFunctionCall(projector,&Kinect::ProjectorType::setColorFrame),Misc::createFunctionCall(this,&CalibrateProjector::depthStreamingCallback));
	
	/* Start capturing the initial background frame: */
	startBackgroundCapture();
	}

CalibrateProjector::~CalibrateProjector(void)
	{
	/* Stop streaming from the 3D video source: */
	camera->stopStreaming();
	diskExtractor->stopStreaming();
	
	/* Clean up: */
	delete diskExtractor;
	delete projector;
	delete camera;
	}

void CalibrateProjector::frame(void)
	{
	/* Check if we are capturing a tie point and there is a new list of extracted disks: */
	if(diskList.lockNewValue()&&capturingTiePoint&&diskList.getLockedValue().size()==1)
		{
		/* Access the only extracted disk: */
		const Kinect::DiskExtractor::Disk& disk=diskList.getLockedValue().front();
		
		/* Check if there is a real disk center position: */
		bool diskValid=true;
		for(int i=0;i<3;++i)
			diskValid=diskValid&&Math::isFinite(disk.center[i]);
		
		#if 0
		
		/* Check if the disk is inside the sandbox area: */
		diskValid=diskValid&&(basePlane.getNormal()^(basePlaneCorners[1]-basePlaneCorners[0]))*(disk.center-basePlaneCorners[0])>=0.0;
		diskValid=diskValid&&(basePlane.getNormal()^(basePlaneCorners[3]-basePlaneCorners[1]))*(disk.center-basePlaneCorners[1])>=0.0;
		diskValid=diskValid&&(basePlane.getNormal()^(basePlaneCorners[2]-basePlaneCorners[3]))*(disk.center-basePlaneCorners[3])>=0.0;
		diskValid=diskValid&&(basePlane.getNormal()^(basePlaneCorners[0]-basePlaneCorners[2]))*(disk.center-basePlaneCorners[2])>=0.0;
		
		#endif
		
		if(diskValid)
			{
			/* Store the just-captured tie point: */
			TiePoint tp;
			int xIndex=tiePointIndex%numTiePoints[0];
			int yIndex=(tiePointIndex/numTiePoints[0])%numTiePoints[1];
			int x=(xIndex+1)*imageSize[0]/(numTiePoints[0]+1);
			int y=(yIndex+1)*imageSize[1]/(numTiePoints[1]+1);
			tp.p=PPoint(Scalar(x)+Scalar(0.5),Scalar(y)+Scalar(0.5));
			tp.o=disk.center;
			tiePoints.push_back(tp);
			
			/* Check if that's enough: */
			--numCaptureFrames;
			if(numCaptureFrames==0)
				{
				/* Stop capturing this tie point and move to the next: */
				std::cout<<"done"<<std::endl;
				capturingTiePoint=false;
				++tiePointIndex;
				
				/* Check if the calibration is complete: */
				if(tiePointIndex>=numTiePoints[0]*numTiePoints[1])
					{
					/* Calculate the calibration transformation: */
					calcCalibration();
					}
				}
			}
		}
	
	/* Update the projector: */
	projector->updateFrames();
	}

void CalibrateProjector::display(GLContextData& contextData) const
	{
	/* Set up OpenGL state: */
	glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glLineWidth(1.0f);
	
	if(capturingBackground)
		{
		/* Go to screen space: */
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0,double(imageSize[0]),0.0,double(imageSize[1]),-1.0,1.0);
		
		/* Indicate that a background frame is being captured: */
		glBegin(GL_QUADS);
		glColor3f(1.0f,0.0f,0.0f);
		glVertex2f(0.0f,0.0f);
		glVertex2f(float(imageSize[0]),0.0f);
		glVertex2f(float(imageSize[0]),float(imageSize[1]));
		glVertex2f(0.0f,float(imageSize[1]));
		glEnd();
		
		/* Return to navigational space: */
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		}
	else
		{
		/* Set up an orthographic projection showing the sandbox area from above: */
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		
		/* Match the sandbox area's aspect ratio against the display screen: */
		Scalar bbw=bbox.getSize(0);
		Scalar bbh=bbox.getSize(1);
		const Vrui::VRScreen* screen=Vrui::getDisplayState(contextData).screen;
		Scalar sw=screen->getWidth();
		Scalar sh=screen->getHeight();
		if(bbw*sh>=sw*bbh) // Sandbox area is wider
			{
			Scalar filler=Math::div2((bbw*sh)/sw-bbh);
			glOrtho(bbox.min[0],bbox.max[0],bbox.min[1]-filler,bbox.max[1]+filler,-200.0,200.0);
			}
		else // Sandbox area is taller
			{
			Scalar filler=Math::div2((bbh*sw)/sh-bbw);
			glOrtho(bbox.min[0]-filler,bbox.max[0]+filler,bbox.min[0],bbox.max[0],-200.0,200.0);
			}
		
		/* Transform camera space to sandbox space: */
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadMatrix(boxTransform);
		
		/* Draw the sandbox outline: */
		glBegin(GL_LINE_LOOP);
		glColor3f(1.0f,1.0f,0.0f);
		glVertex(basePlaneCorners[0]);
		glVertex(basePlaneCorners[1]);
		glVertex(basePlaneCorners[3]);
		glVertex(basePlaneCorners[2]);
		glEnd();
		
		/* Draw the current 3D video facade: */
		glColor3f(1.0f,1.0f,0.0f);
		projector->glRenderAction(contextData);
		
		/* Draw all currently extracted disks: */
		const Kinect::DiskExtractor::DiskList& dl=diskList.getLockedValue();
		for(Kinect::DiskExtractor::DiskList::const_iterator dlIt=dl.begin();dlIt!=dl.end();++dlIt)
			{
			glPushMatrix();
			glTranslate(dlIt->center-Kinect::DiskExtractor::Point::origin);
			glRotate(Vrui::Rotation::rotateFromTo(Vrui::Vector(0,0,1),Vrui::Vector(dlIt->normal)));
			
			glBegin(GL_POLYGON);
			glColor3f(0.0f,1.0f,0.0f);
			for(int i=0;i<64;++i)
				{
				Vrui::Scalar angle=Vrui::Scalar(i)*Vrui::Scalar(2)*Math::Constants<Vrui::Scalar>::pi/Vrui::Scalar(64);
				glVertex3d(Math::cos(angle)*dlIt->radius,Math::sin(angle)*dlIt->radius,0.0);
				}
			glEnd();
			
			glPopMatrix();
			}
		
		/* Go to screen space: */
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0,double(imageSize[0]),0.0,double(imageSize[1]),-1.0,1.0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		
		/* Calculate the screen-space position of the next tie point: */
		int xIndex=tiePointIndex%numTiePoints[0];
		int yIndex=(tiePointIndex/numTiePoints[0])%numTiePoints[1];
		int x=(xIndex+1)*imageSize[0]/(numTiePoints[0]+1);
		int y=(yIndex+1)*imageSize[1]/(numTiePoints[1]+1);
		
		/* Draw the next tie point: */
		glBegin(GL_LINES);
		glColor3f(1.0f,1.0f,1.0f);
		glVertex2f(0.0f,float(y)+0.5f);
		glVertex2f(float(imageSize[0]),float(y)+0.5f);
		glVertex2f(float(x)+0.5f,0.0f);
		glVertex2f(float(x)+0.5f,float(imageSize[1]));
		glEnd();
		
		if(haveProjection)
			{
			/* Draw all currently extracted disks using the current calibration: */
			for(Kinect::DiskExtractor::DiskList::const_iterator dlIt=dl.begin();dlIt!=dl.end();++dlIt)
				{
				Math::Matrix blob(4,1);
				for(int i=0;i<3;++i)
					blob(i)=dlIt->center[i];
				blob(3)=1.0;
				Math::Matrix projBlob=projection*blob;
				double x=(projBlob(0)/projBlob(3)+1.0)*double(imageSize[0])/2.0;
				double y=(projBlob(1)/projBlob(3)+1.0)*double(imageSize[1])/2.0;
				glBegin(GL_LINES);
				glColor3f(1.0f,0.0f,0.0f);
				glVertex2d(x,0.0);
				glVertex2d(x,double(imageSize[1]));
				glVertex2d(0.0,y);
				glVertex2d(double(imageSize[0]),y);
				glEnd();
				}
			}
		
		/* Return to navigational space: */
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		}
	
	glPopAttrib();
	}

void CalibrateProjector::startBackgroundCapture(void)
	{
	/* Bail out if already capturing a tie point or background: */
	if(capturingBackground||capturingTiePoint)
		return;
	
	/* Check if this is a directly-connected 3D camera: */
	Kinect::DirectFrameSource* directCamera=dynamic_cast<Kinect::DirectFrameSource*>(camera);
	if(directCamera!=0)
		{
		/* Tell the 3D camera to capture a new background frame: */
		capturingBackground=true;
		std::cout<<"CalibrateProjector: Capturing "<<numBackgroundFrames<<" background frames..."<<std::flush;
		directCamera->captureBackground(numBackgroundFrames,true,Misc::createFunctionCall(this,&CalibrateProjector::backgroundCaptureCompleteCallback));
		}
	}

void CalibrateProjector::startTiePointCapture(void)
	{
	/* Bail out if already capturing a tie point or background: */
	if(capturingBackground||capturingTiePoint)
		return;
	
	/* Start capturing a new tie point: */
	capturingTiePoint=true;
	numCaptureFrames=numTiePointFrames;
	std::cout<<"CalibrateProjector: Capturing "<<numTiePointFrames<<" tie point frames..."<<std::flush;
	}

void CalibrateProjector::calcCalibration(void)
	{
	/* Create the least-squares system: */
	Math::Matrix a(12,12,0.0);
	
	/* Process all tie points: */
	for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
		{
		// DEBUGGING
		// std::cout<<"Tie point: "<<tpIt->p[0]<<", "<<tpIt->p[1]<<", "<<tpIt->o[0]<<", "<<tpIt->o[1]<<", "<<tpIt->o[2]<<std::endl;
		
		/* Create the tie point's associated two linear equations: */
		double eq[2][12];
		eq[0][0]=tpIt->o[0];
		eq[0][1]=tpIt->o[1];
		eq[0][2]=tpIt->o[2];
		eq[0][3]=1.0;
		eq[0][4]=0.0;
		eq[0][5]=0.0;
		eq[0][6]=0.0;
		eq[0][7]=0.0;
		eq[0][8]=-tpIt->p[0]*tpIt->o[0];
		eq[0][9]=-tpIt->p[0]*tpIt->o[1];
		eq[0][10]=-tpIt->p[0]*tpIt->o[2];
		eq[0][11]=-tpIt->p[0];
		
		eq[1][0]=0.0;
		eq[1][1]=0.0;
		eq[1][2]=0.0;
		eq[1][3]=0.0;
		eq[1][4]=tpIt->o[0];
		eq[1][5]=tpIt->o[1];
		eq[1][6]=tpIt->o[2];
		eq[1][7]=1.0;
		eq[1][8]=-tpIt->p[1]*tpIt->o[0];
		eq[1][9]=-tpIt->p[1]*tpIt->o[1];
		eq[1][10]=-tpIt->p[1]*tpIt->o[2];
		eq[1][11]=-tpIt->p[1];
		
		/* Insert the two equations into the least-squares system: */
		for(int row=0;row<2;++row)
			{
			for(unsigned int i=0;i<12;++i)
				for(unsigned int j=0;j<12;++j)
					a(i,j)+=eq[row][i]*eq[row][j];
			}
		}
	
	/* Find the least square system's smallest eigenvalue: */
	std::pair<Math::Matrix,Math::Matrix> qe=a.jacobiIteration();
	unsigned int minEIndex=0;
	double minE=Math::abs(qe.second(0,0));
	for(unsigned int i=1;i<12;++i)
		{
		if(minE>Math::abs(qe.second(i,0)))
			{
			minEIndex=i;
			minE=Math::abs(qe.second(i,0));
			}
		}
	
	/* Create the initial unscaled homography: */
	Math::Matrix hom(3,4);
	for(int i=0;i<3;++i)
		for(int j=0;j<4;++j)
			hom(i,j)=qe.first(i*4+j,minEIndex);
	
	/* Scale the homography such that projected weights are positive distance from projector: */
	double wLen=Math::sqrt(Math::sqr(hom(2,0))+Math::sqr(hom(2,1))+Math::sqr(hom(2,2)));
	int numNegativeWeights=0;
	for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
		{
		/* Calculate the object-space tie point's projected weight: */
		double w=hom(2,3);
		for(int j=0;j<3;++j)
			w+=hom(2,j)*tpIt->o[j];
		if(w<0.0)
			++numNegativeWeights;
		}
	if(numNegativeWeights==0||numNegativeWeights==int(tiePoints.size()))
		{
		/* Scale the homography: */
		if(numNegativeWeights>0)
			wLen=-wLen;
		for(int i=0;i<3;++i)
			for(int j=0;j<4;++j)
				hom(i,j)/=wLen;
		
		/* Print the scaled homography: */
		for(int i=0;i<3;++i)
			{
			std::cout<<std::setw(10)<<hom(i,0);
			for(int j=1;j<4;++j)
				std::cout<<"   "<<std::setw(10)<<hom(i,j);
			std::cout<<std::endl;
			}
		
		/* Calculate the calibration residual: */
		double res=0.0;
		for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
			{
			Math::Matrix op(4,1);
			for(int i=0;i<3;++i)
				op(i)=tpIt->o[i];
			op(3)=1.0;
			
			Math::Matrix pp=hom*op;
			for(int i=0;i<2;++i)
				pp(i)/=pp(2);
			
			res+=Math::sqr(pp(0)-tpIt->p[0])+Math::sqr(pp(1)-tpIt->p[1]);
			}
		res=Math::sqrt(res/double(tiePoints.size()));
		std::cout<<"RMS calibration residual: "<<res<<std::endl;
		
		/* Calculate the full projector projection matrix: */
		for(unsigned int i=0;i<2;++i)
			for(unsigned int j=0;j<4;++j)
				projection(i,j)=hom(i,j);
		for(unsigned int j=0;j<3;++j)
			projection(2,j)=0.0;
		projection(2,3)=-1.0;
		for(unsigned int j=0;j<4;++j)
			projection(3,j)=hom(2,j);
		
		/* Calculate the z range of all tie points: */
		Math::Interval<double> zRange=Math::Interval<double>::empty;
		int numNegativeWeights=0;
		for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
			{
			/* Transform the object-space tie point with the projection matrix: */
			Math::Matrix op(4,1);
			for(int i=0;i<3;++i)
				op(i)=double(tpIt->o[i]);
			op(3)=1.0;
			Math::Matrix pp=projection*op;
			if(pp(3)<0.0)
				++numNegativeWeights;
			zRange.addValue(pp(2)/pp(3));
			}
		std::cout<<"Z range of collected tie points: ["<<zRange.getMin()<<", "<<zRange.getMax()<<"]"<<std::endl;
		
		/* Double the size of the range to include a safety margin on either side: */
		zRange=Math::Interval<double>(zRange.getMin()*2.0,zRange.getMax()*0.5);
		
		/* Pre-multiply the projection matrix with the inverse viewport matrix to go to clip coordinates: */
		Math::Matrix invViewport(4,4,1.0);
		invViewport(0,0)=2.0/double(imageSize[0]);
		invViewport(0,3)=-1.0;
		invViewport(1,1)=2.0/double(imageSize[1]);
		invViewport(1,3)=-1.0;
		invViewport(2,2)=2.0/(zRange.getSize());
		invViewport(2,3)=-2.0*zRange.getMin()/(zRange.getSize())-1.0;
		projection=invViewport*projection;
		
		/* Write the projection matrix to a file: */
		IO::FilePtr projFile=Vrui::openFile(projectionMatrixFileName.c_str(),IO::File::WriteOnly);
		projFile->setEndianness(Misc::LittleEndian);
		for(int i=0;i<4;++i)
			for(int j=0;j<4;++j)
				projFile->write<double>(projection(i,j));
		
		haveProjection=true;
		}
	else
		std::cout<<"Calibration error: Some tie points have negative projection weights. Please start from scratch"<<std::endl;
	}

/* Create and execute an application object: */
VRUI_APPLICATION_RUN(CalibrateProjector)
