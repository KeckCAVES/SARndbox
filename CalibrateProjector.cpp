/***********************************************************************
CalibrateProjector - Utility to calculate the calibration transformation
of a projector into a Kinect-captured 3D space.
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

#include "CalibrateProjector.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <Misc/FunctionCalls.h>
#include <Misc/File.h>
#include <IO/ValueSource.h>
#include <IO/CSVSource.h>
#include <IO/File.h>
#include <IO/OpenFile.h>
#include <Math/Math.h>
#include <Math/Interval.h>
#include <Geometry/GeometryValueCoders.h>
#include <GL/GLContextData.h>
#include <GL/Extensions/GLARBTextureNonPowerOfTwo.h>
#include <Images/ExtractBlobs.h>
#include <Vrui/Vrui.h>
#include <Vrui/ToolManager.h>
#include <Vrui/OpenFile.h>

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

/*********************************************
Methods of class CalibrateProjector::DataItem:
*********************************************/

CalibrateProjector::DataItem::DataItem(void)
	:blobImageTextureId(0),blobImageVersion(0)
	{
	glGenTextures(1,&blobImageTextureId);
	}

CalibrateProjector::DataItem::~DataItem(void)
	{
	glDeleteTextures(1,&blobImageTextureId);
	}

namespace {

/**************
Helper classes:
**************/

class BlobForegroundSelector // Functor class to select foreground pixels
	{
	/* Methods: */
	public:
	bool operator()(unsigned int x,unsigned int y,const Kinect::FrameSource::DepthPixel& pixel) const
		{
		return pixel<Kinect::FrameSource::invalidDepth;
		}
	};

class BlobMergeChecker // Functor class to check whether two pixels can belong to the same blob
	{
	/* Elements: */
	private:
	int maxDepthDist;
	
	/* Constructors and destructors: */
	public:
	BlobMergeChecker(int sMaxDepthDist)
		:maxDepthDist(sMaxDepthDist)
		{
		}
	
	/* Methods: */
	bool operator()(unsigned int x1,unsigned int y1,const Kinect::FrameSource::DepthPixel& pixel1,unsigned int x2,unsigned int y2,const Kinect::FrameSource::DepthPixel& pixel2) const
		{
		return Math::abs(int(pixel1)-int(pixel2))<=maxDepthDist;
		}
	};

}

/***********************************
Methods of class CalibrateProjector:
***********************************/

void CalibrateProjector::depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer)
	{
	/* Do nothing if currently capturing background frames: */
	if(capturingBackground)
		return;
	
	/* Put the new raw frame into the triple buffer: */
	rawFrames.postNewValue(frameBuffer);
	
	/* Wake up the foreground thread: */
	Vrui::requestUpdate();
	}

void CalibrateProjector::backgroundCaptureCompleteCallback(Kinect::Camera&)
	{
	/* Reset the background capture flag: */
	std::cout<<" done"<<std::endl;
	camera->setRemoveBackground(true);
	capturingBackground=false;
	
	/* Wake up the foreground thread: */
	Vrui::requestUpdate();
	}

