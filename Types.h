/***********************************************************************
Types - Declarations of data types exchanged between the AR Sandbox
modules.
Copyright (c) 2014 Oliver Kreylos

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

#ifndef TYPES_INCLUDED
#define TYPES_INCLUDED

#include <Geometry/Point.h>
#include <Geometry/Vector.h>
#include <Geometry/Plane.h>
#include <Geometry/OrthogonalTransformation.h>
#include <Geometry/ProjectiveTransformation.h>

typedef double Scalar; // Scalar type for geometry objects
typedef Geometry::Point<Scalar,3> Point; // Type for 3D affine points
typedef Geometry::Vector<Scalar,3> Vector; // Type for 3D affine vectors
typedef Geometry::Plane<Scalar,3> Plane; // Type for 3D affine planes
typedef Geometry::OrthogonalTransformation<Scalar,3> OGTransform; // Type for 3D scaled rigid body transformations
typedef Geometry::ProjectiveTransformation<Scalar,3> PTransform; // Type for 3D projective transformations (4x4 matrices)

#endif
