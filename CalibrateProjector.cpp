/***********************************************************************
CalibrateProjector - Utility to calculate the calibration transformation
of a projector into a Kinect-captured 3D space.
Copyright (c) 2012 Oliver Kreylos

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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <Misc/FunctionCalls.h>
#include <Misc/File.h>
#include <Threads/Mutex.h>
#include <Threads/Cond.h>
#include <Threads/TripleBuffer.h>
#include <IO/File.h>
#include <USB/Context.h>
#include <Math/Math.h>
#include <Math/Interval.h>
#include <Math/Matrix.h>
#include <Geometry/Point.h>
#include <GL/gl.h>
#include <Vrui/Vrui.h>
#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Vrui/ToolManager.h>
#include <Vrui/Application.h>
#include <Vrui/OpenFile.h>
#include <Kinect/FrameBuffer.h>
#include <Kinect/Camera.h>

#include "FindBlobs.h"

template <>
class BlobProperty<float> // Blob property class to calculate a blob's plane equation in depth image space
	{
	/* Embedded classes: */
	public:
	typedef float Pixel;
	
	/* Elements: */
	private:
	double pxs,pys,pzs; // Accumulated components of centroid
	#if 0
	double pxpxs,pxpys,pxpzs,pypys,pypzs,pzpzs; // Accumulated components of covariance matrix
	#endif
	size_t numPixels; // Number of accumulated pixels
	
	/* Constructors and destructors: */
	public:
	BlobProperty(void)
		:pxs(0.0),pys(0.0),pzs(0.0),
		 #if 0
		 pxps(0.0),pxpys(0.0),pxpzs(0.0),pypys(0.0),pypzs(0.0),pzpzs(0.0),
		 #endif
		 numPixels(0)
		{
		}
	
	/* Methods: */
	void addPixel(int x,int y,const Pixel& pixelValue)
		{
		pxs+=double(x);
		pys+=double(y);
		pzs+=double(pixelValue);
		#if 0
		pxpxs+=double(x)*double(x);
		pxpys+=double(x)*double(y);
		pxpzs+=double(x)*double(pixelValue);
		pypys+=double(y)*double(y);
		pypzs+=double(y)*double(pixelValue);
		pzpzs+=double(pixelValue)*double(pixelValue);
		#endif
		++numPixels;
		}
	void merge(const BlobProperty& other)
		{
		pxs+=other.pxs;
		pys+=other.pys;
		pzs+=other.pzs;
		#if 0
		pxpxs+=other.pxpxs;
		pxpys+=other.pxpys;
		pxpzs+=other.pxpzs;
		pypys+=other.pypys;
		pypzs+=other.pypzs;
		pzpzs+=other.pzpzs;
		#endif
		numPixels+=other.numPixels;
		}
	size_t getNumPixels(void) const
		{
		return numPixels;
		}
	Geometry::Point<double,3> calcCentroid(void) const // Returns the centroid of the blob in depth image space
		{
		return Geometry::Point<double,3>(pxs/double(numPixels),pys/double(numPixels),pzs/double(numPixels));
		}
	};

