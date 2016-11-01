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

#include "HandExtractor.h"

#include <Misc/Utility.h>
#include <Misc/FunctionCalls.h>
#include <Math/Math.h>
#include <Math/Interval.h>
#include <Geometry/Vector.h>

// DEBUGGING
#include <iostream>

namespace {

/**************
Helper classes:
**************/

struct Span // Helper structure to extract foreground blobs from a depth image
	{
	/* Elements: */
	public:
	unsigned int y; // Row index of the span
	unsigned int start; // Starting column of span
	unsigned int end; // Ending column of span
	unsigned int parent; // Span's parent span
	unsigned int numPixels; // Number of pixels in the span's subtree
	unsigned int blobId; // Blob ID of a root span
	};

struct BlobOrigin // Helper structure to store a point on the border of a foreground blob
	{
	/* Elements: */
	public:
	bool assigned; // Flag if the blob origin has already been assigned
	unsigned int x,y; // Coordinates of blob origin in depth frame
	const unsigned short* biPtr; // Pointer to blob origin in blob ID image
	};

struct Corner // Helper class to store corners in blob images
	{
	/* Elements: */
	public:
	int cornerType; // Corner type, +1: finger tip, -1: finger nook
	unsigned start; // Boundary pixel index at which the corner started
	int x,y; // Corner position in depth frame
	};

typedef Math::Interval<float> Interval;
typedef Geometry::Point<float,2> Point2;
typedef Geometry::Vector<float,2> Vector2;

/****************
Helper functions:
****************/

void drawLine(Images::RGBImage& image,const Point2& p0,const Point2& p1,const Images::RGBImage::Color& color)
	{
	int w=int(image.getWidth());
	int h=int(image.getHeight());
	int x0=int(Math::floor(p0[0]));
	int y0=int(Math::floor(p0[1]));
	int x1=int(Math::floor(p1[0]));
	int y1=int(Math::floor(p1[1]));
	int dx=x1-x0;
	int dy=y1-y0;
	ptrdiff_t stride=w;
	if(Math::abs(dx)>Math::abs(dy))
		{
		/* X direction leads: */
		if(dx<0)
			{
			x0=x1;
			y0=y1;
			dx=-dx;
			dy=-dy;
			}
		Images::RGBImage::Color* lPtr=image.modifyPixels()+y0*stride+x0;
		int yf=dx/2;
		int y=0;
		for(int x=0;x<=dx;++x,++lPtr)
			{
			if(x0+x>=0&&x0+x<w&&y0+y>=0&&y0+y<h)
				*lPtr=color;
			yf+=dy;
			if(yf>=dx)
				{
				++y;
				lPtr+=stride;
				yf-=dx;
				}
			else if(yf<=-dx)
				{
				--y;
				lPtr-=stride;
				yf+=dx;
				}
			}
		}
	else
		{
		/* Y direction leads: */
		if(dy<0)
			{
			x0=x1;
			y0=y1;
			dx=-dx;
			dy=-dy;
			}
		Images::RGBImage::Color* lPtr=image.modifyPixels()+y0*stride+x0;
		int xf=dy/2;
		int x=0;
		for(int y=0;y<=dy;++y,lPtr+=stride)
			{
			if(x0+x>=0&&x0+x<w&&y0+y>=0&&y0+y<h)
				*lPtr=color;
			xf+=dx;
			if(xf>=dy)
				{
				++x;
				++lPtr;
				xf-=dy;
				}
			else if(xf<=-dy)
				{
				--x;
				--lPtr;
				xf+=dy;
				}
			}
		}
	}

void drawCircle(Images::RGBImage& image,const Point2& center,float radius,const Images::RGBImage::Color& color)
	{
	Images::RGBImage::Color* imgBase=image.modifyPixels();
	int size[2];
	size[0]=int(image.getSize(0));
	size[1]=int(image.getSize(1));
	
	/* Draw the circle: */
	int cx=int(Math::floor(center[0]));
	int cy=int(Math::floor(center[1]));
	int r=int(Math::floor(radius+0.5f));
	ptrdiff_t stride=ptrdiff_t(size[0]);
	#if 0 // Draw a filled rain circle
	int ymin=cy-r>=0?cy-r:0;
	int ymax=cy+r<=size[1]-1?cy+r:size[1]-1;
	for(int y=ymin;y<=ymax;++y)
		{
		int ry=int(Math::floor(Math::sqrt(float(r*r)-float((y-cy)*(y-cy)))+0.5f));
		int xmin=cx-ry>=0?cx-ry:0;
		int xmax=cx+ry<=size[0]-1?cx+ry:size[0]-1;
		Images::RGBImage::Color* iPtr=imgBase+(y*stride+xmin);
		for(int x=xmin;x<=xmax;++x,++iPtr)
			*iPtr=color;
		}
	#else // Draw a hollow rain circle
	Images::RGBImage::Color* imgCenter=imgBase+(cy*stride+cx);
	for(int y=0;;++y)
		{
		int x=int(Math::floor(Math::sqrt(float(r*r)-float(y*y))+0.5f));
		if(x<y)
			break;
		if(cy+y<size[1])
			{
			if(cx+x<size[0])
				imgCenter[y*stride+x]=color;
			if(cx-x>=0)
				imgCenter[y*stride-x]=color;
			}
		if(cy+x<size[1])
			{
			if(cx+y<size[0])
				imgCenter[x*stride+y]=color;
			if(cx-y>=0)
				imgCenter[x*stride-y]=color;
			}
		if(cy-y>=0)
			{
			if(cx+x<size[0])
				imgCenter[-y*stride+x]=color;
			if(cx-x>=0)
				imgCenter[-y*stride-x]=color;
			}
		if(cy-x>=0)
			{
			if(cx+y<size[0])
				imgCenter[-x*stride+y]=color;
			if(cx-y>=0)
				imgCenter[-x*stride-y]=color;
			}
		}
	#endif
	}

}

