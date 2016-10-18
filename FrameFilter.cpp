/***********************************************************************
FrameFilter - Class to filter streams of depth frames arriving from a
depth camera, with code to detect unstable values in each pixel, and
fill holes resulting from invalid samples.
Copyright (c) 2012-2016 Oliver Kreylos

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

#include "FrameFilter.h"

#include <Misc/FunctionCalls.h>
#include <Geometry/HVector.h>
#include <Geometry/Matrix.h>

/****************************
Methods of class FrameFilter:
****************************/

void* FrameFilter::filterThreadMethod(void)
	{
	unsigned int lastInputFrameVersion=0;
	
	while(true)
		{
		Kinect::FrameBuffer frame;
		{
		Threads::MutexCond::Lock inputLock(inputCond);
		
		/* Wait until a new frame arrives or the program shuts down: */
		while(runFilterThread&&lastInputFrameVersion==inputFrameVersion)
			inputCond.wait(inputLock);
		
		/* Bail out if the program is shutting down: */
		if(!runFilterThread)
			break;
		
		/* Work on the new frame: */
		frame=inputFrame;
		lastInputFrameVersion=inputFrameVersion;
		}
		
		/* Prepare a new output frame: */
		Kinect::FrameBuffer& newOutputFrame=outputFrames.startNewValue();
		
		/* Enter the new frame into the averaging buffer and calculate the output frame's pixel values: */
		const RawDepth* ifPtr=inputFrame.getData<RawDepth>();
		RawDepth* abPtr=averagingBuffer+averagingSlotIndex*size[1]*size[0];
		unsigned int* sPtr=statBuffer;
		float* ofPtr=validBuffer;
		float* nofPtr=newOutputFrame.getData<float>();
		const PixelDepthCorrection* pdcPtr=pixelDepthCorrection;
		for(unsigned int y=0;y<size[1];++y)
			{
			float py=float(y)+0.5f;
			for(unsigned int x=0;x<size[0];++x,++ifPtr,++pdcPtr,++abPtr,sPtr+=3,++ofPtr,++nofPtr)
				{
				float px=float(x)+0.5f;
				
				unsigned int oldVal=*abPtr;
				unsigned int newVal=*ifPtr;
				
				/* Depth-correct the new value: */
				float newCVal=pdcPtr->correct(newVal);
				
				/* Plug the depth-corrected new value into the minimum and maximum plane equations to determine its validity: */
				float minD=minPlane[0]*px+minPlane[1]*py+minPlane[2]*newCVal+minPlane[3];
				float maxD=maxPlane[0]*px+maxPlane[1]*py+maxPlane[2]*newCVal+maxPlane[3];
				if(minD>=0.0f&&maxD<=0.0f)
					{
					/* Store the new input value: */
					*abPtr=newVal;
					
					/* Update the pixel's statistics: */
					++sPtr[0]; // Number of valid samples
					sPtr[1]+=newVal; // Sum of valid samples
					sPtr[2]+=newVal*newVal; // Sum of squares of valid samples
					
					/* Check if the previous value in the averaging buffer was valid: */
					if(oldVal!=2048U)
						{
						--sPtr[0]; // Number of valid samples
						sPtr[1]-=oldVal; // Sum of valid samples
						sPtr[2]-=oldVal*oldVal; // Sum of squares of valid samples
						}
					}
				else if(!retainValids)
					{
					/* Store an invalid input value: */
					*abPtr=2048U;
					
					/* Check if the previous value in the averaging buffer was valid: */
					if(oldVal!=2048U)
						{
						--sPtr[0]; // Number of valid samples
						sPtr[1]-=oldVal; // Sum of valid samples
						sPtr[2]-=oldVal*oldVal; // Sum of squares of valid samples
						}
					}
				
				/* Check if the pixel is considered "stable": */
				if(sPtr[0]>=minNumSamples&&sPtr[2]*sPtr[0]<=maxVariance*sPtr[0]*sPtr[0]+sPtr[1]*sPtr[1])
					{
					/* Check if the new depth-corrected running mean is outside the previous value's envelope: */
					float newFiltered=pdcPtr->correct(float(sPtr[1])/float(sPtr[0]));
					if(Math::abs(newFiltered-*ofPtr)>=hysteresis)
						{
						/* Set the output pixel value to the depth-corrected running mean: */
						*nofPtr=*ofPtr=newFiltered;
						}
					else
						{
						/* Leave the pixel at its previous value: */
						*nofPtr=*ofPtr;
						}
					}
				else if(retainValids)
					{
					/* Leave the pixel at its previous value: */
					*nofPtr=*ofPtr;
					}
				else
					{
					/* Assign default value to instable pixels: */
					*nofPtr=instableValue;
					}
				}
			}
		
		/* Go to the next averaging slot: */
		if(++averagingSlotIndex==numAveragingSlots)
			averagingSlotIndex=0U;
		
		/* Apply a spatial filter if requested: */
		if(spatialFilter)
			{
			for(int filterPass=0;filterPass<2;++filterPass)
				{
				/* Low-pass filter the entire output frame in-place: */
				for(unsigned int x=0;x<size[0];++x)
					{
					/* Get a pointer to the current column: */
					float* colPtr=newOutputFrame.getData<float>()+x;
					
					/* Filter the first pixel in the column: */
					float lastVal=*colPtr;
					*colPtr=(colPtr[0]*2.0f+colPtr[size[0]])/3.0f;
					colPtr+=size[0];
					
					/* Filter the interior pixels in the column: */
					for(unsigned int y=1;y<size[1]-1;++y,colPtr+=size[0])
						{
						/* Filter the pixel: */
						float nextLastVal=*colPtr;
						*colPtr=(lastVal+colPtr[0]*2.0f+colPtr[size[0]])*0.25f;
						lastVal=nextLastVal;
						}
					
					/* Filter the last pixel in the column: */
					*colPtr=(lastVal+colPtr[0]*2.0f)/3.0f;
					}
				float* rowPtr=newOutputFrame.getData<float>();
				for(unsigned int y=0;y<size[1];++y)
					{
					/* Filter the first pixel in the row: */
					float lastVal=*rowPtr;
					*rowPtr=(rowPtr[0]*2.0f+rowPtr[1])/3.0f;
					++rowPtr;
					
					/* Filter the interior pixels in the row: */
					for(unsigned int x=1;x<size[0]-1;++x,++rowPtr)
						{
						/* Filter the pixel: */
						float nextLastVal=*rowPtr;
						*rowPtr=(lastVal+rowPtr[0]*2.0f+rowPtr[1])*0.25f;
						lastVal=nextLastVal;
						}
					
					/* Filter the last pixel in the row: */
					*rowPtr=(lastVal+rowPtr[0]*2.0f)/3.0f;
					++rowPtr;
					}
				}
			}
		
		/* Finalize the new output frame in the output buffer: */
		outputFrames.postNewValue();
		
		/* Pass the new output frame to the registered receiver: */
		if(outputFrameFunction!=0)
			(*outputFrameFunction)(newOutputFrame);
		}
	
	return 0;
	}