CalibrateProjector::CalibrateProjector(int& argc,char**& argv)
	:Vrui::Application(argc,argv),
	 numTiePointFrames(60),numBackgroundFrames(120),
	 blobMergeDepth(1),
	 camera(0),
	 pixelDepthCorrection(0),
	 capturingBackground(false),
	 blobIdImage(0),blobImage(0),blobImageVersion(0),
	 currentBlob(0),
	 capturingTiePoint(false),numCaptureFrames(0),
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
	std::string sandboxLayoutFileName=CONFIGDIR;
	sandboxLayoutFileName.push_back('/');
	sandboxLayoutFileName.append("BoxLayout.txt");
	int cameraIndex=0;
	imageSize[0]=1024;
	imageSize[1]=768;
	numTiePoints[0]=4;
	numTiePoints[1]=3;
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
				sandboxLayoutFileName=argv[i];
				}
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
			else if(strcasecmp(argv[i]+1,"bmd")==0)
				{
				++i;
				blobMergeDepth=atoi(argv[i]);
				}
			else if(strcasecmp(argv[i]+1,"tpf")==0)
				{
				++i;
				tiePointFileName=argv[i];
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
		std::cout<<"     Default: "<<CONFIGDIR<<"/BoxLayout.txt"<<std::endl;
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
		std::cout<<"  -bmd <mamximum blob merge depth distance>"<<std::endl;
		std::cout<<"     Maximum depth distance between adjacent pixels in the same blob."<<std::endl;
		std::cout<<"     Default: 1"<<std::endl;
		std::cout<<"  -tpf <tie point file name>"<<std::endl;
		std::cout<<"     Reads initial calibration tie points from a CSV file"<<std::endl;
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
		basePlaneCorners[i]=Misc::ValueCoder<OPoint>::decode(s.c_str(),s.c_str()+s.length());
		}
	}
	
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
		
		if(tiePoints.size()>=numTiePoints[0]*numTiePoints[1])
			{
			/* Calculate an initial calibration: */
			calcCalibration();
			}
		}
	
	/* Enable background USB event handling: */
	usbContext.startEventHandling();
	
	/* Open the Kinect camera device: */
	camera=new Kinect::Camera(usbContext,cameraIndex);
	camera->setCompressDepthFrames(true);
	camera->setSmoothDepthFrames(false);
	camera->setBackgroundRemovalFuzz(1);
	
	/* Get the camera's depth frame size: */
	for(int i=0;i<2;++i)
		frameSize[i]=camera->getActualFrameSize(Kinect::FrameSource::DEPTH)[i];
	
	/* Get the camera's depth correction coefficients and create the per-pixel correction buffer: */
	Kinect::FrameSource::DepthCorrection* dc=camera->getDepthCorrectionParameters();
	pixelDepthCorrection=dc->getPixelCorrection(frameSize);
	delete dc;
	
	/* Get the camera's intrinsic parameters: */
	cameraIps=camera->getIntrinsicParameters();
	
	/* Create the blob ID image: */
	blobIdImage=new unsigned int[frameSize[1]*frameSize[0]];
	blobImage=new GLColor<GLubyte,3>[frameSize[1]*frameSize[0]];
	GLColor<GLubyte,3>* biPtr=blobImage;
	for(unsigned int y=0;y<frameSize[1];++y)
		for(unsigned int x=0;x<frameSize[0];++x,++biPtr)
			*biPtr=GLColor<GLubyte,3>(0,0,0);
	blobImageVersion=1;
	
	/* Start streaming depth frames: */
	camera->startStreaming(0,Misc::createFunctionCall(this,&CalibrateProjector::depthStreamingCallback));
	
	/* Start capturing the initial background frame: */
	startBackgroundCapture();
	}

CalibrateProjector::~CalibrateProjector(void)
	{
	/* Delete blob extraction state: */
	delete currentBlob;
	
	/* Stop streaming and close the Kinect camera device: */
	camera->stopStreaming();
	delete camera;
	
	/* Delete allocated buffers: */
	delete[] blobIdImage;
	delete[] blobImage;
	delete[] pixelDepthCorrection;
	}

