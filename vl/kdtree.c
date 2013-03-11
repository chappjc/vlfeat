/** @file kdtree.c
 ** @brief KD-tree - Definition
 ** @author Andrea Vedaldi, David Novotny
 **/

/*
Copyright (C) 2007-12 Andrea Vedaldi and Brian Fulkerson.
All rights reserved.

This file is part of the VLFeat library and is made available under
the terms of the BSD license (see the COPYING file).
*/

/**

<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->
@page kdtree KD-trees and forests
@author Andrea Vedaldi, David Novotny
<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->

@ref kdtree.h implements a KD-tree object, a data structure that can
efficiently index moderately dimensional vector spaces. Both
best-bin-first @cite{beis97shape} and randomized KD-tree forests are
implemented
@cite{silpa-anan08optimised},@cite{muja09fast}. Applications include
fast matching of feature descriptors.

- @ref kdtree-overview
- @ref kdtree-tech

<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->
@section kdtree-overview Overview
<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->

To create a ::VlKDForest object use ::vl_kdforest_new specifying the
dimensionality of the data and the number of trees in the forest.
With one tree only, the algorithm is analogous to @cite{beis97shape}
(best-bin KDTree). Multiple trees correspond to the randomized KDTree
forest as in @cite{silpa-anan08optimised},@cite{muja09fast}.

To let the KD-tree index some data use ::vl_kdforest_build. Note that
for efficiency KD-tree does not copy the data but retains a pointer to
it. Therefore the data must exist (and not change) until the KD-tree
is deleted. To delete the KD-tree object, use ::vl_kdforest_delete.

To find the N nearest neighbors to a query point first instantiate
a ::VlKDForestSearcher and then start search using a ::vl_kdforest_query
with the searcher object as an argument. To set a maximum number of
comparisons per query and calculate approximate nearest neighbors use
::vl_kdforest_set_max_num_comparisons.

<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->
@section kdtree-tech Technical details
<!-- ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  -->

::VlKDForest implements the best-bin-first kd-tree of @cite{beis97shape}.

<b>Construction.</b> Given a set of points @f$ x_1,\dots,x_n \in
\mathbb{R}^d @f$, the algorithm recursively partitions the @e d
dimensional Euclidean space @f$ \mathbb{R}^d @f$ into (hyper-)
rectangles.

Partitions are organized into a binary tree with the root
corresponding to the whole space @f$ \mathbb{R}^d @f$. The algorithm
refines each partition by dividing it into two halves by thresholding
along a given dimension. Both the splitting dimension and the
threshold are determined as a statistic of the data points contained
in the partition. The splitting dimension is the one which has largest
sample variance and the splitting threshold is either the sample mean
or the median. Leaves are atomic partitions and they contain a list of
zero or more data points (typically one).

<b>Querying.</b> Querying amounts to finding the N data points closer
to a given query point @f$ x_q \in \mathbb{R}^d @f$. This is done by
branch-and-bound. A search state is an active partition (initially the
root) and it is weighed by the lower bound on the distance of any
point in the partition and the query point. Such a lower bound is
trivial to compute because partitions are hyper-rectangles.

<b>Querying usage.</b> As said before a user has to create an instance
::VlKDForestSearcher using ::vl_kdforest_new_searcher in order to be able
to make queries. When a user wants to delete a KD-Tree all the searchers
bound to the given KD-Forest are erased automatically. If a user wants to
delete some of the searchers before the KD-Tree erase, he could do it
using the vl_kdforest_delete_searcher method.
**/

#include "kdtree.h"
#include "generic.h"
#include "random.h"
#include "mathop.h"
#include <stdlib.h>

#define VL_HEAP_prefix     vl_kdforest_search_heap
#define VL_HEAP_type       VlKDForestSearchState
#define VL_HEAP_cmp(v,x,y) (v[x].distanceLowerBound - v[y].distanceLowerBound)
#include "heap-def.h"

#define VL_HEAP_prefix     vl_kdtree_split_heap
#define VL_HEAP_type       VlKDTreeSplitDimension
#define VL_HEAP_cmp(v,x,y) (v[x].variance - v[y].variance)
#include "heap-def.h"

