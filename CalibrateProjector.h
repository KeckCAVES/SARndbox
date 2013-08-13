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

#ifndef CALIBRATEPROJECTOR_INCLUDED
#define CALIBRATEPROJECTOR_INCLUDED

#include <vector>
#include <Threads/Mutex.h>
#include <Threads/Cond.h>
#include <Threads/TripleBuffer.h>
#include <USB/Context.h>
#include <Math/Matrix.h>
#include <Geometry/Point.h>
#include <Geometry/AffineCombiner.h>
#include <Geometry/Plane.h>
#include <GL/gl.h>
#include <GL/GLColor.h>
#include <GL/GLObject.h>
#include <Images/ExtractBlobs.h>
#include <Vrui/Application.h>
#include <Vrui/Tool.h>
#include <Vrui/GenericToolFactory.h>
#include <Kinect/FrameBuffer.h>
#include <Kinect/Camera.h>

class CalibrateProjector:public Vrui::Application,public GLObject
	{
	/* Embedded classes: */
	private:
	typedef Kinect::FrameSource::DepthPixel DepthPixel; // Type for depth image pixels
	typedef Kinect::FrameSource::DepthCorrection::PixelCorrection PixelDepthCorrection; // Type for per-pixel depth correction factors
	typedef double Scalar; // Scalar type for points
	typedef Geometry::Point<Scalar,2> PPoint; // Point in 2D projection space
	typedef Geometry::Point<Scalar,3> OPoint; // Point in 3D object space
	typedef Geometry::Plane<Scalar,3> OPlane; // Plane in 3D object space
	
	struct DepthCentroidBlob:public Images::BboxBlob<Images::Blob<DepthPixel> > // Structure to calculate 3D centroids of blobs in depth image space
		{
		/* Embedded classes: */
		public:
		typedef DepthPixel Pixel;
		typedef Images::BboxBlob<Images::Blob<DepthPixel> > Base;
		
		struct Creator:public Base::Creator
			{
			/* Elements: */
			public:
			unsigned int frameSize[2]; // Size of depth images
			PixelDepthCorrection* pixelDepthCorrection; // Buffer of per-pixel depth correction coefficients
			Kinect::FrameSource::IntrinsicParameters::PTransform depthProjection; // Transformation from depth image space to camera space
			};
		
		/* Elements: */
		Kinect::FrameSource::IntrinsicParameters::PTransform::HVector c; // Accumulated centroid components (x, y, z) and total weight
		
		/* Constructors and destructors: */
		DepthCentroidBlob(unsigned int x,unsigned int y,const Pixel& pixel,const Creator& creator)
			:Base(x,y,pixel,creator)
			{
			/* Calculate the pixel's corrected depth value: */
			double px=double(x)+0.5;
			double py=double(y)+0.5;
			double pz=creator.pixelDepthCorrection[y*creator.frameSize[0]+x].correct(float(pixel));
			
			/* Unproject the pixel to calculate its centroid accumulation weight as camera-space z coordinate to the fourth: */
			const Kinect::FrameSource::IntrinsicParameters::PTransform::Matrix& m=creator.depthProjection.getMatrix();
			double weight=Math::sqr(Math::sqr((m(2,0)*px+m(2,1)*py+m(2,2)*pz+m(2,3))/(m(3,0)*px+m(3,1)*py+m(3,2)*pz+m(3,3))));
			
			/* Accumulate the pixel: */
			c[0]=px*weight;
			c[1]=py*weight;
			c[2]=pz*weight;
			c[3]=weight;
			}
		
		/* Methods: */
		void addPixel(unsigned int x,unsigned int y,const Pixel& pixel,const Creator& creator)
			{
			Base::addPixel(x,y,pixel,creator);
			
			/* Calculate the pixel's corrected depth value: */
			double px=double(x)+0.5;
			double py=double(y)+0.5;
			double pz=creator.pixelDepthCorrection[y*creator.frameSize[0]+x].correct(float(pixel));
			
			/* Unproject the pixel to calculate its centroid accumulation weight as camera-space z coordinate to the fourth: */
			const Kinect::FrameSource::IntrinsicParameters::PTransform::Matrix& m=creator.depthProjection.getMatrix();
			double weight=Math::sqr(Math::sqr((m(2,0)*px+m(2,1)*py+m(2,2)*pz+m(2,3))/(m(3,0)*px+m(3,1)*py+m(3,2)*pz+m(3,3))));
			
			/* Accumulate the pixel: */
			c[0]+=px*weight;
			c[1]+=py*weight;
			c[2]+=pz*weight;
			c[3]+=weight;
			}
		void merge(const DepthCentroidBlob& other,const Creator& creator)
			{
			Base::merge(other,creator);
			
			for(int i=0;i<4;++i)
				c[i]+=other.c[i];
			}
		OPoint getCentroid(const Kinect::FrameSource::IntrinsicParameters::PTransform& depthProjection) const // Returns the blob's centroid in camera space
			{
			return depthProjection.transform(c).toPoint();
			}
		};
	
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
	
	struct DataItem:public GLObject::DataItem
		{
		/* Elements: */
		public:
		GLuint blobImageTextureId; // ID of texture object holding the blob image
		GLfloat texMin[2],texMax[2]; // Texture coordinate rectangle to render the blob image texture
		unsigned int blobImageVersion; // Version number of blob image currently in texture object
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	private:
	int imageSize[2]; // Size of projector image
	int numTiePoints[2]; // Number of tie points in x and y
	OPlane basePlane; // Base plane of the configured sandbox area
	OPoint basePlaneCorners[4]; // Corners of the configured sandbox area
	unsigned int numTiePointFrames; // Number of frames to capture per tie point
	unsigned int numBackgroundFrames; // Number of frames to capture for background removal
	int blobMergeDepth; // Maximum depth difference between neighboring pixels in the same blob
	USB::Context usbContext; // USB device context
	Kinect::Camera* camera; // Pointer to Kinect camera defining the object space
	unsigned int frameSize[2]; // Size of the Kinect camera's depth frames in pixels
	PixelDepthCorrection* pixelDepthCorrection; // Buffer of per-pixel depth correction coefficients
	Kinect::FrameSource::IntrinsicParameters cameraIps; // Intrinsic parameters of the Kinect camera
	
	bool capturingBackground; // Flag if the Kinect camera is currently capturing a background frame
	
	Threads::TripleBuffer<Kinect::FrameBuffer> rawFrames; // Triple buffer for raw depth frames from the Kinect camera
	unsigned int* blobIdImage; // An image of blob IDs
	GLColor<GLubyte,3>* blobImage; // A texture image visualizing the current target tracking state
	unsigned int blobImageVersion; // Version counter for the blob image
	DepthCentroidBlob* currentBlob; // The currently selected target blob
	OPoint currentCentroid; // Centroid of the currently selected target blob in camera space
	
	bool capturingTiePoint; // Flag whether the main thread is currently capturing a tie point
	unsigned int numCaptureFrames; // Number of tie point frames still to capture
	Geometry::AffineCombiner<double,3> tiePointCombiner; // Combiner to average multiple tie point frames
	
	std::vector<TiePoint> tiePoints; // List of already captured tie points
	bool haveProjection; // Flag if a projection matrix has been computed
	Math::Matrix projection; // The current projection matrix
	
	/* Private methods: */
	void depthStreamingCallback(const Kinect::FrameBuffer& frameBuffer); // Callback receiving depth frames from the Kinect camera
	void backgroundCaptureCompleteCallback(Kinect::Camera& camera); // Callback when the Kinect camera is done capturing a background image
	
	/* Constructors and destructors: */
	public:
	CalibrateProjector(int& argc,char**& argv);
	virtual ~CalibrateProjector(void);
	
	/* Methods from Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	
	/* New methods: */
	void startBackgroundCapture(void); // Starts capturing a background frame
	void startTiePointCapture(void); // Starts capturing an averaged depth frame
	void calcCalibration(void); // Calculates the calibration transformation after all tie points have been collected
	};

#endif