/**************************************
Static elements of class HandExtractor:
**************************************/

const unsigned short HandExtractor::invalidBlobId=0xffffU;
const int HandExtractor::walkDx[8]={ 1, 1, 0,-1,-1,-1, 0, 1};
const int HandExtractor::walkDy[8]={ 0, 1, 1, 1, 0,-1,-1,-1};

/******************************
Methods of class HandExtractor:
******************************/

void* HandExtractor::extractorThreadMethod(void)
	{
	unsigned int lastInputFrameVersion=0;
	
	while(true)
		{
		Kinect::FrameBuffer frame;
		{
		Threads::MutexCond::Lock inputLock(inputCond);
		
		/* Wait until a new frame arrives or the program shuts down: */
		while(runExtractorThread&&lastInputFrameVersion==inputFrameVersion)
			inputCond.wait(inputLock);
		
		/* Bail out if the program is shutting down: */
		if(!runExtractorThread)
			break;
		
		/* Work on the new frame: */
		frame=inputFrame;
		lastInputFrameVersion=inputFrameVersion;
		}
		
		/* Prepare a new output hand list: */
		HandList& newHandList=extractedHands.startNewValue();
		
		/* Extract hands from the new input frame: */
		extractHands(frame.getData<DepthPixel>(),newHandList,0);
		
		/* Finalize the new extracted hands list in the output buffer: */
		extractedHands.postNewValue();
		
		/* Pass the new output frame to the registered receiver: */
		if(handsExtractedFunction!=0)
			(*handsExtractedFunction)(newHandList);
		}
	
	return 0;
	}

HandExtractor::HandExtractor(const unsigned int sDepthFrameSize[2],const HandExtractor::PixelDepthCorrection* sPixelDepthCorrection,const PTransform& sDepthProjection)
	:pixelDepthCorrection(sPixelDepthCorrection),depthProjection(sDepthProjection),
	 inputFrameVersion(0),runExtractorThread(false),
	 maxFgDepth(0x07ffU-1U),maxDepthDist(1),minBlobSize(1500),maxBlobSize(150000),
	 blobIdImage(0),
	 snakeLength(50),snake(0),
	 maxCornerEnterDist(28),minCenterDist(10),minCornerExitDist(32),
	 minHandProbability(0.15f),
	 handsExtractedFunction(0)
	{
	/* Copy the depth frame size: */
	for(int i=0;i<2;++i)
		depthFrameSize[i]=sDepthFrameSize[i];
	
	/* Allocate the blob ID image: */
	blobIdImage=new unsigned short[(depthFrameSize[1]+2)*(depthFrameSize[0]+2)];
	biStride=depthFrameSize[0]+2;
	
	/* Initialize the border of the blob ID image: */
	unsigned short* biPtr=blobIdImage;
	for(unsigned int x=1;x<depthFrameSize[0]+2;++x,++biPtr)
		*biPtr=invalidBlobId;
	for(unsigned int y=1;y<depthFrameSize[1]+2;++y,biPtr+=biStride)
		*biPtr=invalidBlobId;
	for(unsigned int x=1;x<depthFrameSize[0]+2;++x,--biPtr)
		*biPtr=invalidBlobId;
	for(unsigned int y=1;y<depthFrameSize[1]+2;++y,biPtr-=biStride)
		*biPtr=invalidBlobId;
	
	/* Calculate the array of edge walking pointer offsets: */
	for(int i=0;i<8;++i)
		walkOffsets[i]=walkDy[i]*biStride+walkDx[i];
	
	/* Initialize the edge walking snake: */
	setSnakeLength(snakeLength);
	
	/* Start the hand extraction thread: */
	runExtractorThread=true;
	extractorThread.start(this,&HandExtractor::extractorThreadMethod);
	}