void CalibrateProjector::frame(void)
	{
	/* Check if there is a new raw depth frame: */
	if(rawFrames.lockNewValue())
		{
		/* Extract all foreground blobs from the raw depth frame: */
		const DepthPixel* framePixels=static_cast<const DepthPixel*>(rawFrames.getLockedValue().getBuffer());
		BlobForegroundSelector bfs;
		BlobMergeChecker bmc(blobMergeDepth);
		DepthCentroidBlob::Creator blobCreator;
		for(int i=0;i<2;++i)
			blobCreator.frameSize[i]=frameSize[i];
		blobCreator.pixelDepthCorrection=pixelDepthCorrection;
		blobCreator.depthProjection=cameraIps.depthProjection;
		std::vector<DepthCentroidBlob> blobs=Images::extractBlobs<DepthCentroidBlob>(frameSize,framePixels,bfs,bmc,blobCreator,blobIdImage);
		
		/* Find the largest blob that is inside the sandbox area and roughly disk-shaped: */
		std::vector<DepthCentroidBlob>::iterator biggestBlobIt=blobs.end();
		size_t maxNumPixels=50;
		for(std::vector<DepthCentroidBlob>::iterator bIt=blobs.begin();bIt!=blobs.end();++bIt)
			if(maxNumPixels<bIt->numPixels)
				{
				/* Check if the blob is inside the configured sandbox area and roughly blob-shaped: */
				OPoint blobCentroid=bIt->getCentroid(cameraIps.depthProjection);
				bool inside=true;
				inside=inside&&Geometry::cross(basePlane.getNormal(),basePlaneCorners[1]-basePlaneCorners[0])*(blobCentroid-basePlaneCorners[0])>=0.0;
				inside=inside&&Geometry::cross(basePlane.getNormal(),basePlaneCorners[3]-basePlaneCorners[1])*(blobCentroid-basePlaneCorners[1])>=0.0;
				inside=inside&&Geometry::cross(basePlane.getNormal(),basePlaneCorners[2]-basePlaneCorners[3])*(blobCentroid-basePlaneCorners[3])>=0.0;
				inside=inside&&Geometry::cross(basePlane.getNormal(),basePlaneCorners[0]-basePlaneCorners[2])*(blobCentroid-basePlaneCorners[2])>=0.0;
				double fillRatio=double(bIt->numPixels)/(double(bIt->bbMax[0]-bIt->bbMin[0])*double(bIt->bbMax[1]-bIt->bbMin[1]));
				if(inside&&fillRatio>=0.7) // Approximate fill ratio of a circle inside a square
					{
					/* Use this blob for now: */
					biggestBlobIt=bIt;
					maxNumPixels=bIt->numPixels;
					}
				}
		
		/* Update the current blob: */
		delete currentBlob;
		currentBlob=0;
		if(biggestBlobIt!=blobs.end())
			{
			currentBlob=new DepthCentroidBlob(*biggestBlobIt);
			currentCentroid=currentBlob->getCentroid(cameraIps.depthProjection);
			}
		
		/* Create the blob image: */
		#if VISUALIZE_BLOBS
		GLColor<GLubyte,3> blobColors[]=
			{
			GLColor<GLubyte,3>(255,0,0),
			GLColor<GLubyte,3>(255,255,0),
			GLColor<GLubyte,3>(0,255,255),
			GLColor<GLubyte,3>(0,0,255),
			GLColor<GLubyte,3>(255,0,255),
			GLColor<GLubyte,3>(128,0,0),
			GLColor<GLubyte,3>(128,128,0),
			GLColor<GLubyte,3>(0,128,0),
			GLColor<GLubyte,3>(0,128,128),
			GLColor<GLubyte,3>(0,0,128),
			GLColor<GLubyte,3>(128,0,128),
			GLColor<GLubyte,3>(255,128,128),
			GLColor<GLubyte,3>(255,255,128),
			GLColor<GLubyte,3>(128,255,128),
			GLColor<GLubyte,3>(128,255,255),
			GLColor<GLubyte,3>(128,128,255),
			GLColor<GLubyte,3>(255,128,255)
			};
		const unsigned int numBlobColors=sizeof(blobColors)/sizeof(blobColors[0]);
		#endif
		
		const unsigned int* biiPtr=blobIdImage;
		GLColor<GLubyte,3>* biPtr=blobImage;
		for(unsigned int y=0;y<frameSize[1];++y)
			for(unsigned int x=0;x<frameSize[0];++x,++biiPtr,++biPtr)
				{
				#if VISUALIZE_BLOBS
				
				/* Assign different colors to each blob: */
				if(currentBlob!=0&&*biiPtr==currentBlob->blobId)
					*biPtr=GLColor<GLubyte,3>(0,255,0);
				else if(*biiPtr==~0x0U)
					*biPtr=GLColor<GLubyte,3>(0,0,0);
				else
					*biPtr=blobColors[(*biiPtr)%numBlobColors];
				
				#else
				
				/* Make the current target blob green and all others yellow: */
				if(currentBlob!=0&&*biiPtr==currentBlob->blobId)
					*biPtr=GLColor<GLubyte,3>(0,255,0);
				else if(*biiPtr!=~0x0U)
					*biPtr=GLColor<GLubyte,3>(255,255,0);
				else
					*biPtr=GLColor<GLubyte,3>(0,0,0);
				
				#endif
				}
		++blobImageVersion;
		
		/* Check if we are currently capturing a tie point: */
		if(capturingTiePoint&&currentBlob!=0)
			{
			/* Add the current target blob to the tie point combiner: */
			tiePointCombiner.addPoint(currentCentroid);
			--numCaptureFrames;
			
			if(numCaptureFrames==0)
				{
				/* Store the just-captured tie point: */
				TiePoint tp;
				int pointIndex=int(tiePoints.size());
				int xIndex=pointIndex%numTiePoints[0];
				int yIndex=(pointIndex/numTiePoints[0])%numTiePoints[1];
				int x=(xIndex+1)*imageSize[0]/(numTiePoints[0]+1);
				int y=(yIndex+1)*imageSize[1]/(numTiePoints[1]+1);
				tp.p=PPoint(Scalar(x)+Scalar(0.5),Scalar(y)+Scalar(0.5));
				tp.o=tiePointCombiner.getPoint();
				tiePoints.push_back(tp);
				
				std::cout<<" done"<<std::endl;
				std::cout<<"Tie point: "<<tp.p[0]<<", "<<tp.p[1]<<"; "<<tp.o[0]<<", "<<tp.o[1]<<", "<<tp.o[2]<<std::endl;
				
				capturingTiePoint=false;
				
				/* Check if the calibration is complete: */
				if(tiePoints.size()>=numTiePoints[0]*numTiePoints[1])
					{
					/* Calculate the calibration transformation: */
					calcCalibration();
					}
				}
			}
		}
	}

