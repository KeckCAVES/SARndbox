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

#ifndef RAINMAKER_INCLUDED
#define RAINMAKER_INCLUDED

#include <vector>
#include <Threads/Thread.h>
#include <Threads/MutexCond.h>
#include <Geometry/Point.h>
#include <Geometry/Matrix.h>
#include <Geometry/ProjectiveTransformation.h>
#include <Kinect/FrameBuffer.h>

/* Forward declarations: */
namespace Misc {
template <class ParameterParam>
class FunctionCall;
}
namespace Geometry {
template <class ScalarParam,int dimensionParam>
class Plane;
}
class ValidPixelProperty;

class RainMaker
	{
	/* Embedded classes: */
	public:
	typedef unsigned short RawDepth; // Data type for raw depth values
	typedef Geometry::Point<double,3> Point;
	typedef Geometry::Plane<double,3> Plane;
	typedef Geometry::ProjectiveTransformation<double,3> PTransform;
	
	struct Blob // Structure to describe a detected object in camera space
		{
		/* Elements: */
		public:
		Point centroid; // Object's centroid in camera space
		double radius; // Object's approximate radius in camera space
		};
	
	typedef std::vector<Blob> BlobList; // Type for lists of detected objects
	typedef Misc::FunctionCall<const BlobList&> OutputBlobsFunction; // Type for functions called when a new object list has been extracted
	
	/* Elements: */
	private:
	unsigned int depthSize[2]; // Width and height of incoming depth frames
	bool depthIsFloat; // Flag whether the incoming depth frames have float pixel values
	unsigned int colorSize[2]; // Width and height of incoming color frames
	PTransform depthProjection; // Projective transformation from depth image space to camera space
	PTransform colorProjection; // Projective transformation from camera space to color image space
	Geometry::Matrix<float,3,4> colorDepthHomography; // Homography from 3D depth image space into 2D color image space
	float minPlane[4]; // Plane equation of the lower bound of valid depth values in depth image space
	float maxPlane[4]; // Plane equation of the upper bound of valid depth values in depth image space
	int minBlobSize; // Minimum size of objects to be detected
	Threads::MutexCond inputCond; // Condition variable to signal arrival of a new input frame
	Kinect::FrameBuffer inputDepthFrame; // The most recent input depth frame
	unsigned int inputDepthFrameVersion; // Version number of input depth frame
	Kinect::FrameBuffer inputColorFrame; // The most recent input color frame
	unsigned int inputColorFrameVersion; // Version number of input color frame
	volatile bool runDetectionThread; // Flag to keep the background object detection thread running
	Threads::Thread detectionThread; // The background object detection thread
	OutputBlobsFunction* outputBlobsFunction; // Function called when a new (potentially empty) object list has been extracted
	
	/* Private methods: */
	template <class DepthPixelParam>
	void extractBlobs(const Kinect::FrameBuffer& depthFrame,const ValidPixelProperty& vpp,BlobList& blobsCc);
	void* detectionThreadMethod(void); // Method for the object detection thread
	
	/* Constructors and destructors: */
	public:
	RainMaker(const unsigned int sDepthSize[2],const unsigned int sColorSize[2],const PTransform& sDepthProjection,const PTransform& sColorProjection,const Plane& basePlane,double minElevation,double maxElevation,int sMinBlobSize); // Creates an object detector for frames of the given size and the given range of elevation values relative to the given base plane in camera space
	~RainMaker(void); // Destroys the object detector
	
	/* Methods: */
	void setDepthIsFloat(bool newDepthIsFloat); // Sets whether incoming depth frames have float pixel values
	void setOutputBlobsFunction(OutputBlobsFunction* newOutputBlobsFunction); // Sets the output function; adopts given functor object
	void receiveRawDepthFrame(const Kinect::FrameBuffer& newDepthFrame); // Called to receive a new raw depth frame
	void receiveRawColorFrame(const Kinect::FrameBuffer& newColorFrame); // Called to receive a new raw color frame
	};

#endif