class CalibrateProjector:public Vrui::Application
	{
	/* Embedded classes: */
	private:
	class BackgroundProperty // Functor class to identify non-background pixels in averaged depth frames
		{
		/* Elements: */
		private:
		const float* backgroundFrame; // Pointer to the background frame
		
		/* Constructors and destructors: */
		public:
		BackgroundProperty(const float* sBackgroundFrame)
			:backgroundFrame(sBackgroundFrame)
			{
			}
		
		/* Methods: */
		public:
		bool operator()(int x,int y,const float& pixel) const
			{
			return pixel<backgroundFrame[y*640+x];
			}
		bool operator()(int x,int y,const unsigned short& pixel) const
			{
			return float(pixel)<backgroundFrame[y*640+x];
			}
		};
	
	typedef float Scalar; // Scalar type for points
	typedef Geometry::Point<Scalar,2> PPoint; // Point in 2D projection space
	typedef Geometry::Point<Scalar,3> OPoint; // Point in 3D object space
	
	struct TiePoint // Tie point between 3D object space and 2D projector space
		{
		/* Elements: */
		public:
		PPoint p; // Projection-space point
		OPoint o; // Object-space point
		};
	
	class CaptureTool;
	typedef Vrui::GenericToolFactory<CaptureTool> CaptureToolFactory; // Tool class uses the generic factory class
	
	class CaptureTool:public Vrui::Tool,public Vrui::Application::Tool<CalibrateProjector>
		{
		friend class Vrui::GenericToolFactory<CaptureTool>;
		
		/* Elements: */
		private:
		static CaptureToolFactory* factory; // Pointer to the factory object for this class
		
		/* Constructors and destructors: */
		public:
		CaptureTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment);
		virtual ~CaptureTool(void);
		
		/* Methods from class Vrui::Tool: */
		virtual const Vrui::ToolFactory* getFactory(void) const;
		virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
		};
	
	/* Elements: */
	private:
	USB::Context usbContext; // USB device context
	Kinect::Camera* camera; // Pointer to Kinect camera defining the object space
	bool hasDepthCorrection; // Flag whether the camera has per-pixel depth correction coefficients
	Kinect::FrameBuffer depthCorrection; // Buffer of per-pixel depth correction coefficients
	Kinect::FrameSource::IntrinsicParameters cameraIps; // Intrinsic parameters of the Kinect camera
	Threads::TripleBuffer<Kinect::FrameBuffer> rawFrames; // Triple buffer for raw depth frames from the Kinect camera
	std::vector<Blob<unsigned short> > rawBlobs; // List of foreground blobs found in the current raw depth frame
	int numCaptureFrames; // Number of frames to capture per tie point
	bool captureMin; // Flag whether to capture average or minimum pixel values
	Threads::Mutex ndfMutex; // Mutex protecting the depth frame capture counter
	Threads::Cond ndfCond; // Condition variable to signal that depth frame capture is done
	int numDepthFrames; // Number of depth frames left to average
	float* avgDepthFrame; // Buffer to average a sequence of depth frames from the Kinect camera
	int* avgDepthSum; // Number of averaged samples for each pixel in the average depth frame
	float* backgroundFrame; // Buffer holding an averaged background frame
	int imageSize[2]; // Size of projector image
	int numTiePoints[2]; // Number of tie points in x and y
	std::vector<TiePoint> tiePoints; // List of already captured tie points
	
	/* Private methods: */
	void depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer); // Callback receiving depth frames from the Kinect camera
	
	/* Constructors and destructors: */
	public:
	CalibrateProjector(int& argc,char**& argv,char**& appDefaults);
	virtual ~CalibrateProjector(void);
	
	/* Methods from Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	
	/* New methods: */
	void startCapture(void); // Starts capturing an averaged depth frame
	void addTiePoint(void); // Adds a calibration tie point based on a previously captured depth frame
	void calcCalibration(void); // Calculates the calibration transformation after all tie points have been collected
	};

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
		application->startCapture();
	}

/***********************************
Methods of class CalibrateProjector:
***********************************/

void CalibrateProjector::depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer)
	{
	/* Put the new raw frame into the triple buffer: */
	rawFrames.postNewValue(frameBuffer);
	
	/* Check if there are depth frames left to process: */
	Threads::Mutex::Lock ndfLock(ndfMutex);
	if(numDepthFrames>0)
		{
		/* Add the new frame to the averaging buffer: */
		const unsigned short* sPtr=static_cast<const unsigned short*>(frameBuffer.getBuffer());
		float* dPtr=avgDepthFrame;
		int* dsPtr=avgDepthSum;
		if(captureMin)
			{
			for(unsigned int y=0;y<480;++y)
				for(unsigned int x=0;x<640;++x,++sPtr,++dPtr)
					{
					if(*dPtr>float(*sPtr))
						*dPtr=float(*sPtr);
					}
			}
		else
			{
			for(unsigned int y=0;y<480;++y)
				for(unsigned int x=0;x<640;++x,++sPtr,++dPtr,++dsPtr)
					{
					/* Accumulate the pixel only if it is a valid depth reading: */
					if(*sPtr!=Kinect::FrameSource::invalidDepth)
						{
						*dPtr+=float(*sPtr);
						++*dsPtr;
						}
					}
			}
		
		/* Register the depth frame and check if it was the last: */
		--numDepthFrames;
		if(numDepthFrames==0)
			{
			/* Wake up the foreground thread: */
			ndfCond.broadcast();
			}
		}
	/* Update the foreground thread: */
	Vrui::requestUpdate();
	}

