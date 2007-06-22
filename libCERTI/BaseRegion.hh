// ----------------------------------------------------------------------------
// CERTI - HLA RunTime Infrastructure
// Copyright (C) 2002-2005  ONERA
//
// This file is part of CERTI-libRTI
//
// CERTI-libRTI is free software ; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation ; either version 2 of
// the License, or (at your option) any later version.
//
// CERTI-libRTI is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY ; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program ; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
//
// $Id: BaseRegion.hh,v 3.2 2007/06/22 08:51:36 erk Exp $
// ----------------------------------------------------------------------------

#ifndef CERTI_BASE_REGION_HH
#define CERTI_BASE_REGION_HH

#include "Extent.hh"
#include "Handled.hh"

#include <vector>

namespace certi {

typedef Handle RegionHandle ;

class CERTI_EXPORT BaseRegion : public Handled<RegionHandle>
{
public:
    BaseRegion(RegionHandle);
    virtual ~BaseRegion();

    virtual ULong getRangeLowerBound(ExtentIndex, DimensionHandle) const
        throw (ArrayIndexOutOfBounds);

    virtual ULong getRangeUpperBound(ExtentIndex, DimensionHandle) const
        throw (ArrayIndexOutOfBounds);

    virtual void setRangeLowerBound(ExtentIndex, DimensionHandle, ULong)
        throw (ArrayIndexOutOfBounds);

    virtual void setRangeUpperBound(ExtentIndex, DimensionHandle, ULong)
        throw (ArrayIndexOutOfBounds);

    virtual ULong getNumberOfExtents() const
        throw ();

    virtual SpaceHandle getSpaceHandle() const
        throw () = 0 ;

    const std::vector<Extent> &getExtents() const ;
    void replaceExtents(const std::vector<Extent> &) throw (InvalidExtents);
    bool overlaps(const BaseRegion &region) const ;

protected:
    void setExtents(const std::vector<Extent> &);

    std::vector<Extent> extents ;
};

} // namespace certi

#endif // CERTI_BASE_REGION_HH
