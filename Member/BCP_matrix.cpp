// Copyright (C) 2000, International Business Machines
// Corporation and others.  All Rights Reserved.
#include <numeric>
#include <algorithm>
#include <functional>

using std::sort;
using std::copy;
using std::transform;
using std::bind2nd;
using std::partial_sum;
using std::rotate;
using std::fill;

#include "BCP_error.hpp"
#include "BCP_matrix.hpp"

//#############################################################################

BCP_lp_relax::BCP_lp_relax(const BCP_lp_relax& mat) :
   OsiPackedMatrix(mat),
   _Objective(mat._Objective),
   _ColLowerBound(mat._ColLowerBound),
   _ColUpperBound(mat._ColUpperBound),
   _RowLowerBound(mat._RowLowerBound),
   _RowUpperBound(mat._RowUpperBound) {}

//-----------------------------------------------------------------------------

BCP_lp_relax&
BCP_lp_relax::operator=(const BCP_lp_relax& mat)
{
   OsiPackedMatrix::operator=(mat);
   _Objective = mat._Objective;
   _ColLowerBound = mat._ColLowerBound;
   _ColUpperBound = mat._ColUpperBound;
   _RowLowerBound = mat._RowLowerBound;
   _RowUpperBound = mat._RowUpperBound;
   return *this;
}

//-----------------------------------------------------------------------------

void
BCP_lp_relax::reserve(const int MaxColNum, const int MaxRowNum,
		      const int MaxNonzeros)
{
   OsiPackedMatrix::reserve(isColOrdered() ? MaxColNum : MaxRowNum,
			    MaxNonzeros);
   _Objective.reserve(MaxColNum);
   _ColLowerBound.reserve(MaxColNum);
   _ColUpperBound.reserve(MaxColNum);
   _RowLowerBound.reserve(MaxRowNum);
   _RowUpperBound.reserve(MaxRowNum);
}

//-----------------------------------------------------------------------------

void
BCP_lp_relax::clear() {
   OsiPackedMatrix::clear();
   _ColLowerBound.clear();  _ColUpperBound.clear();  _Objective.clear();
   _RowLowerBound.clear();  _RowUpperBound.clear();
}

//-----------------------------------------------------------------------------
#if 0
void 
BCP_lp_relax::add_col_set(const BCP_col_set& Cols)
{
   rightAppendPackedMatrix(Cols);
   _Objective.append(Cols.Objective());
   _ColLowerBound.append(Cols.LowerBound());
   _ColUpperBound.append(Cols.UpperBound());
}

//-----------------------------------------------------------------------------

void 
BCP_lp_relax::add_row_set(const BCP_row_set& Rows)
{
   bottomAppendPackedMatrix(Rows);
   _RowLowerBound.append(Rows.LowerBound());
   _RowUpperBound.append(Rows.UpperBound());
}
#endif
//-----------------------------------------------------------------------------

void 
BCP_lp_relax::erase_col_set(const BCP_vec<int>& pos)
{
   deleteCols(pos.size(), pos.begin());
   _Objective.erase_by_index(pos);
   _ColLowerBound.erase_by_index(pos);
   _ColUpperBound.erase_by_index(pos);
}

//-----------------------------------------------------------------------------

void 
BCP_lp_relax::erase_row_set(const BCP_vec<int>& pos)
{
   deleteRows(pos.size(), pos.begin());
   _RowLowerBound.erase_by_index(pos);
   _RowUpperBound.erase_by_index(pos);
}

//-----------------------------------------------------------------------------
#if 0
double 
BCP_lp_relax::dot_product_col(const int index,
			      const BCP_vec<double>& col) const
{
   if (! isColOrdered())
      throw BCP_fatal_error("\
LP: dot_product_col() called for a row ordered matrix...\n");
   return dot_product(index, col);
}

double 
BCP_lp_relax::dot_product_col(const int index,
			      const double* col) const
{
   if (_Ordering != ColWise)
      throw BCP_fatal_error("\
LP: dot_product_col() called for a row ordered matrix...\n");
   return dot_product(index, col);
}

//-----------------------------------------------------------------------------

double 
BCP_lp_relax::dot_product_row(const int index,
			    const BCP_vec<double>& row) const
{
   if (_Ordering != RowWise)
      throw BCP_fatal_error("\
LP: dot_product_col() called for a col ordered matrix...\n");
   return dot_product(index, row);
}

