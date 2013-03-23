/***********************************************************************
FindBlobs - Helper function to extract all eight-connected blobs of
pixels from a frame that match an arbitrary property.
Copyright (c) 2010-2013 Oliver Kreylos

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

#ifndef FINDBLOBS_INCLUDED
#define FINDBLOBS_INCLUDED

#include <vector>

template <class PixelParam>
class BlobProperty // Class to accumulate additional pixel properties along with blobs
	{
	/* Embedded classes: */
	public:
	typedef PixelParam Pixel; // Underlying pixel type
	
	/* Methods: */
	void addPixel(unsigned int x,unsigned int y,const PixelParam& pixelValue) // Adds a pixel to the property accumulator
		{
		}
	void merge(const BlobProperty& other) // Merges two blob property accumulators when their respecive blobs are merged
		{
		}
	};

template <class PixelParam>
struct Blob // Structure for extracted blobs
	{
	/* Embedded classes: */
	public:
	typedef PixelParam Pixel; // Underlying pixel type
	
	/* Elements: */
	public:
	double x,y; // Position of blob's centroid
	unsigned int min[2],max[2]; // Bounding box of blob
	BlobProperty<Pixel> blobProperty; // Additional accumulated blob property
	};

template <class PixelParam>
class PixelProperty // Class to check whether a pixel should be considered part of a blob
	{
	/* Embedded classes: */
	public:
	typedef PixelParam Pixel; // Underlying pixel type
	
	/* Methods: */
	bool operator()(unsigned int x,unsigned int y,const Pixel& pixel) const // Returns true if the given pixel satisfies the property
		{
		return false;
		}
	};

template <class PixelParam,class PixelPropertyParam>
std::vector<Blob<PixelParam> > findBlobs(const unsigned int size[2],const PixelParam* frame,const PixelPropertyParam& property); // Extracts all connected blobs from the given frame whose pixels have the given property

#ifndef FINDBLOBS_IMPLEMENTATION
#include "FindBlobs.icpp"
#endif

#endif