#define VL_HEAP_prefix     vl_kdforest_neighbor_heap
#define VL_HEAP_type       VlKDForestNeighbor
#define VL_HEAP_cmp(v,x,y) (v[y].distance - v[x].distance)
#include "heap-def.h"

/** ------------------------------------------------------------------
 ** @internal
 ** @brief Allocate a new node from the tree pool
 **/

static vl_uindex
vl_kdtree_node_new (VlKDTree * tree, vl_uindex parentIndex)
{
  VlKDTreeNode * node = NULL ;
  vl_uindex nodeIndex = tree->numUsedNodes ;
  tree -> numUsedNodes += 1 ;

  assert (tree->numUsedNodes <= tree->numAllocatedNodes) ;

  node = tree->nodes + nodeIndex ;
  node -> parent = parentIndex ;
  node -> lowerChild = 0 ;
  node -> upperChild = 0 ;
  node -> splitDimension = 0 ;
  node -> splitThreshold = 0 ;
  return nodeIndex ;
}

/** ------------------------------------------------------------------
 ** @internal
 ** @brief Compare KDTree index entries for sorting
 **/

VL_INLINE int
vl_kdtree_compare_index_entries (void const * a,
                                 void const * b)
{
  double delta =
    ((VlKDTreeDataIndexEntry const*)a) -> value -
    ((VlKDTreeDataIndexEntry const*)b) -> value ;
  if (delta < 0) return -1 ;
  if (delta > 0) return +1 ;
  return 0 ;
}

/** ------------------------------------------------------------------
 ** @internal
 ** @brief Build KDTree recursively
 ** @param forest forest to which the tree belongs.
 ** @param tree tree being built.
 ** @param nodeIndex node to process.
 ** @param dataBegin begin of data for this node.
 ** @param dataEnd end of data for this node.
 ** @param depth depth of this node.
 **/