CalibrateProjector::CalibrateProjector(int& argc,char**& argv,char**& appDefaults)
	:Vrui::Application(argc,argv,appDefaults),
	 camera(0),
	 hasDepthCorrection(false),
	 numCaptureFrames(60),
	 captureMin(true),
	 numDepthFrames(-1),
	 avgDepthFrame(new float[640*480]),
	 avgDepthSum(new int[640*480]),
	 backgroundFrame(new float[640*480])
	{
	/* Register the custom tool class: */
	CaptureToolFactory* toolFactory1=new CaptureToolFactory("CaptureTool","Capture",0,*Vrui::getToolManager());
	toolFactory1->setNumButtons(1);
	toolFactory1->setButtonFunction(0,"Capture Frame");
	Vrui::getToolManager()->addClass(toolFactory1,Vrui::ToolManager::defaultToolFactoryDestructor);
	
	/* Process command line parameters: */
	bool printHelp=false;
	int cameraIndex=0;
	imageSize[0]=1024;
	imageSize[1]=768;
	numTiePoints[0]=4;
	numTiePoints[1]=3;
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
			else if(strcasecmp(argv[i]+1,"s")==0)
				{
				for(int j=0;j<2;++j)
					{
					++i;
					imageSize[j]=atoi(argv[i]);
					}
				}
			else if(strcasecmp(argv[i]+1,"tp")==0)
				{
				for(int j=0;j<2;++j)
					{
					++i;
					numTiePoints[j]=atoi(argv[i]);
					}
				}
			}
		}
	
	if(printHelp)
		{
		std::cout<<"Usage: CalibrateProjector [option 1] ... [option n]"<<std::endl;
		std::cout<<"  Options:"<<std::endl;
		std::cout<<"  -h"<<std::endl;
		std::cout<<"     Prints this help message"<<std::endl;
		std::cout<<"  -c <camera index>"<<std::endl;
		std::cout<<"     Selects the local Kinect camera of the given index (0: first camera"<<std::endl;
		std::cout<<"     on USB bus)"<<std::endl;
		std::cout<<"     Default: 0"<<std::endl;
		std::cout<<"  -s <projector image width> <projector image height>"<<std::endl;
		std::cout<<"     Sets the width and height of the projector image in pixels. This"<<std::endl;
		std::cout<<"     must match the actual resolution of the projector."<<std::endl;
		std::cout<<"     Default: 1024 768"<<std::endl;
		std::cout<<"  -tp <grid width> <grid height>"<<std::endl;
		std::cout<<"     Sets the number of tie points to be collected before a calibration"<<std::endl;
		std::cout<<"     is computed."<<std::endl;
		std::cout<<"     Default: 4 3"<<std::endl;
		}
	
	/* Enable background USB event handling: */
	usbContext.startEventHandling();
	
	/* Open the Kinect camera device: */
	camera=new Kinect::Camera(usbContext,cameraIndex);
	
	/* Check if the camera has per-pixel depth correction coefficients: */
	hasDepthCorrection=camera->hasDepthCorrectionCoefficients();
	if(hasDepthCorrection)
		{
		/* Retrieve the per-pixel depth correction coefficients: */
		depthCorrection=camera->getDepthCorrectionCoefficients();
		}
	
	/* Get the camera's intrinsic parameters: */
	cameraIps=camera->getIntrinsicParameters();
	
	/* Start streaming depth frames: */
	camera->startStreaming(0,Misc::createFunctionCall(this,&CalibrateProjector::depthStreamingCallback));
	
	/* Capture a depth frame to use as background: */
	std::cout<<"CalibrateProjector: Capturing background frame..."<<std::flush;
	float* fPtr=avgDepthFrame;
	for(unsigned int y=0;y<480;++y)
		for(unsigned int x=0;x<640;++x,++fPtr)
			*fPtr=float(Kinect::FrameSource::invalidDepth);
	
	{
	Threads::Mutex::Lock ndfLock(ndfMutex);
	numDepthFrames=150;
	while(numDepthFrames>0)
		ndfCond.wait(ndfMutex);
	
	/* Finish the capture process: */
	numDepthFrames=-1;
	captureMin=false;
	}
	
	/* Copy the depth frame into the background frame: */
	float* sPtr=avgDepthFrame;
	float* dPtr=backgroundFrame;
	if(hasDepthCorrection)
		{
		const Kinect::FrameSource::PixelDepthCorrection* pdcPtr=static_cast<const Kinect::FrameSource::PixelDepthCorrection*>(depthCorrection.getBuffer());
		for(unsigned int y=0;y<480;++y)
			for(unsigned int x=0;x<640;++x,++sPtr,++pdcPtr,++dPtr)
				*dPtr=(*sPtr)*pdcPtr->scale+pdcPtr->offset-5.0f; // Subtract small fuzz value
		}
	else
		{
		for(unsigned int y=0;y<480;++y)
			for(unsigned int x=0;x<640;++x,++sPtr,++dPtr)
				*dPtr=*sPtr-5.0f; // Subtract small fuzz value
		}
	
	std::cout<<" done"<<std::endl;
	}