FrameFilter::FrameFilter(const unsigned int sSize[2],unsigned int sNumAveragingSlots,const FrameFilter::PixelDepthCorrection* sPixelDepthCorrection,const PTransform& depthProjection,const Plane& basePlane)
	:pixelDepthCorrection(sPixelDepthCorrection),
	 averagingBuffer(0),
	 statBuffer(0),
	 outputFrameFunction(0)
	{
	/* Remember the frame size: */
	for(int i=0;i<2;++i)
		size[i]=sSize[i];
	
	/* Initialize the input frame slot: */
	inputFrameVersion=0;
	
	/* Initialize the valid depth range: */
	setValidDepthInterval(0U,2046U);
	
	/* Initialize the averaging buffer: */
	numAveragingSlots=sNumAveragingSlots;
	averagingBuffer=new RawDepth[numAveragingSlots*size[1]*size[0]];
	RawDepth* abPtr=averagingBuffer;
	for(unsigned int i=0;i<numAveragingSlots;++i)
		for(unsigned int y=0;y<size[1];++y)
			for(unsigned int x=0;x<size[0];++x,++abPtr)
				*abPtr=2048U; // Mark sample as invalid
	averagingSlotIndex=0U;
	
	/* Initialize the statistics buffer: */
	statBuffer=new unsigned int[size[1]*size[0]*3];
	unsigned int* sbPtr=statBuffer;
	for(unsigned int y=0;y<size[1];++y)
		for(unsigned int x=0;x<size[0];++x)
			for(int i=0;i<3;++i,++sbPtr)
				*sbPtr=0;
	
	/* Initialize the stability criterion: */
	minNumSamples=(numAveragingSlots+1)/2;
	maxVariance=4;
	hysteresis=0.1f;
	retainValids=true;
	instableValue=0.0;
	
	/* Enable spatial filtering: */
	spatialFilter=true;
	
	/* Convert the base plane equation from camera space to depth-image space: */
	PTransform::HVector basePlaneCc(basePlane.getNormal());
	basePlaneCc[3]=-basePlane.getOffset();
	PTransform::HVector basePlaneDic(depthProjection.getMatrix().transposeMultiply(basePlaneCc));
	basePlaneDic/=Geometry::mag(basePlaneDic.toVector());
	
	/* Initialize the valid buffer: */
	validBuffer=new float[size[1]*size[0]];
	float* vbPtr=validBuffer;
	for(unsigned int y=0;y<size[1];++y)
		for(unsigned int x=0;x<size[0];++x,++vbPtr)
			*vbPtr=float(-((double(x)+0.5)*basePlaneDic[0]+(double(y)+0.5)*basePlaneDic[1]+basePlaneDic[3])/basePlaneDic[2]);
	
	/* Initialize the output frame buffer: */
	for(int i=0;i<3;++i)
		outputFrames.getBuffer(i)=Kinect::FrameBuffer(size[0],size[1],size[1]*size[0]*sizeof(float));
	
	/* Start the filtering thread: */
	runFilterThread=true;
	filterThread.start(this,&FrameFilter::filterThreadMethod);
	}