static void
vl_kdtree_build_recursively
(VlKDForest * forest,
 VlKDTree * tree, vl_uindex nodeIndex,
 vl_uindex dataBegin, vl_uindex dataEnd,
 unsigned int depth)
{
  vl_uindex d, i, medianIndex, splitIndex ;
  VlKDTreeNode * node = tree->nodes + nodeIndex ;
  VlKDTreeSplitDimension * splitDimension ;

  /* base case: there is only one data point */
  if (dataEnd - dataBegin <= 1) {
    if (tree->depth < depth) tree->depth = depth ;
    node->lowerChild = - dataBegin - 1;
    node->upperChild = - dataEnd - 1 ;
    return ;
  }

  /* compute the dimension with largest variance > 0 */
  forest->splitHeapNumNodes = 0 ;
  for (d = 0 ; d < forest->dimension ; ++ d) {
    double mean = 0 ; /* unnormalized */
    double secondMoment = 0 ;
    double variance = 0 ;
    for (i = dataBegin ; i < dataEnd ; ++ i) {
      vl_index di = tree->dataIndex[i].index ;
      double datum ;
      switch(forest->dataType) {
        case VL_TYPE_FLOAT: datum = ((float const*)forest->data)
          [di * forest->dimension + d] ;
          break ;
        case VL_TYPE_DOUBLE: datum = ((double const*)forest->data)
          [di * forest->dimension + d] ;
          break ;
        default:
          abort() ;
      }
      mean += datum ;
      secondMoment += datum * datum ;
    }
    mean /= (dataEnd - dataBegin) ;
    secondMoment /= (dataEnd - dataBegin) ;
    variance = secondMoment - mean * mean ;

    if (variance == 0) continue ;

    /* keep splitHeapSize most varying dimensions */
    if (forest->splitHeapNumNodes < forest->splitHeapSize) {
      VlKDTreeSplitDimension * splitDimension
        = forest->splitHeapArray + forest->splitHeapNumNodes ;
      splitDimension->dimension = (unsigned int)d ;
      splitDimension->mean = mean ;
      splitDimension->variance = variance ;
      vl_kdtree_split_heap_push (forest->splitHeapArray, &forest->splitHeapNumNodes) ;
    } else {
      VlKDTreeSplitDimension * splitDimension = forest->splitHeapArray + 0 ;
      if (splitDimension->variance < variance) {
        splitDimension->dimension = (unsigned int)d ;
        splitDimension->mean = mean ;
        splitDimension->variance = variance ;
        vl_kdtree_split_heap_update (forest->splitHeapArray, forest->splitHeapNumNodes, 0) ;
      }
    }
  }

  /* additional base case: the maximum variance is equal to 0 (overlapping points) */
  if (forest->splitHeapNumNodes == 0) {
    node->lowerChild = - dataBegin - 1 ;
    node->upperChild = - dataEnd - 1 ;
    return ;
  }

  /* toss a dice to decide the splitting dimension (variance > 0) */
  splitDimension = forest->splitHeapArray
  + (vl_rand_uint32(forest->rand) % VL_MIN(forest->splitHeapSize, forest->splitHeapNumNodes)) ;

  node->splitDimension = splitDimension->dimension ;

  /* sort data along largest variance dimension */
  for (i = dataBegin ; i < dataEnd ; ++ i) {
    vl_index di = tree->dataIndex[i].index ;
    double datum ;
    switch (forest->dataType) {
      case VL_TYPE_FLOAT: datum = ((float const*)forest->data)
        [di * forest->dimension + splitDimension->dimension] ;
        break ;
      case VL_TYPE_DOUBLE: datum = ((double const*)forest->data)
        [di * forest->dimension + splitDimension->dimension] ;
        break ;
      default:
        abort() ;
    }
    tree->dataIndex [i] .value = datum ;
  }
  qsort (tree->dataIndex + dataBegin,
         dataEnd - dataBegin,
         sizeof (VlKDTreeDataIndexEntry),
         vl_kdtree_compare_index_entries) ;

  /* determine split threshold */
  switch (forest->thresholdingMethod) {
    case VL_KDTREE_MEAN :
      node->splitThreshold = splitDimension->mean ;
      for (splitIndex = dataBegin ;
           splitIndex < dataEnd && tree->dataIndex[splitIndex].value <= node->splitThreshold ;
           ++ splitIndex) ;
      splitIndex -= 1 ;
      /* If the mean does not provide a proper partition, fall back to
       * median. This usually happens if all points have the same
       * value and the zero variance test fails for numerical accuracy
       * reasons. In this case, also due to numerical accuracy, the
       * mean value can be smaller, equal, or larger than all
       * points. */
      if (dataBegin <= splitIndex && splitIndex + 1 < dataEnd) break ;

    case VL_KDTREE_MEDIAN :
      medianIndex = (dataBegin + dataEnd - 1) / 2 ;
      splitIndex = medianIndex ;
      node -> splitThreshold = tree->dataIndex[medianIndex].value ;
      break ;

    default:
      abort() ;
  }

  /* divide subparts */
  node->lowerChild = vl_kdtree_node_new (tree, nodeIndex) ;
  vl_kdtree_build_recursively (forest, tree, node->lowerChild, dataBegin, splitIndex + 1, depth + 1) ;

  node->upperChild = vl_kdtree_node_new (tree, nodeIndex) ;
  vl_kdtree_build_recursively (forest, tree, node->upperChild, splitIndex + 1, dataEnd, depth + 1) ;
}

/** ------------------------------------------------------------------
 ** @brief Create new KDForest object
 ** @param dataType type of data (::VL_TYPE_FLOAT or ::VL_TYPE_DOUBLE)
 ** @param dimension data dimensionality.
 ** @param numTrees number of trees in the forest.
 ** @param distance type of distance norm (::VlDistanceL1 or ::VlDistanceL2).
 ** @return new KDForest.
 **/

VL_EXPORT VlKDForest *
vl_kdforest_new (vl_type dataType,
                 vl_size dimension, vl_size numTrees, VlVectorComparisonType distance)
{
  VlKDForest * self = vl_malloc (sizeof(VlKDForest)) ;

  assert(dataType == VL_TYPE_FLOAT || dataType == VL_TYPE_DOUBLE) ;
  assert(dimension >= 1) ;
  assert(numTrees >= 1) ;

  self -> rand = vl_get_rand () ;
  self -> dataType = dataType ;
  self -> numData = 0 ;
  self -> data = 0 ;
  self -> dimension = dimension ;
  self -> numTrees = numTrees ;
  self -> trees = 0 ;
  self -> thresholdingMethod = VL_KDTREE_MEDIAN ;
  self -> splitHeapSize = VL_MIN(numTrees, VL_KDTREE_SPLIT_HEAP_SIZE) ;
  self -> splitHeapNumNodes = 0 ;
  self -> distance = distance;
  self -> maxNumNodes = 0;
  self -> numSearchers = 0;

  switch (self->dataType) {
    case VL_TYPE_FLOAT:
      self -> distanceFunction = (void(*)(void))
      vl_get_vector_comparison_function_f (distance) ;
      break;
    case VL_TYPE_DOUBLE :
      self -> distanceFunction = (void(*)(void))
      vl_get_vector_comparison_function_d (distance) ;
      break ;
    default :
      abort() ;
  }

  return self ;
}