void CalibrateProjector::display(GLContextData& contextData) const
	{
	/* Get the context data item: */
	DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
	
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
	
	if(capturingBackground)
		{
		/* Indicate that a background frame is being captured: */
		glBegin(GL_QUADS);
		glColor3f(1.0f,0.0f,0.0f);
		glVertex2f(0.0f,0.0f);
		glVertex2f(float(imageSize[0]),0.0f);
		glVertex2f(float(imageSize[0]),float(imageSize[1]));
		glVertex2f(0.0f,float(imageSize[1]));
		glEnd();
		}
	else
		{
		/* Calculate the screen-space position of the next tie point: */
		int pointIndex=int(tiePoints.size());
		int xIndex=pointIndex%numTiePoints[0];
		int yIndex=(pointIndex/numTiePoints[0])%numTiePoints[1];
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
		
		/* Draw the current blob image: */
		glBindTexture(GL_TEXTURE_2D,dataItem->blobImageTextureId);
		if(dataItem->blobImageVersion!=blobImageVersion)
			{
			/* Upload the new blob image into the texture: */
			glTexSubImage2D(GL_TEXTURE_2D,0,0,0,frameSize[0],frameSize[1],GL_RGB,GL_UNSIGNED_BYTE,blobImage);
			dataItem->blobImageVersion=blobImageVersion;
			}
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE),
		glBegin(GL_QUADS);
		glTexCoord2f(dataItem->texMin[0],dataItem->texMin[1]);
		glVertex3f(0.0f,0.0f,-0.01);
		glTexCoord2f(dataItem->texMax[0],dataItem->texMin[1]);
		glVertex3f(float(imageSize[0]),0.0f,-0.01);
		glTexCoord2f(dataItem->texMax[0],dataItem->texMax[1]);
		glVertex3f(float(imageSize[0]),float(imageSize[1]),-0.01);
		glTexCoord2f(dataItem->texMin[0],dataItem->texMax[1]);
		glVertex3f(0.0f,float(imageSize[1]),-0.01);
		glEnd();
		glDisable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D,0);
		
		if(currentBlob!=0)
			{
			#if 0
			
			/* Draw the currently selected target blob: */
			glScaled(double(imageSize[0])/double(frameSize[0]),double(imageSize[1])/double(frameSize[1]),1.0);
			glColor3f(0.0f,1.0f,0.0f);
			glBegin(GL_LINE_LOOP);
			glVertex2i(currentBlob->bbMin[0],currentBlob->bbMin[1]);
			glVertex2i(currentBlob->bbMax[0],currentBlob->bbMin[1]);
			glVertex2i(currentBlob->bbMax[0],currentBlob->bbMax[1]);
			glVertex2i(currentBlob->bbMin[0],currentBlob->bbMax[1]);
			glEnd();
			
			#endif
			
			if(haveProjection)
				{
				/* Draw the currently selected target blob using the current calibration: */
				glLoadIdentity();
				Math::Matrix blob(4,1);
				for(int i=0;i<3;++i)
					blob(i)=currentCentroid[i];
				blob(3)=1.0;
				Math::Matrix projBlob=projection*blob;
				double x=projBlob(0)/projBlob(3);
				double y=projBlob(1)/projBlob(3);
				glBegin(GL_LINES);
				glColor3f(1.0f,0.0f,0.0f);
				glVertex2d(x,-1.0);
				glVertex2d(x,1.0);
				glVertex2d(-1.0,y);
				glVertex2d(1.0,y);
				glEnd();
				}
			}
		}
	
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	
	glPopAttrib();
	}