CalibrateProjector::~CalibrateProjector(void)
	{
	/* Stop streaming and close the Kinect camera device: */
	camera->stopStreaming();
	delete camera;
	
	delete[] avgDepthFrame;
	delete[] avgDepthSum;
	delete[] backgroundFrame;
	}

void CalibrateProjector::frame(void)
	{
	/* Lock the most recent raw depth frame: */
	if(rawFrames.lockNewValue())
		{
		/* Create blobs for all non-background pixels: */
		const int size[2]={640,480};
		BackgroundProperty bp(backgroundFrame);
		rawBlobs=findBlobs(size,static_cast<unsigned short*>(rawFrames.getLockedValue().getBuffer()),bp);
		}
	
	/* Check if a depth frame capture just finished: */
	bool processDepthFrame=false;
	{
	Threads::Mutex::Lock ndfLock(ndfMutex);
	if(numDepthFrames==0)
		{
		/* Process the average frame later: */
		std::cout<<" done"<<std::endl;
		processDepthFrame=true;
		
		/* Finish the capture process: */
		numDepthFrames=-1;
		}
	}
	
	if(processDepthFrame)
		{
		/* Finish averaging the depth frame: */
		float* dPtr=avgDepthFrame;
		int* dsPtr=avgDepthSum;
		if(hasDepthCorrection)
			{
			const Kinect::FrameSource::PixelDepthCorrection* pdcPtr=static_cast<const Kinect::FrameSource::PixelDepthCorrection*>(depthCorrection.getBuffer());
			for(unsigned int y=0;y<480;++y)
				for(unsigned int x=0;x<640;++x,++dPtr,++dsPtr,++pdcPtr)
					{
					/* Only use the pixel if it was sampled in at least half the frames: */
					if(*dsPtr>=numCaptureFrames/2)
						*dPtr=((*dPtr)/float(*dsPtr))*pdcPtr->scale+pdcPtr->offset;
					else
						*dPtr=float(Kinect::FrameSource::invalidDepth);
					}
			}
		else
			{
			for(unsigned int y=0;y<480;++y)
				for(unsigned int x=0;x<640;++x,++dPtr,++dsPtr)
					{
					/* Only use the pixel if it was sampled in at least half the frames: */
					if(*dsPtr>=numCaptureFrames/2)
						*dPtr=(*dPtr)/float(*dsPtr);
					else
						*dPtr=float(Kinect::FrameSource::invalidDepth);
					}
			}
		
		/* Add a calibration tie point based on the captured depth frame: */	
		addTiePoint();
		}
	}