/** ------------------------------------------------------------------
 ** @brief Create a KDForest searcher object, used for processing queries
 ** @param kdforest a forest to which the queries should be pointing.
 ** @return KDForest searcher object.
 **
 ** A searcher is an object attached to the forest which must be created
 ** before running the queries. Each query has to be invoked with the
 ** searcher as its argument.
 **
 ** When using a multi-threaded approach a user should at first instantiate
 ** a correct number of searchers - each used in one thread.
 ** Then in each thread a query to the given searcher could be run.
 **
 **/

VL_EXPORT VlKDForestSearcher *
vl_kdforest_new_searcher (VlKDForest * kdforest)
{

  VlKDForestSearcher * self = vl_malloc(sizeof(VlKDForestSearcher));

  self -> searchId = 0 ;
  self -> searchHeapNumNodes = 0 ;
  self -> searchNumComparisons = 0 ;
  self -> searchNumRecursions = 0 ;

  if(kdforest->numSearchers == 0) {
    kdforest->headSearcher = self;
    self->previous = NULL;
    self->next = NULL;
  } else {
    VlKDForestSearcher * lastSearcher = kdforest->headSearcher;

    while (1) {
      if(lastSearcher->next) {
        lastSearcher = lastSearcher->next;
      } else {
        lastSearcher->next = self;
        self->previous = lastSearcher;
        self->next = NULL;
        break;
      }
    }
  }

  kdforest->numSearchers++;

  self->forest = kdforest;
  self -> searchHeapArray = vl_malloc (sizeof(VlKDForestSearchState) * kdforest->maxNumNodes) ;
  self -> searchIdBook = vl_calloc (sizeof(vl_uindex), kdforest->numData) ;

  return self;
}

/** ------------------------------------------------------------------
 ** @brief Delete a KDForest searcher object, created by vl_kdforest_new_searcher
 ** @param searcher a searcher which should be erased.
 **/

VL_EXPORT void
vl_kdforest_delete_searcher (VlKDForestSearcher * searcher)
{

  if (searcher->previous && searcher->next) {
    searcher->previous->next = searcher->next;
    searcher->next->previous = searcher->previous;
  } else if (searcher->previous && !searcher->next) {
    searcher->previous->next = NULL;
  } else if (!searcher->previous && searcher->next) {
    searcher->next->previous = NULL;
    searcher->forest->headSearcher = searcher->next;
  } else {
    searcher->forest->headSearcher = NULL;
  }

  searcher->forest->numSearchers--;

  vl_free(searcher->searchHeapArray);
  vl_free(searcher->searchIdBook);
  vl_free(searcher);
}

VL_INLINE VlKDForestSearcher *
vl_kdforest_get_searcher (VlKDForest const * self, vl_uindex pos)
{
  VlKDForestSearcher * lastSearcher = self->headSearcher;
  vl_uindex sIdx;

  for(sIdx = 0; sIdx <= pos; sIdx++) {
    if(lastSearcher) {
      if(sIdx == pos) {
        return lastSearcher;
      } else {
        lastSearcher = lastSearcher->next;
      }
    } else {
      VL_PRINT("Search at idx %d does not exist -> aborting.", pos);
      abort();
    }
  }

  return NULL;
}

/** ------------------------------------------------------------------
 ** @brief Delete KDForest object
 ** @param self KDForest object to delete
 ** @sa ::vl_kdforest_new
 **/

