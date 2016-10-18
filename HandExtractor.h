/***********************************************************************
HandExtractor - Class to identify hands from a depth image.
Copyright (c) 2015-2016 Oliver Kreylos

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

#ifndef HANDEXTRACTOR_INCLUDED
#define HANDEXTRACTOR_INCLUDED

#include <stddef.h>
#include <vector>
#include <Misc/SizedTypes.h>
#include <Threads/Thread.h>
#include <Threads/MutexCond.h>
#include <Threads/TripleBuffer.h>
#include <Images/RGBImage.h>
#include <Kinect/FrameBuffer.h>
#include <Kinect/FrameSource.h>

#include "Types.h"

/* Forward declarations: */
namespace Misc {
template <class ParameterParam>
class FunctionCall;
}

class HandExtractor
	{
	/* Embedded classes: */
	public:
	typedef Misc::UInt16 DepthPixel; // Type for depth frame pixels
	typedef Kinect::FrameSource::DepthCorrection::PixelCorrection PixelDepthCorrection; // Type for per-pixel depth correction factors
	
	struct Hand // Structure to report detected hand positions
		{
		/* Elements: */
		public:
		Point center; // Hand's center in depth image space
		double radius; // Hand's approximate radius in depth image space
		};
	
	typedef std::vector<Hand> HandList; // Type for lists of hand positions
	typedef Misc::FunctionCall<const HandList&> HandsExtractedFunction; // Type for functions called when a new hand list has been extracted
	
	private:
	struct EdgePixel // Helper structure storing an edge pixel of a blob
		{
		/* Elements: */
		public:
		int x,y; // Position of edge pixel in depth frame
		const unsigned short* biPtr; // Pointer to edge pixel in blob ID image
		};

	/* Elements: */
	private:
	unsigned int depthFrameSize[2]; // Size of incoming depth frames
	const PixelDepthCorrection* pixelDepthCorrection; // Buffer of per-pixel depth correction coefficients
	PTransform depthProjection; // Projective transformation from depth image space to camera space
	
	Threads::MutexCond inputCond; // Condition variable to signal arrival of a new input frame
	Kinect::FrameBuffer inputFrame; // The most recent input frame
	unsigned int inputFrameVersion; // Version number of input frame
	volatile bool runExtractorThread; // Flag to keep the background extraction thread running
	Threads::Thread extractorThread; // The background filtering thread
	
	DepthPixel maxFgDepth; // Maximum depth value for foreground blobs
	unsigned int maxDepthDist; // Maximum depth distance between adjacent pixels to belong to the same foreground blob
	unsigned int minBlobSize,maxBlobSize; // Minimum and maximum number of pixels to consider a blob a hand candidate
	unsigned short* blobIdImage; // Image of per-pixel blob IDs with one pixel boundary layer
	ptrdiff_t biStride; // Row stride in blob ID image
	static const unsigned short invalidBlobId; // Invalid blob ID
	static const int walkDx[8]; // Array of edge walking steps in clockwise order in x
	static const int walkDy[8]; // Array of edge walking steps in clockwise order in y
	ptrdiff_t walkOffsets[8]; // Array of pointer offsets for edge walking steps in clockwise order
	unsigned int snakeLength; // Length of the "snake" walking around blobs' edges to detect corners
	EdgePixel* snake; // Array of snake's pixels
	int maxCornerEnterDist; // Maximum distance between snake's head and tail to enter corner state
	int minCenterDist; // Minimum distance from snake's center to line defined by its head and tail to enter corner state
	int minCornerExitDist; // Minimum distance between snake's head and tail to leave corner state
	float minHandProbability; // Minimum probability rating at which to accept a blob as a hand
	
	Threads::TripleBuffer<HandList> extractedHands; // Triple buffer of lists of extracted hands
	HandsExtractedFunction* handsExtractedFunction; // Function called when a new list of extracted hands is ready
	
	/* Private methods: */
	void* extractorThreadMethod(void); // Method for the background hand extraction thread
	
	/* Constructors and destructors: */
	public:
	HandExtractor(const unsigned int sDepthFrameSize[2],const PixelDepthCorrection* sPixelDepthCorrection,const PTransform& sDepthProjection); // Creates a hand extractor for depth frames of the given size
	private:
	HandExtractor(const HandExtractor& source); // Prohibit copy constructor
	HandExtractor& operator=(const HandExtractor& source); // Prohibit assignment operator
	public:
	~HandExtractor(void);
	
	/* Methods: */
	DepthPixel getMaxFgDepth(void) const // Returns the maximum depth value for foreground blobs
		{
		return maxFgDepth;
		}
	void setMaxFgDepth(DepthPixel newMaxFgDepth); // Sets the maximum depth value for foreground blobs
	unsigned int getMaxDepthDist(void) const // Returns the maximum depth distance between adjacent pixels to belong to the same foreground blob
		{
		return maxDepthDist;
		}
	void setMaxDepthDist(unsigned int newMaxDepthDist); // Sets the maximum depth distance between adjacent pixels to belong to the same foreground blob
	unsigned int getMinBlobSize(void) const // Returns the minimum number of pixels to consider a blob a hand candidate
		{
		return minBlobSize;
		}
	unsigned int getMaxBlobSize(void) const // Returns the maximum number of pixels to consider a blob a hand candidate
		{
		return minBlobSize;
		}
	void setBlobSizeRange(unsigned int newMinBlobSize,unsigned int newMaxBlobSize); // Sets the range of numbers of pixels to consider a blob a hand candidate
	unsigned int getSnakeLength(void) const // Returns the length of the corner detection "snake"
		{
		return snakeLength;
		}
	void setSnakeLength(unsigned int newSnakeLength); // Sets the length of the corner detection "snake"
	int getMaxCornerEnterDist(void) const // Returns the maximum distance between snake's head and tail to enter corner state
		{
		return maxCornerEnterDist;
		}
	int getMinCenterDist(void) const // Returns the minimum distance from snake's center to line defined by its head and tail to enter corner state
		{
		return minCenterDist;
		}
	int getMinCornerExitDist(void) const // Returns the minimum distance between snake's head and tail to exit corner state
		{
		return minCornerExitDist;
		}
	void setCornerDists(int newMaxCornerEnterDist,int newMinCenterDist,int newMinCornerExitDist); // Sets distances between snake's head and tail to enter and exit corner state, respectively
	void extractHands(const DepthPixel* depthFrame,HandList& hands,Images::RGBImage* blobImage); // Extracts hands from the given depth frame
	void setHandsExtractedFunction(HandsExtractedFunction* newHandsExtractedFunction); // Sets the output function; adopts given functor object
	void receiveRawFrame(const Kinect::FrameBuffer& newFrame); // Called to receive a new raw depth frame
	bool lockNewExtractedHands(void) // Locks the most recently produced output list of extracted hands for reading; returns true if the locked list is new
		{
		return extractedHands.lockNewValue();
		}
	const HandList& getLockedExtractedHands(void) const // Returns the most recently locked output list of extracted hands
		{
		return extractedHands.getLockedValue();
		}
	};

#endif