void CalibrateProjector::initContext(GLContextData& contextData) const
	{
	/* Create the data item: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	
	/* Check whether non-power-of-two-dimension textures are supported: */
	bool haveNpotdt=GLARBTextureNonPowerOfTwo::isSupported();
	if(haveNpotdt)
		GLARBTextureNonPowerOfTwo::initExtension();
	
	/* Calculate the texture coordinate rectangle: */
	unsigned int texSize[2];
	if(haveNpotdt)
		{
		for(int i=0;i<2;++i)
			texSize[i]=frameSize[i];
		}
	else
		{
		for(int i=0;i<2;++i)
			for(texSize[i]=1U;texSize[i]<frameSize[i];texSize[i]<<=1)
				;
		}
	for(int i=0;i<2;++i)
		{
		dataItem->texMin[i]=0.0f;
		dataItem->texMax[i]=GLfloat(frameSize[i])/GLfloat(texSize[i]);
		}
	
	/* Initialize the texture object: */
	glBindTexture(GL_TEXTURE_2D,dataItem->blobImageTextureId);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,texSize[0],texSize[1],0,GL_RGB,GL_UNSIGNED_BYTE,0);
	
	/* Protect the texture object: */
	glBindTexture(GL_TEXTURE_2D,0);
	}

void CalibrateProjector::startBackgroundCapture(void)
	{
	/* Bail out if already capturing a tie point or background: */
	if(capturingBackground||capturingTiePoint)
		return;
	
	/* Tell the Kinect camera to capture a new background frame: */
	capturingBackground=true;
	std::cout<<"CalibrateProjector: Capturing "<<numBackgroundFrames<<" background frames..."<<std::flush;
	camera->captureBackground(numBackgroundFrames,true,Misc::createFunctionCall(this,&CalibrateProjector::backgroundCaptureCompleteCallback));
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
	tiePointCombiner.reset();
	}

void CalibrateProjector::calcCalibration(void)
	{
	/* Create the least-squares system: */
	Math::Matrix a(12,12,0.0);
	
	/* Process all tie points: */
	for(std::vector<TiePoint>::iterator tpIt=tiePoints.begin();tpIt!=tiePoints.end();++tpIt)
		{
		std::cout<<"Tie point: "<<tpIt->p[0]<<", "<<tpIt->p[1]<<", "<<tpIt->o[0]<<", "<<tpIt->o[1]<<", "<<tpIt->o[2]<<std::endl;
		
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
		std::string projFileName=CONFIGDIR;
		projFileName.push_back('/');
		projFileName.append("ProjectorMatrix.dat");
		IO::FilePtr projFile=Vrui::openFile(projFileName.c_str(),IO::File::WriteOnly);
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