VL_EXPORT void
vl_kdforest_delete (VlKDForest * self)
{
  vl_uindex ti ;

  if(self->numSearchers > 0) {
    VlKDForestSearcher * underDeletion = self->headSearcher;
    while (1) {
      vl_free(underDeletion->searchHeapArray);
      vl_free(underDeletion->searchIdBook);
      if(underDeletion->next) {
        underDeletion = underDeletion->next;
        vl_free(underDeletion->previous);
      } else {
        vl_free(underDeletion);
        break;
      }
    }
  }

  if (self->trees) {
    for (ti = 0 ; ti < self->numTrees ; ++ ti) {
      if (self->trees[ti]) {
        if (self->trees[ti]->nodes) vl_free (self->trees[ti]->nodes) ;
        if (self->trees[ti]->dataIndex) vl_free (self->trees[ti]->dataIndex) ;
        vl_free (self->trees[ti]) ;
      }
    }
    vl_free (self->trees) ;
  }
  vl_free (self) ;
}

/** ------------------------------------------------------------------
 ** @internal @brief Compute tree bounds recursively
 ** @param tree KDTree object instance.
 ** @param nodeIndex node index to start from.
 ** @param searchBounds 2 x numDimension array of bounds.
 **/

static void
vl_kdtree_calc_bounds_recursively (VlKDTree * tree,
                                   vl_uindex nodeIndex, double * searchBounds)
{
  VlKDTreeNode * node = tree->nodes + nodeIndex ;
  vl_uindex i = node->splitDimension ;
  double t = node->splitThreshold ;

  node->lowerBound = searchBounds [2 * i + 0] ;
  node->upperBound = searchBounds [2 * i + 1] ;

  //VL_PRINT("%f %f\n",node->lowerBound,node->upperBound);

  if (node->lowerChild > 0) {
    searchBounds [2 * i + 1] = t ;
    vl_kdtree_calc_bounds_recursively (tree, node->lowerChild, searchBounds) ;
    searchBounds [2 * i + 1] = node->upperBound ;
  }
  if (node->upperChild > 0) {
    searchBounds [2 * i + 0] = t ;
    vl_kdtree_calc_bounds_recursively (tree, node->upperChild, searchBounds) ;
    searchBounds [2 * i + 0] = node->lowerBound ;
  }
}

/** ------------------------------------------------------------------
 ** @brief Build KDTree from data
 ** @param self KDTree object
 ** @param numData number of data points.
 ** @param data pointer to the data.
 **
 ** The function builds the KDTree by processing the data @a data. For
 ** efficiency, KDTree does not copy the data, but retains a pointer to it.
 ** Therefore the data must survive (and not change) until the KDTree
 ** is deleted.
 **/

VL_EXPORT void
vl_kdforest_build (VlKDForest * self, vl_size numData, void const * data)
{
  vl_uindex di, ti ;
  vl_size maxNumNodes ;
  double * searchBounds;

  /* need to check: if alredy built, clean first */
  self->data = data ;
  self->numData = numData ;
  self->trees = vl_malloc (sizeof(VlKDTree*) * self->numTrees) ;
  maxNumNodes = 0 ;

  for (ti = 0 ; ti < self->numTrees ; ++ ti) {
    self->trees[ti] = vl_malloc (sizeof(VlKDTree)) ;
    self->trees[ti]->dataIndex = vl_malloc (sizeof(VlKDTreeDataIndexEntry) * self->numData) ;
    for (di = 0 ; di < self->numData ; ++ di) {
      self->trees[ti]->dataIndex[di].index = di ;
    }
    self->trees[ti]->numUsedNodes = 0 ;
    /* num. nodes of a complete binary tree with numData leaves */
    self->trees[ti]->numAllocatedNodes = 2 * self->numData - 1 ;
    self->trees[ti]->nodes = vl_malloc (sizeof(VlKDTreeNode) * self->trees[ti]->numAllocatedNodes) ;
    self->trees[ti]->depth = 0 ;
    vl_kdtree_build_recursively (self, self->trees[ti],
                                 vl_kdtree_node_new(self->trees[ti], 0), 0,
                                 self->numData, 0) ;
    maxNumNodes += self->trees[ti]->numUsedNodes ;
  }

  searchBounds = vl_malloc(sizeof(double) * 2 * self->dimension);

  for (ti = 0 ; ti < self->numTrees ; ++ ti) {
    double * iter = searchBounds  ;
    double * end = iter + 2 * self->dimension ;
    while (iter < end) {
      *iter++ = - VL_INFINITY_F ;
      *iter++ = + VL_INFINITY_F ;
    }

    vl_kdtree_calc_bounds_recursively (self->trees[ti], 0, searchBounds) ;
  }

  vl_free(searchBounds);
  self -> maxNumNodes = maxNumNodes;
}