HandExtractor::~HandExtractor(void)
	{
	/* Shut down the extraction thread: */
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	runExtractorThread=false;
	inputCond.signal();
	}
	extractorThread.join();
	
	delete[] blobIdImage;
	delete[] snake;
	}

void HandExtractor::setMaxFgDepth(DepthPixel newMaxFgDepth)
	{
	maxFgDepth=newMaxFgDepth;
	}

void HandExtractor::setMaxDepthDist(unsigned int newMaxDepthDist)
	{
	maxDepthDist=newMaxDepthDist;
	}

void HandExtractor::setBlobSizeRange(unsigned int newMinBlobSize,unsigned int newMaxBlobSize)
	{
	minBlobSize=newMinBlobSize;
	maxBlobSize=newMaxBlobSize;
	}

void HandExtractor::setSnakeLength(unsigned int newSnakeLength)
	{
	snakeLength=newSnakeLength;
	
	/* Re-allocate the snake array: */
	delete[] snake;
	snake=new EdgePixel[snakeLength];
	}

void HandExtractor::setCornerDists(int newMaxCornerEnterDist,int newMinCenterDist,int newMinCornerExitDist)
	{
	maxCornerEnterDist=newMaxCornerEnterDist;
	minCenterDist=newMinCenterDist;
	minCornerExitDist=newMinCornerExitDist;
	}