void CalibrateProjector::display(GLContextData& contextData) const
	{
	/* Calculate the screen-space position of the next tie point: */
	int pointIndex=int(tiePoints.size());
	int xIndex=pointIndex%numTiePoints[0];
	int yIndex=(pointIndex/numTiePoints[0])%numTiePoints[1];
	int x=(xIndex+1)*imageSize[0]/(numTiePoints[0]+1);
	int y=(yIndex+1)*imageSize[1]/(numTiePoints[1]+1);
	
	glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT);
	glDisable(GL_LIGHTING);
	glLineWidth(1.0f);
	
	/* Go to screen space: */
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0,double(imageSize[0]),0.0,double(imageSize[1]),-1.0,1.0);
	
	glBegin(GL_LINES);
	glColor3f(1.0f,1.0f,1.0f);
	glVertex2f(0.0f,float(y)+0.5f);
	glVertex2f(float(imageSize[0]),float(y)+0.5f);
	glVertex2f(float(x)+0.5f,0.0f);
	glVertex2f(float(x)+0.5f,float(imageSize[1]));
	glEnd();
	
	/* Draw all foreground blobs in the current raw depth frame: */
	glScaled(double(imageSize[0])/640.0,double(imageSize[1])/480.0,1.0);
	for(std::vector<Blob<unsigned short> >::const_iterator bIt=rawBlobs.begin();bIt!=rawBlobs.end();++bIt)
		{
		glColor3f(0.0f,1.0f,0.0f);
		glBegin(GL_LINE_LOOP);
		glVertex2i(bIt->min[0],bIt->min[1]);
		glVertex2i(bIt->max[0],bIt->min[1]);
		glVertex2i(bIt->max[0],bIt->max[1]);
		glVertex2i(bIt->min[0],bIt->max[1]);
		glEnd();
		}
	
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	
	glPopAttrib();
	}

void CalibrateProjector::startCapture(void)
	{
	Threads::Mutex::Lock ndfLock(ndfMutex);
	
	/* Do nothing if already capturing a depth frame: */
	if(numDepthFrames>=0)
		return;
	
	/* Reset the average depth frame: */
	float* dPtr=avgDepthFrame;
	int* dsPtr=avgDepthSum;
	for(unsigned int y=0;y<480;++y)
		for(unsigned int x=0;x<640;++x,++dPtr,++dsPtr)
			{
			*dPtr=0.0f;
			*dsPtr=0;
			}
	
	/* Request a sequence of raw depth frames: */
	numDepthFrames=numCaptureFrames;
	
	/* Background thread will capture frames and count back to zero */
	std::cout<<"CalibrateProjector: Capturing "<<numCaptureFrames<<" depth frame..."<<std::flush;
	}

void CalibrateProjector::addTiePoint(void)
	{
	/* Create blobs for all non-background pixels: */
	const int size[2]={640,480};
	BackgroundProperty bp(backgroundFrame);
	std::vector<Blob<float> > blobs=findBlobs(size,avgDepthFrame,bp);
	
	/* Use the largest blob to create a tie point: */
	std::vector<Blob<float> >::iterator largestBIt=blobs.end();
	size_t largestNumPixels=50;
	for(std::vector<Blob<float> >::iterator bIt=blobs.begin();bIt!=blobs.end();++bIt)
		{
		if(largestNumPixels<bIt->blobProperty.getNumPixels())
			{
			largestBIt=bIt;
			largestNumPixels=bIt->blobProperty.getNumPixels();
			}
		}
	
	if(largestBIt!=blobs.end())
		{
		/* Get the largest blob's centroid in depth image space: */
		OPoint op=largestBIt->blobProperty.calcCentroid();
		std::cout<<op[0]<<", "<<op[1]<<", "<<op[2]<<std::endl;
		
		/* Transform the largest blob's centroid to 3D camera space: */
		op=cameraIps.depthProjection.transform(op);
		std::cout<<op[0]<<", "<<op[1]<<", "<<op[2]<<std::endl;
		
		/* Store the new tie point: */
		TiePoint tp;
		int pointIndex=int(tiePoints.size());
		int xIndex=pointIndex%numTiePoints[0];
		int yIndex=(pointIndex/numTiePoints[0])%numTiePoints[1];
		int x=(xIndex+1)*imageSize[0]/(numTiePoints[0]+1);
		int y=(yIndex+1)*imageSize[1]/(numTiePoints[1]+1);
		tp.p=PPoint(Scalar(x)+Scalar(0.5),Scalar(y)+Scalar(0.5));
		tp.o=op;
		tiePoints.push_back(tp);
		
		/* Check if the calibration is complete: */
		if(tiePoints.size()>=numTiePoints[0]*numTiePoints[1])
			{
			/* Calculate the calibration transformation: */
			calcCalibration();
			}
		
		#if 0
		{
		Misc::File capFrame("CapturedFrame.ppm","wb",Misc::File::DontCare);
		fprintf(capFrame.getFilePtr(),"P6\n");
		fprintf(capFrame.getFilePtr(),"640 480\n");
		fprintf(capFrame.getFilePtr(),"255\n");
		float* fPtr=avgDepthFrame;
		float* bPtr=backgroundFrame;
		for(int y=0;y<480;++y)
			for(int x=0;x<640;++x,++fPtr,++bPtr)
				{
				unsigned char col[3];
				for(int i=0;i<3;++i)
					col[i]=(unsigned char)(*fPtr*256.0f/2048.0f);
				if(*fPtr>=*bPtr)
					col[0]=0;
				if(x>=largestBIt->min[0]&&x<largestBIt->max[0]&&y>=largestBIt->min[1]&&y<largestBIt->max[1])
					col[1]=col[2]=0;
				if(x==(unsigned char)(largestBIt->x)&&y==(unsigned char)(largestBIt->y))
					col[0]=col[1]=col[2]=0;
				capFrame.write<unsigned char>(col,3);
				}
		}
		#endif
		}
	else
		{
		std::cout<<"No blobs found in averaged depth frame!"<<std::endl;
		
		#if 0
		{
		Misc::File capFrame("CapturedFrame.ppm","wb",Misc::File::DontCare);
		fprintf(capFrame.getFilePtr(),"P6\n");
		fprintf(capFrame.getFilePtr(),"640 480\n");
		fprintf(capFrame.getFilePtr(),"255\n");
		float* fPtr=avgDepthFrame;
		float* bPtr=backgroundFrame;
		for(int y=0;y<480;++y)
			for(int x=0;x<640;++x,++fPtr,++bPtr)
				{
				unsigned char col[3];
				for(int i=0;i<3;++i)
					col[i]=(unsigned char)(*fPtr*256.0f/2048.0f);
				if(*fPtr>=*bPtr)
					col[0]=0;
				capFrame.write<unsigned char>(col,3);
				}
		}
		#endif
		}
	}