/** ------------------------------------------------------------------
 ** @internal @brief
 **/

VL_EXPORT vl_uindex
vl_kdforest_query_recursively (VlKDForestSearcher * searcher,
                               VlKDTree * tree,
                               vl_uindex nodeIndex,
                               VlKDForestNeighbor * neighbors,
                               vl_size numNeighbors,
                               vl_size * numAddedNeighbors,
                               double dist,
                               void const * query)
{

  VlKDTreeNode const * node = tree->nodes + nodeIndex ;
  vl_uindex i = node->splitDimension ;
  vl_index nextChild, saveChild ;
  double delta, saveDist ;
  double x ;
  double x1 = node->lowerBound ;
  double x2 = node->splitThreshold ;
  double x3 = node->upperBound ;
  VlKDForestSearchState * searchState ;

  searcher->searchNumRecursions ++ ;

  switch (searcher->forest->dataType) {
    case VL_TYPE_FLOAT :
      x = ((float const*) query)[i] ;
      break ;
    case VL_TYPE_DOUBLE :
      x = ((double const*) query)[i] ;
      break ;
    default :
      abort() ;
  }

  /* base case: this is a leaf node */
  if (node->lowerChild < 0) {

    vl_index begin = - node->lowerChild - 1 ;
    vl_index end   = - node->upperChild - 1 ;
    vl_index iter ;

    for (iter = begin ;
         iter < end &&
         (searcher->forest->searchMaxNumComparisons == 0 ||
          searcher->searchNumComparisons < searcher->forest->searchMaxNumComparisons) ;
         ++ iter) {

      vl_index di = tree->dataIndex [iter].index ;

      /* multiple KDTrees share the database points and we must avoid
       * adding the same point twice */
      if (searcher->searchIdBook[di] == searcher->searchId) continue ;
      searcher->searchIdBook[di] = searcher->searchId ;

      /* compare the query to this point */
      switch (searcher->forest->dataType) {
        case VL_TYPE_FLOAT:
          dist = ((VlFloatVectorComparisonFunction)searcher->forest->distanceFunction)
                 (searcher->forest->dimension,
                  ((float const *)query),
                  ((float const*)searcher->forest->data) + di * searcher->forest->dimension) ;
          break ;
        case VL_TYPE_DOUBLE:
          dist = ((VlDoubleVectorComparisonFunction)searcher->forest->distanceFunction)
                 (searcher->forest->dimension,
                  ((double const *)query),
                  ((double const*)searcher->forest->data) + di * searcher->forest->dimension) ;
          break ;
        default:
          abort() ;
      }
      searcher->searchNumComparisons += 1 ;

      /* see if it should be added to the result set */
      if (*numAddedNeighbors < numNeighbors) {
        VlKDForestNeighbor * newNeighbor = neighbors + *numAddedNeighbors ;
        newNeighbor->index = di ;
        newNeighbor->distance = dist ;
        vl_kdforest_neighbor_heap_push (neighbors, numAddedNeighbors) ;
      } else {
        VlKDForestNeighbor * largestNeighbor = neighbors + 0 ;
        if (largestNeighbor->distance > dist) {
          largestNeighbor->index = di ;
          largestNeighbor->distance = dist ;
          vl_kdforest_neighbor_heap_update (neighbors, *numAddedNeighbors, 0) ;
        }
      }
    } /* next data point */


    return nodeIndex ;
  }

#if 0
  assert (x1 <= x2 && x2 <= x3) ;
  assert (node->lowerChild >= 0) ;
  assert (node->upperChild >= 0) ;
#endif

  /*
   *   x1  x2 x3
   * x (---|---]
   *   (--x|---]
   *   (---|x--]
   *   (---|---] x
   */

  delta = x - x2 ;
  saveDist = dist + delta*delta ;

  if (x <= x2) {
    nextChild = node->lowerChild ;
    saveChild = node->upperChild ;
    if (x <= x1) {
      delta = x - x1 ;
      saveDist -= delta*delta ;
    }
  } else {
    nextChild = node->upperChild ;
    saveChild = node->lowerChild ;
    if (x > x3) {
      delta = x - x3 ;
      saveDist -= delta*delta ;
    }
  }

  if (*numAddedNeighbors < numNeighbors || neighbors[0].distance > saveDist) {
    searchState = searcher->searchHeapArray + searcher->searchHeapNumNodes ;
    searchState->tree = tree ;
    searchState->nodeIndex = saveChild ;
    searchState->distanceLowerBound = saveDist ;
    vl_kdforest_search_heap_push (searcher->searchHeapArray ,
                                  &searcher->searchHeapNumNodes) ;
  }

  return vl_kdforest_query_recursively (searcher,
                                        tree,
                                        nextChild,
                                        neighbors,
                                        numNeighbors,
                                        numAddedNeighbors,
                                        dist,
                                        query) ;
}

