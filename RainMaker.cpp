/***********************************************************************
RainMaker - Class to detect objects moving through a given range of
depths in a depth image sequence to trigger rainfall on virtual terrain.
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

#include "RainMaker.h"

#include <Misc/FunctionCalls.h>
#include <Geometry/HVector.h>
#include <Geometry/Plane.h>

#include "FindBlobs.h"

template <>
class BlobProperty<unsigned short> // Class to calculate the 3D centroid of a blob in depth image space
	{
	/* Elements: */
	private:
	double pxs,pys,pzs; // Accumulated components of centroid
	size_t numPixels; // Number of accumulated pixels
	
	/* Constructors and destructors: */
	public:
	BlobProperty(void)
		:pxs(0.0),pys(0.0),pzs(0.0),
		 numPixels(0)
		{
		}
	
	/* Methods: */
	void addPixel(unsigned int x,unsigned int y,const unsigned short& pixelValue)
		{
		pxs+=double(x);
		pys+=double(y);
		pzs+=double(pixelValue);
		++numPixels;
		}
	void merge(const BlobProperty& other)
		{
		pxs+=other.pxs;
		pys+=other.pys;
		pzs+=other.pzs;
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

template <>
class BlobProperty<float> // Class to calculate the 3D centroid of a blob in depth image space
	{
	/* Elements: */
	private:
	double pxs,pys,pzs; // Accumulated components of centroid
	size_t numPixels; // Number of accumulated pixels
	
	/* Constructors and destructors: */
	public:
	BlobProperty(void)
		:pxs(0.0),pys(0.0),pzs(0.0),
		 numPixels(0)
		{
		}
	
	/* Methods: */
	void addPixel(unsigned int x,unsigned int y,const float& pixelValue)
		{
		pxs+=double(x);
		pys+=double(y);
		pzs+=double(pixelValue);
		++numPixels;
		}
	void merge(const BlobProperty& other)
		{
		pxs+=other.pxs;
		pys+=other.pys;
		pzs+=other.pzs;
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

class ValidPixelProperty // Functor class to identify valid pixels in raw depth frames
	{
	/* Elements: */
	private:
	float minPlane[4]; // Plane equation of the lower bound of valid depth values in depth image space
	float maxPlane[4]; // Plane equation of the upper bound of valid depth values in depth image space
	Geometry::Matrix<float,3,4> colorDepthHomography; // Homography from 3D depth image space into 2D color image space
	unsigned int colorSize[2]; // Width and height of color frames
	const unsigned char* colorFrame; // The current color frame
	
	/* Constructors and destructors: */
	public:
	ValidPixelProperty(const float sMinPlane[4],const float sMaxPlane[4],const Geometry::Matrix<float,3,4>& sColorDepthHomography,const unsigned int sColorSize[2])
		:colorDepthHomography(sColorDepthHomography),
		 colorFrame(0)
		{
		/* Copy the min and max plane equations: */
		for(int i=0;i<4;++i)
			minPlane[i]=sMinPlane[i];
		for(int i=0;i<4;++i)
			maxPlane[i]=sMaxPlane[i];
		
		/* Copy the color image size: */
		for(int i=0;i<2;++i)
			colorSize[i]=sColorSize[i];
		}
	
	/* Methods: */
	public:
	void setColorFrame(const unsigned char* newColorFrame) // Sets the color frame for the next blob extraction
		{
		colorFrame=newColorFrame;
		}
	bool operator()(unsigned int x,unsigned int y,const unsigned short& pixel) const
		{
		return operator()(x,y,float(pixel));
		}
	bool operator()(unsigned int x,unsigned int y,const float& pixel) const
		{
		/* Plug the pixel into the plane equations to determine its validity: */
		float px=float(x)+0.5f;
		float py=float(y)+0.5f;
		float pz=pixel;
		float minD=minPlane[0]*px+minPlane[1]*py+minPlane[2]*pz+minPlane[3];
		float maxD=maxPlane[0]*px+maxPlane[1]*py+maxPlane[2]*pz+maxPlane[3];
		if(minD<0.0f||maxD>0.0f)
			return false;
		
		#if 0
		
		/* Project the pixel into the color frame: */
		Geometry::ComponentArray<float,3> colorPos=colorDepthHomography*Geometry::ComponentArray<float,4>(px,py,pz,1.0f);
		int cx=int(Math::floor(colorPos[0]/colorPos[2]));
		int cy=int(Math::floor(colorPos[1]/colorPos[2]));
		if(cx<0||cx>=colorSize[0]||cy<0||cy>=colorSize[1])
			return false;
		
		#if 0
		
		/* Check if the pixel is mostly black-ish: */
		const unsigned char* rgb=colorFrame+((cy*colorSize[0]+cx)*3);
		return rgb[0]<64U&&rgb[1]<64U&&rgb[2]<64U;
		
		#else
		
		/* Normalize the pixel's color: */
		const unsigned char* rgb=colorFrame+((cy*colorSize[0]+cx)*3);
		unsigned char max=rgb[0];
		for(int i=1;i<3;++i)
			if(max<rgb[i])
				max=rgb[i];
		float rgb0[3];
		for(int i=0;i<3;++i)
			rgb0[i]=float(rgb[i])/float(max);
		
		/* Check if the color is red-ish: */
		return rgb0[0]>=0.8f&&rgb0[1]<0.25f&&rgb0[2]<0.25f;
		
		#endif
		
		#else
		
		return true;
		
		#endif
		}
	};

/**************************
Methods of class RainMaker:
**************************/

template <class DepthPixelParam>
inline
void RainMaker::extractBlobs(const Kinect::FrameBuffer& depthFrame,const ValidPixelProperty& vpp,RainMaker::BlobList& blobsCc)
	{
	/* Extract raw blobs from the depth frame: */
	std::vector< ::Blob<DepthPixelParam> > blobsDic=findBlobs(depthSize,depthFrame.getData<DepthPixelParam>(),vpp);
	
	/* Transform all blobs larger than the threshold to camera space: */
	blobsCc.reserve(blobsDic.size());
	for(typename std::vector< ::Blob<DepthPixelParam> >::const_iterator bIt=blobsDic.begin();bIt!=blobsDic.end();++bIt)
		if(bIt->max[0]-bIt->min[0]>=minBlobSize&&bIt->max[1]-bIt->min[1]>=minBlobSize)
			{
			Blob blobCc;
			Point centroidDic=bIt->blobProperty.calcCentroid();
			blobCc.centroid=depthProjection.transform(centroidDic);
			
			/* Estimate the radius of the blob in camera space (this is admittedly ad-hoc): */
			double radiusDic=double(bIt->max[0]-bIt->min[0])*0.5;
			if(radiusDic>(bIt->max[1]-bIt->min[1])*0.5)
				{
				radiusDic=(bIt->max[1]-bIt->min[1])*0.5;
				blobCc.radius=Geometry::dist(depthProjection.transform(Point(centroidDic[0],centroidDic[1]+radiusDic,centroidDic[2])),blobCc.centroid);
				}
			else
				blobCc.radius=Geometry::dist(depthProjection.transform(Point(centroidDic[0]+radiusDic,centroidDic[1],centroidDic[2])),blobCc.centroid);
			
			/* Store the blob: */
			blobsCc.push_back(blobCc);
			}
	}

void* RainMaker::detectionThreadMethod(void)
	{
	unsigned int lastInputDepthFrameVersion=0;
	unsigned int lastInputColorFrameVersion=0;
	
	/* Create a pixel validity decider: */
	ValidPixelProperty vpp(minPlane,maxPlane,colorDepthHomography,colorSize);
	
	while(true)
		{
		Kinect::FrameBuffer depthFrame,colorFrame;
		{
		Threads::MutexCond::Lock inputLock(inputCond);
		
		/* Wait until a new depth and color frame arrive, or the program shuts down: */
		while(runDetectionThread&&(lastInputDepthFrameVersion==inputDepthFrameVersion||lastInputColorFrameVersion==inputColorFrameVersion))
			inputCond.wait(inputLock);
		
		/* Bail out if the program is shutting down: */
		if(!runDetectionThread)
			break;
		
		/* Work on the new frames: */
		depthFrame=inputDepthFrame;
		colorFrame=inputColorFrame;
		lastInputDepthFrameVersion=inputDepthFrameVersion;
		lastInputColorFrameVersion=inputColorFrameVersion;
		}
		
		if(outputBlobsFunction!=0)
			{
			/* Set the most recent color frame in the pixel validator: */
			vpp.setColorFrame(colorFrame.getData<unsigned char>());
			
			/* Detect all objects in the depth frame between the min and max planes: */
			BlobList blobsCc;
			if(depthIsFloat)
				extractBlobs<float>(depthFrame,vpp,blobsCc);
			else
				extractBlobs<unsigned short>(depthFrame,vpp,blobsCc);
			
			/* Call the callback function: */
			(*outputBlobsFunction)(blobsCc);
			}
		}
	
	return 0;
	}

RainMaker::RainMaker(const unsigned int sDepthSize[2],const unsigned int sColorSize[2],const RainMaker::PTransform& sDepthProjection,const RainMaker::PTransform& sColorProjection,const RainMaker::Plane& basePlane,double minElevation,double maxElevation,int sMinBlobSize)
	:depthIsFloat(false),
	 outputBlobsFunction(0)
	{
	/* Remember the frame sizes: */
	for(int i=0;i<2;++i)
		depthSize[i]=sDepthSize[i];
	for(int i=0;i<2;++i)
		colorSize[i]=sColorSize[i];
	
	/* Remember the depth and color projections: */
	depthProjection=sDepthProjection;
	colorProjection=sColorProjection;
	
	/* Calculate the direct homography from depth image space to color image space: */
	PTransform hom=PTransform::scale(PTransform::Scale(double(colorSize[0]),double(colorSize[1]),1.0)); // Go to color image space
	hom*=colorProjection; // Go to color texture space
	
	/* Remove the superfluous z component row: */
	for(int i=0;i<2;++i)
		for(int j=0;j<4;++j)
			colorDepthHomography(i,j)=float(hom.getMatrix()(i,j));
	for(int j=0;j<4;++j)
		colorDepthHomography(2,j)=float(hom.getMatrix()(3,j));
	
	/* Initialize the input frame slot: */
	inputDepthFrameVersion=0;
	inputColorFrameVersion=0;
	
	/* Calculate the equations of the minimum and maximum elevation planes in camera space: */
	PTransform::HVector minPlaneCc(basePlane.getNormal());
	minPlaneCc[3]=-(basePlane.getOffset()+minElevation*basePlane.getNormal().mag());
	PTransform::HVector maxPlaneCc(basePlane.getNormal());
	maxPlaneCc[3]=-(basePlane.getOffset()+maxElevation*basePlane.getNormal().mag());
	
	/* Transform the plane equations to depth image space and flip and swap the min and max planes because elevation increases opposite to raw depth: */
	PTransform::HVector minPlaneDic(depthProjection.getMatrix().transposeMultiply(minPlaneCc));
	double minPlaneScale=-1.0/Geometry::mag(minPlaneDic.toVector());
	for(int i=0;i<4;++i)
		maxPlane[i]=float(minPlaneDic[i]*minPlaneScale);
	PTransform::HVector maxPlaneDic(depthProjection.getMatrix().transposeMultiply(maxPlaneCc));
	double maxPlaneScale=-1.0/Geometry::mag(maxPlaneDic.toVector());
	for(int i=0;i<4;++i)
		minPlane[i]=float(maxPlaneDic[i]*maxPlaneScale);
	
	/* Initialize the blob detector: */
	minBlobSize=sMinBlobSize;
	
	/* Start the object detection thread: */
	runDetectionThread=true;
	detectionThread.start(this,&RainMaker::detectionThreadMethod);
	}

RainMaker::~RainMaker(void)
	{
	/* Shut down the object detection thread: */
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	runDetectionThread=false;
	inputCond.signal();
	}
	detectionThread.join();
	
	/* Release all allocated resources: */
	delete outputBlobsFunction;
	}

void RainMaker::setDepthIsFloat(bool newDepthIsFloat)
	{
	depthIsFloat=newDepthIsFloat;
	}

void RainMaker::setOutputBlobsFunction(RainMaker::OutputBlobsFunction* newOutputBlobsFunction)
	{
	delete outputBlobsFunction;
	outputBlobsFunction=newOutputBlobsFunction;
	}

void RainMaker::receiveRawDepthFrame(const Kinect::FrameBuffer& newDepthFrame)
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	
	/* Store the new buffer in the input buffer: */
	inputDepthFrame=newDepthFrame;
	++inputDepthFrameVersion;
	
	/* Signal the background thread: */
	inputCond.signal();
	}

void RainMaker::receiveRawColorFrame(const Kinect::FrameBuffer& newColorFrame)
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	
	/* Store the new buffer in the input buffer: */
	inputColorFrame=newColorFrame;
	++inputColorFrameVersion;
	
	/* Signal the background thread: */
	inputCond.signal();
	}