FrameFilter::~FrameFilter(void)
	{
	/* Shut down the filtering thread: */
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	runFilterThread=false;
	inputCond.signal();
	}
	filterThread.join();
	
	/* Release all allocated buffers: */
	delete[] averagingBuffer;
	delete[] statBuffer;
	delete[] validBuffer;
	delete outputFrameFunction;
	}

void FrameFilter::setValidDepthInterval(unsigned int newMinDepth,unsigned int newMaxDepth)
	{
	/* Set the equations for the minimum and maximum plane in depth image space: */
	minPlane[0]=0.0f;
	minPlane[1]=0.0f;
	minPlane[2]=1.0f;
	minPlane[3]=-float(newMinDepth)+0.5f;
	maxPlane[0]=0.0f;
	maxPlane[1]=0.0f;
	maxPlane[2]=1.0f;
	maxPlane[3]=-float(newMaxDepth)-0.5f;
	}

void FrameFilter::setValidElevationInterval(const PTransform& depthProjection,const Plane& basePlane,double newMinElevation,double newMaxElevation)
	{
	/* Calculate the equations of the minimum and maximum elevation planes in camera space: */
	PTransform::HVector minPlaneCc(basePlane.getNormal());
	minPlaneCc[3]=-(basePlane.getOffset()+newMinElevation*basePlane.getNormal().mag());
	PTransform::HVector maxPlaneCc(basePlane.getNormal());
	maxPlaneCc[3]=-(basePlane.getOffset()+newMaxElevation*basePlane.getNormal().mag());
	
	/* Transform the plane equations to depth image space and flip and swap the min and max planes because elevation increases opposite to raw depth: */
	PTransform::HVector minPlaneDic(depthProjection.getMatrix().transposeMultiply(minPlaneCc));
	double minPlaneScale=-1.0/Geometry::mag(minPlaneDic.toVector());
	for(int i=0;i<4;++i)
		maxPlane[i]=float(minPlaneDic[i]*minPlaneScale);
	PTransform::HVector maxPlaneDic(depthProjection.getMatrix().transposeMultiply(maxPlaneCc));
	double maxPlaneScale=-1.0/Geometry::mag(maxPlaneDic.toVector());
	for(int i=0;i<4;++i)
		minPlane[i]=float(maxPlaneDic[i]*maxPlaneScale);
	}

void FrameFilter::setStableParameters(unsigned int newMinNumSamples,unsigned int newMaxVariance)
	{
	minNumSamples=newMinNumSamples;
	maxVariance=newMaxVariance;
	}

void FrameFilter::setHysteresis(float newHysteresis)
	{
	hysteresis=newHysteresis;
	}

void FrameFilter::setRetainValids(bool newRetainValids)
	{
	retainValids=newRetainValids;
	}

void FrameFilter::setInstableValue(float newInstableValue)
	{
	instableValue=newInstableValue;
	}

void FrameFilter::setSpatialFilter(bool newSpatialFilter)
	{
	spatialFilter=newSpatialFilter;
	}

void FrameFilter::setOutputFrameFunction(FrameFilter::OutputFrameFunction* newOutputFrameFunction)
	{
	delete outputFrameFunction;
	outputFrameFunction=newOutputFrameFunction;
	}

void FrameFilter::receiveRawFrame(const Kinect::FrameBuffer& newFrame)
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	
	/* Store the new buffer in the input buffer: */
	inputFrame=newFrame;
	++inputFrameVersion;
	
	/* Signal the background thread: */
	inputCond.signal();
	}