/** ------------------------------------------------------------------
 ** @brief Query operation
 ** @param searcher KDTree searcher object instance.
 ** @param neighbors list of nearest neighbors found (output).
 ** @param numNeighbors number of nearest neighbors to find.
 ** @param query query point.
 ** @return number of tree leaves visited.
 **
 ** A neighbor is represented by an instance of the structure
 ** ::VlKDForestNeighbor. Each entry contains the index of the
 ** neighbor (this is an index into the KDTree data) and its distance
 ** to the query point. Neighbors are sorted by increasing distance.
 **/

VL_EXPORT vl_size
vl_kdforest_query (VlKDForestSearcher * searcher,
                   VlKDForestNeighbor * neighbors,
                   vl_size numNeighbors,
                   void const * query)
{

  vl_uindex i, ti ;
  vl_bool exactSearch = searcher->forest->searchMaxNumComparisons == 0 ;

  VlKDForestSearchState * searchState  ;
  vl_size numAddedNeighbors = 0 ;

  assert (neighbors) ;
  assert (numNeighbors > 0) ;
  assert (query) ;

  /* this number is used to differentiate a query from the next */
  searcher -> searchId += 1 ;
  searcher -> searchNumRecursions = 0 ;

  searcher->searchNumComparisons = 0 ;
  searcher->searchNumSimplifications = 0 ;

  /* put the root node into the search heap */
  searcher->searchHeapNumNodes = 0 ;
  for (ti = 0 ; ti < searcher->forest->numTrees ; ++ ti) {
    searchState = searcher->searchHeapArray + searcher->searchHeapNumNodes ;
    searchState -> tree = searcher->forest->trees[ti] ;
    searchState -> nodeIndex = 0 ;
    searchState -> distanceLowerBound = 0 ;

    vl_kdforest_search_heap_push (searcher->searchHeapArray, &searcher->searchHeapNumNodes) ;
  }

  /* branch and bound */
  while (exactSearch || searcher->searchNumComparisons < searcher->forest->searchMaxNumComparisons)
  {
    /* pop the next optimal search node */
    VlKDForestSearchState * searchState ;

    /* break if search space completed */
    if (searcher->searchHeapNumNodes == 0) {
      break ;
    }
    searchState = searcher->searchHeapArray +
                  vl_kdforest_search_heap_pop (searcher->searchHeapArray, &searcher->searchHeapNumNodes) ;
    /* break if no better solution may exist */
    if (numAddedNeighbors == numNeighbors &&
        neighbors[0].distance < searchState->distanceLowerBound) {
      searcher->searchNumSimplifications ++ ;
      break ;
    }
    vl_kdforest_query_recursively (searcher,
                                   searchState->tree,
                                   searchState->nodeIndex,
                                   neighbors,
                                   numNeighbors,
                                   &numAddedNeighbors,
                                   searchState->distanceLowerBound,
                                   query) ;
  }

  /* sort neighbors by increasing distance */
  for (i = numAddedNeighbors ; i < numNeighbors ; ++ i) {
    neighbors[i].index = -1 ;
    neighbors[i].distance = VL_NAN_F ;
  }

  while (numAddedNeighbors) {
    vl_kdforest_neighbor_heap_pop (neighbors, &numAddedNeighbors) ;
  }

  return searcher->searchNumComparisons ;
}