void CalibrateProjector::calcCalibration(void)
	{
	/* Create the least-squares system: */
	Math::Matrix a(12,12,0.0);
	
	/* Process all tie points: */
	for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
		{
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
					a.set(i,j,a(i,j)+eq[row][i]*eq[row][j]);
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
	
	/* Create the normalized homography: */
	Math::Matrix hom(3,4);
	double scale=qe.first(11,minEIndex);
	for(int i=0;i<3;++i)
		for(int j=0;j<4;++j)
			hom.set(i,j,qe.first(i*4+j,minEIndex)/scale);
	
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
	Math::Matrix projection(4,4);
	for(unsigned int i=0;i<2;++i)
		for(unsigned int j=0;j<4;++j)
			projection(i,j)=hom(i,j);
	for(unsigned int j=0;j<4;++j)
		projection(2,j)=j==2?1.0:0.0;
	for(unsigned int j=0;j<4;++j)
		projection(3,j)=hom(2,j);
	
	/* Calculate the z range of all tie points: */
	Math::Interval<double> zRange=Math::Interval<double>::empty;
	for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
		{
		/* Transform the object-space tie point with the projection matrix: */
		Math::Matrix op(4,1);
		for(int i=0;i<3;++i)
			op(i)=double(tpIt->o[i]);
		op(3)=1.0;
		Math::Matrix pp=projection*op;
		zRange.addValue(pp(2)/pp(3));
		}
	std::cout<<"Z range of collected tie points: ["<<zRange.getMin()<<", "<<zRange.getMax()<<"]"<<std::endl;
	
	/* Double the size of the range to include a safety margin on either side: */
	zRange=Math::Interval<double>(zRange.getMin()-zRange.getSize()*0.5,zRange.getMax()+zRange.getSize()*0.5);
	
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
	std::string projFileName=CONFIGDIR;
	projFileName.push_back('/');
	projFileName.append("ProjectorMatrix.dat");
	IO::FilePtr projFile=Vrui::openFile(projFileName.c_str(),IO::File::WriteOnly);
	projFile->setEndianness(Misc::LittleEndian);
	for(int i=0;i<4;++i)
		for(int j=0;j<4;++j)
			projFile->write<double>(projection(i,j));
	}

int main(int argc,char* argv[])
	{
	try
		{
		char** appDefault=0;
		CalibrateProjector app(argc,argv,appDefault);
		app.run();
		}
	catch(std::runtime_error err)
		{
		std::cerr<<"Caught exception "<<err.what()<<std::endl;
		return 1;
		}
	
	return 0;
	}