void HandExtractor::extractHands(const HandExtractor::DepthPixel* depthFrame,HandExtractor::HandList& hands,Images::RGBImage* blobImage)
	{
	Images::RGBImage::Color* imgPtr=0;
	if(blobImage!=0)
		{
		/* Create the result image: */
		blobImage->clear(Images::RGBImage::Color(0,0,0));
		imgPtr=blobImage->replacePixels();
		}
	
	/* Extract all four-connected foreground blobs from the given depth frame: */
	std::vector<Span> spans;
	unsigned int numSpans=0;
	unsigned int lastRowSpan=0;
	const DepthPixel* dfRowPtr=depthFrame;
	for(unsigned int y=0;y<depthFrameSize[1];++y,dfRowPtr+=depthFrameSize[0])
		{
		const DepthPixel* dfPtr=dfRowPtr;
		unsigned int rowSpan=numSpans;
		unsigned int x=0;
		while(true)
			{
			/* Find the beginning of the next foreground span: */
			for(;x<depthFrameSize[0]&&*dfPtr>maxFgDepth;++x,++dfPtr)
				;
			if(x>=depthFrameSize[0])
				break;
			
			/* Start a new foreground span: */
			Span newSpan;
			newSpan.y=y;
			newSpan.start=x;
			
			/* Trace out the current foreground span: */
			DepthPixel lastDepth=*dfPtr;
			++x;
			++dfPtr;
			for(;x<depthFrameSize[0]&&*dfPtr<=maxFgDepth&&*dfPtr+maxDepthDist>=lastDepth&&*dfPtr<=lastDepth+maxDepthDist;++x,++dfPtr)
				lastDepth=*dfPtr;
			
			/* Finalize and store the new foreground span: */
			newSpan.end=x;
			newSpan.parent=numSpans;
			newSpan.numPixels=newSpan.end-newSpan.start;
			newSpan.blobId=invalidBlobId;
			spans.push_back(newSpan);
			++numSpans;
			
			/* Skip any spans from the previous row that were just passed by: */
			for(;lastRowSpan<rowSpan&&spans[lastRowSpan].end<newSpan.start;++lastRowSpan)
				;
			
			/* Check if the current span links up with any from the previous row: */
			for(unsigned int lrs=lastRowSpan;lrs<rowSpan&&spans[lrs].start<=newSpan.end;++lrs)
				{
				/* Check if the two spans have depth in common: */
				unsigned int o1=Misc::max(newSpan.start,spans[lrs].start);
				unsigned int o2=Misc::min(newSpan.end,spans[lrs].end);
				const DepthPixel* lrsPtr1=dfRowPtr+o1;
				const DepthPixel* lrsPtr0=lrsPtr1-depthFrameSize[0];
				bool canLink=false;
				for(unsigned int o=o1;o<o2&&!canLink;++o,++lrsPtr0,++lrsPtr1)
					canLink=*lrsPtr0+maxDepthDist>=*lrsPtr1&&*lrsPtr0<=*lrsPtr1+maxDepthDist;
				
				/* Merge the two spans if they can link: */
				if(canLink)
					{
					/* Find the roots of the two spans' respective subtrees: */
					unsigned int root1=lrs;
					while(root1!=spans[root1].parent)
						root1=spans[root1].parent;
					unsigned int root2=numSpans-1;
					while(root2!=spans[root2].parent)
						root2=spans[root2].parent;
					
					if(root1<root2)
						{
						/* Make the first span the new root: */
						spans[root2].parent=root1;
						spans[root1].numPixels+=spans[root2].numPixels;
						}
					else if(root1>root2)
						{
						/* Make the second span the new root: */
						spans[root1].parent=root2;
						spans[root2].numPixels+=spans[root1].numPixels;
						}
					}
				}
			}
		
		/* Skip any leftover spans from the previous row: */
		lastRowSpan=rowSpan;
		}
	
	/* Assign consecutive blob IDs to all root spans: */
	unsigned int nextBlobId=0;
	for(unsigned int i=0;i<numSpans;++i)
		{
		/* Check if the span is a root span: */
		if(spans[i].parent==i)
			{
			if(spans[i].numPixels>=minBlobSize&&spans[i].numPixels<=maxBlobSize)
				{
				spans[i].blobId=nextBlobId;
				++nextBlobId;
				}
			}
		else
			{
			/* Find the root of the span's subtree: */
			unsigned int root=spans[i].parent;
			while(root!=spans[root].parent)
				root=spans[root].parent;
			
			/* Assign the span's blob ID from the root: */
			spans[i].blobId=spans[root].blobId;
			}
		}
	
	#if 0
	
	/* Create the result color image: */
	static const Images::RGBImage::Color blobColors[18]=
		{
		Images::RGBImage::Color(255,0,0),
		Images::RGBImage::Color(255,255,0),
		Images::RGBImage::Color(0,255,0),
		Images::RGBImage::Color(0,255,255),
		Images::RGBImage::Color(0,0,255),
		Images::RGBImage::Color(255,0,255),
		Images::RGBImage::Color(128,0,0),
		Images::RGBImage::Color(128,128,0),
		Images::RGBImage::Color(0,128,0),
		Images::RGBImage::Color(0,128,128),
		Images::RGBImage::Color(0,0,128),
		Images::RGBImage::Color(128,0,128),
		Images::RGBImage::Color(255,128,128),
		Images::RGBImage::Color(255,255,128),
		Images::RGBImage::Color(128,255,128),
		Images::RGBImage::Color(128,255,255),
		Images::RGBImage::Color(128,128,255),
		Images::RGBImage::Color(255,128,255)
		};
	
	for(unsigned int i=0;i<numSpans;++i)
		{
		/* Find the span's root: */
		int root=i;
		while(spans[root].parent!=root)
			root=spans[root].parent;
		
		if(spans[root].blobId!=invalidBlobId)
			{
			/* Fill in the span: */
			Images::RGBImage::Color* cPtr=result.modifyPixelRow(spans[i].y)+spans[i].start;
			for(int x=spans[i].start;x<spans[i].end;++x,++cPtr)
				*cPtr=blobColors[spans[root].blobId%18];
			}
		}
	
	#endif
	
	/* Create an array of blob origin points: */
	BlobOrigin* blobOrigins=new BlobOrigin[nextBlobId];
	for(unsigned int i=0;i<nextBlobId;++i)
		blobOrigins[i].assigned=false;
	
	/* Create the blob ID image: */
	unsigned short* biRowPtr=blobIdImage+biStride+1;
	unsigned int spanIndex=0;
	for(unsigned int y=0;y<depthFrameSize[1];++y,biRowPtr+=biStride)
		{
		/* Process all spans and spaces between spans in the current row: */
		unsigned int x=0;
		unsigned short* biPtr=biRowPtr;
		while(true)
			{
			/* Find the start of the next span in the current row: */
			unsigned int nextSpanStart=depthFrameSize[0];
			if(spanIndex<numSpans&&spans[spanIndex].y==y)
				nextSpanStart=spans[spanIndex].start;
			
			/* Assign the invalid blob IDs until the start of the next span: */
			for(;x<nextSpanStart;++x,++biPtr)
				*biPtr=invalidBlobId;
			
			/* Bail out if the current row is done: */
			if(x==depthFrameSize[0])
				break;
			
			/* Check if the current span's blob is valid, and encountered for the first time: */
			unsigned int blobId=spans[spanIndex].blobId;
			if(blobId<nextBlobId&&!blobOrigins[blobId].assigned)
				{
				/* Store the beginning of the current span as the blob's origin: */
				blobOrigins[blobId].assigned=true;
				blobOrigins[blobId].x=x;
				blobOrigins[blobId].y=y;
				blobOrigins[blobId].biPtr=biPtr;
				}
			
			/* Assign the current span's blob ID: */
			for(;x<spans[spanIndex].end;++x,++biPtr)
				*biPtr=blobId;
			
			/* Go to the next span: */
			++spanIndex;
			}
		}
	
	/* Initialize the result list: */
	hands.clear();
	
	/* Walk around the edges of all foreground blobs in counter-clockwise order and decide whether they are hand-shaped: */
	EdgePixel* snakeEnd=snake+snakeLength;
	int enterDist2=Math::sqr(maxCornerEnterDist);
	int centerDist2=Math::sqr(minCenterDist);
	int exitDist2=Math::sqr(minCornerExitDist);
	std::vector<Corner> corners;
	corners.reserve(10);
	for(unsigned int blobId=0;blobId<nextBlobId;++blobId)
		{
		/* Initialize the edge-walking snake: */
		EdgePixel* snakeHead=snake;
		snakeHead->x=int(blobOrigins[blobId].x);
		snakeHead->y=int(blobOrigins[blobId].y);
		snakeHead->biPtr=blobOrigins[blobId].biPtr;
		unsigned int walkDir=0; // The blob origin is the bottom-left pixel of the blob, so 0 is the correct initial walking direction
		for(unsigned int i=1;i<snakeLength;++i)
			{
			/* Turn 90 degrees clockwise: */
			walkDir=(walkDir+6)&0x7U;
			
			/* Turn counter-clockwise until the next step stays in the same blob: */
			while(snakeHead->biPtr[walkOffsets[walkDir]]!=blobId)
				walkDir=(walkDir+1)&0x7U;
			
			/* Walk one step along the blob edge: */
			snakeHead[1].x=snakeHead->x+walkDx[walkDir];
			snakeHead[1].y=snakeHead->y+walkDy[walkDir];
			snakeHead[1].biPtr=snakeHead->biPtr+walkOffsets[walkDir];
			
			/* Move the snake head forward: */
			++snakeHead;
			}
		EdgePixel* snakeTail=snake;
		EdgePixel* snakeMid=snake+snakeLength/2;
		
		/* Walk the snake exactly once around the blob: */
		Corner corner;
		corner.cornerType=0;
		int cornerDist2=0;
		unsigned int pixelIndex=0;
		int firstCornerDist2=0;
		unsigned int firstCornerStart=0;
		do
			{
			/* Check if the current snake sits on a corner: */
			int newCornerType=0;
			int headTailDist2=Math::sqr(snakeHead->x-snakeTail->x)+Math::sqr(snakeHead->y-snakeTail->y);
			int centerElevation2=0;
			if(headTailDist2<=enterDist2)
				{
				/* Determine the type of corner by comparing the snake's center point against the line defined by its head and tail: */
				int nx=snakeTail->y-snakeHead->y;
				int ny=snakeHead->x-snakeTail->x;
				int d=nx*(snakeMid->x-snakeTail->x)+ny*(snakeMid->y-snakeTail->y);
				if(Math::sqr(d)>=centerDist2*headTailDist2)
					{
					/* Enter corner state: */
					if(d<0)
						newCornerType=1; // Finger tip
					else
						newCornerType=-1; // Finger nook
					if(headTailDist2>0)
						centerElevation2=Math::sqr(d)/headTailDist2;
					else
						centerElevation2=Math::sqr(snakeMid->x-snakeTail->x)+Math::sqr(snakeMid->y-snakeTail->y);
					}
				}
			
			/* Check if the snake changed corner type since the last step: */
			if(corner.cornerType!=newCornerType)
				{
				if(corner.cornerType!=0)
					{
					/* If the previous corner is the first, remember its corner distance: */
					if(corners.empty())
						firstCornerDist2=cornerDist2;
					
					/* Store the previous corner: */
					corners.push_back(corner);
					}
				
				if(newCornerType!=0)
					{
					/* Start a new corner: */
					corner.start=pixelIndex;
					corner.x=snakeMid->x;
					corner.y=snakeMid->y;
					cornerDist2=centerElevation2;
					
					/* If this is the first corner, remember its starting pixel: */
					if(corners.empty())
						firstCornerStart=pixelIndex;
					}
				
				/* Change the type of the current corner: */
				corner.cornerType=newCornerType;
				}
			else if(corner.cornerType!=0&&cornerDist2<centerElevation2)
				{
				/* Update the current corner: */
				corner.x=snakeMid->x;
				corner.y=snakeMid->y;
				cornerDist2=centerElevation2;
				}
			
			if(imgPtr!=0)
				{
				/* Draw the snake's center point: */
				Images::RGBImage::Color* cPtr=imgPtr+(snakeMid->y*depthFrameSize[0]+snakeMid->x);
				if(corner.cornerType==1)
					*cPtr=Images::RGBImage::Color(96,160,96);
				else if(corner.cornerType==-1)
					*cPtr=Images::RGBImage::Color(160,96,160);
				else
					{
					#if 1
					*cPtr=Images::RGBImage::Color(128,128,128);
					#else
					for(int i=0;i<3;++i)
						(*cPtr)[i]=(*cPtr)[i]+(255U-(*cPtr)[i])/2;
					#endif
					}
				}
			
			/* Walk one step along the blob edge: */
			walkDir=(walkDir+6)&0x7U; // Turn 90 degrees counter-clockwise
			while(snakeHead->biPtr[walkOffsets[walkDir]]!=blobId)
				walkDir=(walkDir+1)&0x7U;
			snakeTail->x=snakeHead->x+walkDx[walkDir];
			snakeTail->y=snakeHead->y+walkDy[walkDir];
			snakeTail->biPtr=snakeHead->biPtr+walkOffsets[walkDir];
			
			/* Move the snake head forward: */
			snakeHead=snakeTail;
			if(++snakeMid==snakeEnd)
				snakeMid=snake;
			if(++snakeTail==snakeEnd)
				snakeTail=snake;
			
			++pixelIndex;
			}
		while(snakeTail->biPtr!=blobOrigins[blobId].biPtr);
		
		if(corner.cornerType!=0)
			{
			if(!corners.empty()&&firstCornerStart==0&&corners.front().cornerType==corner.cornerType)
				{
				/* Merge the first and last corners: */
				if(firstCornerDist2<cornerDist2)
					{
					corners.front().x=corner.x;
					corners.front().y=corner.y;
					}
				}
			else
				{
				/* Store the last corner: */
				corners.push_back(corner);
				}
			}
		
		if(imgPtr!=0)
			{
			/* Draw all corners: */
			for(std::vector<Corner>::iterator cIt=corners.begin();cIt!=corners.end();++cIt)
				{
				Images::RGBImage::Color* cPtr=imgPtr+(cIt->y*depthFrameSize[0]+cIt->x);
				if(cIt->cornerType==1)
					*cPtr=Images::RGBImage::Color(0,255,0);
				else if(cIt->cornerType==-1)
					*cPtr=Images::RGBImage::Color(255,0,255);
				}
			}
		
		/* Check if the extracted set of corners matches a hand model: */
		float maxProb=minHandProbability;
		Point2 center=Point2::origin; // Hand center point
		float depth=0.0f; // Hand's average depth value
		float radius=0.0f; // Hand radius
		size_t numCorners=corners.size();
		if(numCorners>=8) // At least four finger tips, three nooks, and a thumb tip (thumb nook optional)
			{
			for(size_t i=0;i<numCorners;++i)
				{
				/* Check if the current corner starts a sequence of four tips interleaved with three nooks: */
				Corner& t0=corners[i];
				Corner& n1=corners[(i+1)%numCorners];
				Corner& t1=corners[(i+2)%numCorners];
				Corner& n2=corners[(i+3)%numCorners];
				Corner& t2=corners[(i+4)%numCorners];
				Corner& n3=corners[(i+5)%numCorners];
				Corner& t3=corners[(i+6)%numCorners];
				if(t0.cornerType==1&&
				   n1.cornerType==-1&&t1.cornerType==1&&
				   n2.cornerType==-1&&t2.cornerType==1&&
				   n3.cornerType==-1&&t3.cornerType==1)
					{
					/* Construct a hand model: */
					Point2 tp0(float(t0.x)+0.5f,float(t0.y)+0.5f);
					Point2 np1(float(n1.x)+0.5f,float(n1.y)+0.5f);
					Point2 tp1(float(t1.x)+0.5f,float(t1.y)+0.5f);
					Point2 np2(float(n2.x)+0.5f,float(n2.y)+0.5f);
					Point2 tp2(float(t2.x)+0.5f,float(t2.y)+0.5f);
					Point2 np3(float(n3.x)+0.5f,float(n3.y)+0.5f);
					Point2 tp3(float(t3.x)+0.5f,float(t3.y)+0.5f);
					
					/* Calculate the range of finger tip distances: */
					Interval tipDistance(Geometry::dist(tp0,tp1));
					tipDistance.addValue(Geometry::dist(tp1,tp2));
					tipDistance.addValue(Geometry::dist(tp2,tp3));
					
					/* Calculate the range of finger nook distances: */
					Interval nookDistance(Geometry::dist(np1,np2));
					nookDistance.addValue(Geometry::dist(np2,np3));
					
					/* Calculate finger root points: */
					Vector2 curve=Geometry::mid(np1,np3)-np2;
					Point2 rp0=np1+(np1-np2)*0.5f+curve;
					Point2 rp1=Geometry::mid(np1,np2);
					Point2 rp2=Geometry::mid(np2,np3);
					Point2 rp3=np3+(np3-np2)*0.5f+curve;
					
					/* Calculate the range of finger lengths: */
					Interval fingerLength(Geometry::dist(tp0,rp0));
					fingerLength.addValue(Geometry::dist(tp1,rp1));
					fingerLength.addValue(Geometry::dist(tp2,rp2));
					fingerLength.addValue(Geometry::dist(tp3,rp3));
					
					/* Calculate the probability that this is a hand: */
					float prob=1.0f;
					prob*=Math::sqr(tipDistance.getMin()/tipDistance.getMax());
					prob*=nookDistance.getMin()/nookDistance.getMax();
					prob*=fingerLength.getMin()/fingerLength.getMax();
					
					if(maxProb<prob)
						{
						/* Calculate finger length to nook distance ratio: */
						float fdNdRatio=Math::mid(Geometry::dist(tp1,rp1),Geometry::dist(tp2,rp2))/Math::mid(Geometry::dist(np1,np2),Geometry::dist(np2,np3));
						
						/* Calculate the hand center and radius: */
						float centerOffset=1.0f/fdNdRatio;
						center=Geometry::mid(Geometry::mid(rp0+(rp0-tp0)*centerOffset,rp1+(rp1-tp1)*centerOffset),
						                     Geometry::mid(rp2+(rp2-tp2)*centerOffset,rp3+(rp3-tp3)*centerOffset));
						center=Geometry::mid(rp1+(rp1-tp1)*centerOffset,rp2+(rp2-tp2)*centerOffset);
						radius=(Geometry::dist(center,tp0)+Geometry::dist(center,tp1)+Geometry::dist(center,tp2)+Geometry::dist(center,tp3))*0.25f;
						
						/* Calculate the hand's average depth in depth-corrected depth image space: */
						depth=0.0f;
						if(pixelDepthCorrection!=0)
							{
							ptrdiff_t t0Off=t0.y*depthFrameSize[0]+t0.x;
							depth+=pixelDepthCorrection[t0Off].correct(float(depthFrame[t0Off]));
							ptrdiff_t n1Off=n1.y*depthFrameSize[0]+n1.x;
							depth+=pixelDepthCorrection[n1Off].correct(float(depthFrame[n1Off]));
							ptrdiff_t t1Off=t1.y*depthFrameSize[0]+t1.x;
							depth+=pixelDepthCorrection[t1Off].correct(float(depthFrame[t1Off]));
							ptrdiff_t n2Off=n2.y*depthFrameSize[0]+n2.x;
							depth+=pixelDepthCorrection[n2Off].correct(float(depthFrame[n2Off]));
							ptrdiff_t t2Off=t2.y*depthFrameSize[0]+t2.x;
							depth+=pixelDepthCorrection[t2Off].correct(float(depthFrame[t2Off]));
							ptrdiff_t n3Off=n3.y*depthFrameSize[0]+n3.x;
							depth+=pixelDepthCorrection[n3Off].correct(float(depthFrame[n3Off]));
							ptrdiff_t t3Off=t3.y*depthFrameSize[0]+t3.x;
							depth+=pixelDepthCorrection[t3Off].correct(float(depthFrame[t3Off]));
							}
						else
							{
							depth+=float(depthFrame[t0.y*depthFrameSize[0]+t0.x]);
							depth+=float(depthFrame[n1.y*depthFrameSize[0]+n1.x]);
							depth+=float(depthFrame[t1.y*depthFrameSize[0]+t1.x]);
							depth+=float(depthFrame[n2.y*depthFrameSize[0]+n2.x]);
							depth+=float(depthFrame[t2.y*depthFrameSize[0]+t2.x]);
							depth+=float(depthFrame[n3.y*depthFrameSize[0]+n3.x]);
							depth+=float(depthFrame[t3.y*depthFrameSize[0]+t3.x]);
							}
						depth/=7.0f;
						
						maxProb=prob;
						
						if(imgPtr!=0)
							{
							/* Draw the hand: */
							drawLine(*blobImage,tp0,rp0,Images::RGBImage::Color(255,255,255));
							drawLine(*blobImage,tp1,rp1,Images::RGBImage::Color(255,255,255));
							drawLine(*blobImage,tp2,rp2,Images::RGBImage::Color(255,255,255));
							drawLine(*blobImage,tp3,rp3,Images::RGBImage::Color(255,255,255));
							drawCircle(*blobImage,center,radius,Images::RGBImage::Color(255,255,255));
							}
						}
					}
				}
			}
		
		/* Check if the blob matches a hand: */
		if(maxProb>minHandProbability)
			{
			// DEBUGGING
			// std::cout<<"Hand in depth space: "<<center[0]<<", "<<center[1]<<", "<<depth<<", "<<radius<<std::endl;
			
			/* Store the hand in camera space: */
			Hand newHand;
			newHand.center=depthProjection.transform(Point(center[0],center[1],depth));
			newHand.radius=Geometry::dist(newHand.center,depthProjection.transform(Point(center[0]+radius,center[1],depth)));
			hands.push_back(newHand);
			
			// DEBUGGING
			// std::cout<<"Hand in camera space: "<<newHand.center[0]<<", "<<newHand.center[1]<<", "<<newHand.center[2]<<", "<<newHand.radius<<std::endl;
			}
		
		/* Clean up: */
		corners.clear();
		}
	
	/* Clean up: */
	delete[] blobOrigins;
	}

void HandExtractor::setHandsExtractedFunction(HandExtractor::HandsExtractedFunction* newHandsExtractedFunction)
	{
	delete handsExtractedFunction;
	handsExtractedFunction=newHandsExtractedFunction;
	}

void HandExtractor::receiveRawFrame(const Kinect::FrameBuffer& newFrame)
	{
	Threads::MutexCond::Lock inputLock(inputCond);
	
	/* Store the new buffer in the input buffer: */
	inputFrame=newFrame;
	++inputFrameVersion;
	
	/* Signal the background thread: */
	inputCond.signal();
	}