double 
BCP_lp_relax::dot_product_row(const int index,
			    const double* row) const
{
   if (_Ordering != RowWise)
      throw BCP_fatal_error("\
LP: dot_product_col() called for a col ordered matrix...\n");
   return dot_product(index, row);
}
#endif

//#############################################################################

void
BCP_lp_relax::BCP_createColumnOrderedMatrix(BCP_vec<BCP_row*>& rows,
					    BCP_vec<double>& CLB,
					    BCP_vec<double>& CUB,
					    BCP_vec<double>& OBJ)
{
  int i, nzcnt = 0;
  const int rownum = rows.size();
  
  for (i = 0; i < rownum; ++i)
    nzcnt += rows[i]->getNumElements();
  OsiPackedMatrix::reserve(rownum, nzcnt);
  OsiPackedMatrix::setDimensions(rownum, CLB.size());
  _RowLowerBound.reserve(rownum);
  _RowUpperBound.reserve(rownum);
  for (i = 0; i < rownum; ++i) {
    BCP_row* row = rows[i];
    OsiPackedMatrix::appendMajorVector(*row);
    _RowLowerBound.unchecked_push_back(row->LowerBound());
    _RowUpperBound.unchecked_push_back(row->UpperBound());
  }
  _ColLowerBound.swap(CLB);
  _ColUpperBound.swap(CUB);
  _Objective.swap(OBJ);
}

//-----------------------------------------------------------------------------

BCP_lp_relax::BCP_lp_relax(BCP_vec<BCP_row*>& rows,
			   BCP_vec<double>& CLB, BCP_vec<double>& CUB,
			   BCP_vec<double>& OBJ) :
  OsiPackedMatrix(false /*rowordered*/, 0, 0, 0, NULL, NULL, NULL, NULL)
{
  BCP_createColumnOrderedMatrix(rows, CLB, CUB, OBJ);
}

//-----------------------------------------------------------------------------

BCP_lp_relax::BCP_lp_relax(BCP_vec<BCP_row*>& rows,
			   BCP_vec<double>& CLB, BCP_vec<double>& CUB,
			   BCP_vec<double>& OBJ,
			   double extra_gap, double extra_major) :
  OsiPackedMatrix(false /*rowordered*/, 0, 0, 0, NULL, NULL, NULL, NULL)
{
  setExtraGap(extra_gap);
  setExtraMajor(extra_major);
  BCP_createColumnOrderedMatrix(rows, CLB, CUB, OBJ);
}

//#############################################################################

void
BCP_lp_relax::BCP_createRowOrderedMatrix(BCP_vec<BCP_col*>& cols,
					 BCP_vec<double>& RLB,
					 BCP_vec<double>& RUB)
{
  int i, nzcnt = 0;
  const int colnum = cols.size();
  
  for (i = 0; i < colnum; ++i)
    nzcnt += cols[i]->getNumElements();
  OsiPackedMatrix::reserve(colnum, nzcnt);
  OsiPackedMatrix::setDimensions(RLB.size(), colnum);
  _ColLowerBound.reserve(colnum);
  _ColUpperBound.reserve(colnum);
  _Objective.reserve(colnum);
  for (i = 0; i < colnum; ++i) {
    BCP_col* col = cols[i];
    OsiPackedMatrix::appendMajorVector(*col);
    _ColLowerBound.unchecked_push_back(col->LowerBound());
    _ColUpperBound.unchecked_push_back(col->UpperBound());
    _Objective.unchecked_push_back(col->Objective());
  }
  _RowLowerBound.swap(RLB);
  _RowUpperBound.swap(RUB);
}

//-----------------------------------------------------------------------------

BCP_lp_relax::BCP_lp_relax(BCP_vec<BCP_col*>& cols,
			   BCP_vec<double>& RLB, BCP_vec<double>& RUB) :
  OsiPackedMatrix(true /*colordered*/, 0, 0, 0, NULL, NULL, NULL, NULL)
{
  BCP_createRowOrderedMatrix(cols, RLB, RUB);
}

//-----------------------------------------------------------------------------

BCP_lp_relax::BCP_lp_relax(BCP_vec<BCP_col*>& cols,
			   BCP_vec<double>& RLB, BCP_vec<double>& RUB,
			   double extra_gap, double extra_major) :
  OsiPackedMatrix(true /*colordered*/, 0, 0, 0, NULL, NULL, NULL, NULL)
{
  setExtraGap(extra_gap);
  setExtraMajor(extra_major);
  BCP_createRowOrderedMatrix(cols, RLB, RUB);
}