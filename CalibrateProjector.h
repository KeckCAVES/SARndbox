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

#ifndef CALIBRATEPROJECTOR_INCLUDED
#define CALIBRATEPROJECTOR_INCLUDED

#include <vector>
#include <Threads/TripleBuffer.h>
#include <Math/Matrix.h>
#include <Geometry/Point.h>
#include <Geometry/Vector.h>
#include <Geometry/Plane.h>
#include <Geometry/Box.h>
#include <Geometry/OrthonormalTransformation.h>
#include <Vrui/Application.h>
#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Kinect/Config.h>
#include <Kinect/ProjectorType.h>
#include <Kinect/ProjectorHeader.h>
#include <Kinect/DiskExtractor.h>

/* Forward declarations: */
namespace Kinect {
class FrameBuffer;
class FrameSource;
class DirectFrameSource;
}

class CalibrateProjector:public Vrui::Application
	{
	/* Embedded classes: */
	private:
	typedef Kinect::DiskExtractor::Scalar Scalar; // Scalar type
	typedef Kinect::DiskExtractor::Point OPoint; // Type for 3D points in object (camera) space
	typedef Kinect::DiskExtractor::Vector Vector; // Type for 3D vectors
	typedef Geometry::Point<Scalar,2> PPoint; // Type for 2D points in projection space
	typedef Geometry::Plane<Scalar,3> OPlane; // Type for planes
	typedef Geometry::Box<Scalar,3> Box; // Type for bounding boxes
	typedef Geometry::OrthonormalTransformation<Scalar,3> ONTransform; // Type for rigid body transformations
	
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
	int imageSize[2]; // Size of projector image
	int numTiePoints[2]; // Number of tie points in x and y
	OPlane basePlane; // Base plane of the configured sandbox area
	OPoint basePlaneCorners[4]; // Corners of the configured sandbox area
	ONTransform boxTransform; // Transformation from camera space to sandbox space (x along long sandbox axis, z up)
	Box bbox; // Bounding box around the sandbox area
	unsigned int numTiePointFrames; // Number of frames to capture per tie point
	unsigned int numBackgroundFrames; // Number of frames to capture for background removal
	
	Kinect::FrameSource* camera; // 3D video source to calibrate
	Kinect::DiskExtractor* diskExtractor; // Object to extract disk shapes from a 3D video stream
	Kinect::ProjectorType* projector; // A projector to render the 3D video stream
	bool capturingBackground; // Flag if the 3D camera is currently capturing a background frame
	bool capturingTiePoint; // Flag whether the main thread is currently capturing a tie point
	unsigned int numCaptureFrames; // Number of background or tie point frames still to capture
	
	Threads::TripleBuffer<Kinect::DiskExtractor::DiskList> diskList; // Triple buffer of lists of extracted disks
	std::vector<TiePoint> tiePoints; // List of collected calibration tie points
	int tiePointIndex; // Index of the next tie point to be collected
	bool haveProjection; // Flag if a projection matrix has been computed
	Math::Matrix projection; // The current projection matrix
	
	std::string projectionMatrixFileName; // Name of the file to which the projection matrix is saved
	
	/* Private methods: */
	void depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer); // Callback receiving depth frames from the 3D camera
	#if !KINECT_CONFIG_USE_SHADERPROJECTOR
	void meshStreamingCallback(const Kinect::MeshBuffer& meshBuffer); // Callback receiving projected meshes from the 3D video projector
	#endif
	void backgroundCaptureCompleteCallback(Kinect::DirectFrameSource& camera); // Callback when the 3D camera is done capturing a background image
	void diskExtractionCallback(const Kinect::DiskExtractor::DiskList& disks); // Called when a new list of disks has been extracted
	
	/* Constructors and destructors: */
	public:
	CalibrateProjector(int& argc,char**& argv);
	virtual ~CalibrateProjector(void);
	
	/* Methods from Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	
	/* New methods: */
	void startBackgroundCapture(void); // Starts capturing a background frame
	void startTiePointCapture(void); // Starts capturing an averaged depth frame
	void calcCalibration(void); // Calculates the calibration transformation after all tie points have been collected
	};

#endif
