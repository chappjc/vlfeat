/** @file kdtree.h
 ** @brief KD-tree (@ref kdtree)
 ** @author Andrea Vedaldi, David Novotny
 **/

/*
Copyright (C) 2007-12 Andrea Vedaldi and Brian Fulkerson.
All rights reserved.

This file is part of the VLFeat library and is made available under
the terms of the BSD license (see the COPYING file).
*/

#ifndef VL_KDTREE_H
#define VL_KDTREE_H

#include "generic.h"
#include "mathop.h"

#define VL_KDTREE_SPLIT_HEAP_SIZE 5

typedef struct _VlKDTreeNode VlKDTreeNode ;
typedef struct _VlKDTreeSplitDimension VlKDTreeSplitDimension ;
typedef struct _VlKDTreeDataIndexEntry VlKDTreeDataIndexEntry ;
typedef struct _VlKDForestSearchState VlKDForestSearchState ;

struct _VlKDTreeNode
{
  vl_uindex parent ;
  vl_index lowerChild ;
  vl_index upperChild ;
  unsigned int splitDimension ;
  double splitThreshold ;
  double lowerBound ;
  double upperBound ;
} ;

struct _VlKDTreeSplitDimension
{
  unsigned int dimension ;
  double mean ;
  double variance ;
} ;

struct _VlKDTreeDataIndexEntry
{
  vl_index index ;
  double value ;
} ;



/** @brief Thresholding method */
typedef enum _VlKDTreeThresholdingMethod
{
  VL_KDTREE_MEDIAN,
  VL_KDTREE_MEAN
} VlKDTreeThresholdingMethod ;

/** @brief Neighbor of a query point */
typedef struct _VlKDForestNeighbor {
  double distance ;   /**< distance to the query point */
  vl_uindex index ;   /**< index of the neighbor in the KDTree data */
} VlKDForestNeighbor ;

typedef struct _VlKDTree
{
  VlKDTreeNode * nodes ;
  vl_size numUsedNodes ;
  vl_size numAllocatedNodes ;
  VlKDTreeDataIndexEntry * dataIndex ;
  unsigned int depth ;
} VlKDTree ;

struct _VlKDForestSearchState
{
  VlKDTree * tree ;
  vl_uindex nodeIndex ;
  double distanceLowerBound ;
} ;

struct _VlKDForestSearcher;

/** @brief KDForest object */
typedef struct _VlKDForest
{
  vl_size dimension ;

  /* random number generator */
  VlRand * rand ;

  /* indexed data */
  vl_type dataType ;
  void const * data ;
  vl_size numData ;
  VlVectorComparisonType distance;
  void (*distanceFunction)(void) ;

  /* tree structure */
  VlKDTree ** trees ;
  vl_size numTrees ;

  /* build */
  VlKDTreeThresholdingMethod thresholdingMethod ;
  VlKDTreeSplitDimension splitHeapArray [VL_KDTREE_SPLIT_HEAP_SIZE] ;
  vl_size splitHeapNumNodes ;
  vl_size splitHeapSize ;
  vl_size maxNumNodes;

  /* query */
  vl_size searchMaxNumComparisons ;
  vl_size numSearchers;
  struct _VlKDForestSearcher * headSearcher;   /**< head of the double linked list with searchers */

} VlKDForest ;

/** @brief KDForest searcher object */
struct _VlKDForestSearcher
{

  /* maintain a linked list of searchers for later disposal*/
  struct _VlKDForestSearcher * next;
  struct _VlKDForestSearcher * previous;

  vl_uindex * searchIdBook ;
  VlKDForestSearchState * searchHeapArray ;
  VlKDForest * forest;

  vl_size searchNumComparisons;
  vl_size searchNumRecursions ;
  vl_size searchNumSimplifications ;

  vl_size searchHeapNumNodes ;
  vl_uindex searchId ;

};

typedef struct _VlKDForestSearcher VlKDForestSearcher ;

/** @name Creating, copying and disposing
 ** @{ */
VL_EXPORT VlKDForest * vl_kdforest_new (vl_type dataType,
                                        vl_size dimension, vl_size numTrees, VlVectorComparisonType normType) ;
VL_EXPORT VlKDForestSearcher * vl_kdforest_new_searcher (VlKDForest * kdforest);
VL_EXPORT void vl_kdforest_delete (VlKDForest * self) ;
VL_EXPORT void vl_kdforest_delete_searcher (VlKDForestSearcher * searcher) ;
/** @} */

/** @name Building and querying
 ** @{ */
VL_EXPORT void vl_kdforest_build (VlKDForest * self,
                                  vl_size numData,
                                  void const * data) ;
VL_EXPORT vl_size vl_kdforest_query (VlKDForestSearcher * searcher,
                                     VlKDForestNeighbor * neighbors,
                                     vl_size numNeighbors,
                                     void const * query) ;
/** @} */

/** @name Retrieving and setting parameters
 ** @{ */
VL_INLINE vl_size vl_kdforest_get_depth_of_tree (VlKDForest const * self, vl_uindex treeIndex) ;
VL_INLINE vl_size vl_kdforest_get_num_nodes_of_tree (VlKDForest const * self, vl_uindex treeIndex) ;
VL_INLINE vl_size vl_kdforest_get_num_trees (VlKDForest const * self) ;
VL_INLINE vl_size vl_kdforest_get_data_dimension (VlKDForest const * self) ;
VL_INLINE vl_type vl_kdforest_get_data_type (VlKDForest const * self) ;
VL_INLINE void vl_kdforest_set_max_num_comparisons (VlKDForest * self, vl_size n) ;
VL_INLINE vl_size vl_kdforest_get_max_num_comparisons (VlKDForest * self) ;
VL_INLINE void vl_kdforest_set_thresholding_method (VlKDForest * self, VlKDTreeThresholdingMethod method) ;
VL_INLINE VlKDTreeThresholdingMethod vl_kdforest_get_thresholding_method (VlKDForest const * self) ;
VL_INLINE VlKDForest * vl_kdforest_searcher_get_forest (VlKDForestSearcher const * self) ;
VL_INLINE VlKDForestSearcher * vl_kdforest_get_searcher (VlKDForest const * self, vl_uindex pos) ;
/** @} */

/** ------------------------------------------------------------------
 ** @brief Get the number of nodes of a given tree
 ** @param self KDForest object.
 ** @param treeIndex index of the tree.
 ** @return number of trees.
 **/

vl_size
vl_kdforest_get_num_nodes_of_tree (VlKDForest const * self, vl_uindex treeIndex)
{
  assert (treeIndex < self->numTrees) ;
  return self->trees[treeIndex]->numUsedNodes ;
}

/** ------------------------------------------------------------------
 ** @brief Get the detph of a given tree
 ** @param self KDForest object.
 ** @param treeIndex index of the tree.
 ** @return number of trees.
 **/

vl_size
vl_kdforest_get_depth_of_tree (VlKDForest const * self, vl_uindex treeIndex)
{
  assert (treeIndex < self->numTrees) ;
  return self->trees[treeIndex]->depth ;
}

/** ------------------------------------------------------------------
 ** @brief Get the number of trees in the forest
 **
 ** @param self KDForest object.
 ** @return number of trees.
 **/

vl_size
vl_kdforest_get_num_trees (VlKDForest const * self)
{
  return self->numTrees ;
}

/** ------------------------------------------------------------------
 ** @brief Set the maximum number of comparisons for a search
 **
 ** @param self KDForest object.
 ** @param n maximum number of leaves.
 **
 ** This function sets the maximum number of comparisons for a
 ** nearest neighbor search. Setting it to 0 means unbounded comparisons.
 **
 ** @sa ::vl_kdforest_query, ::vl_kdforest_get_max_num_comparisons.
 **/

void
vl_kdforest_set_max_num_comparisons (VlKDForest * self, vl_size n)
{
  self->searchMaxNumComparisons = n ;
}

/** ------------------------------------------------------------------
 ** @brief Get the maximum number of comparisons for a search
 **
 ** @param self KDForest object.
 ** @return maximum number of leaves.
 **
 ** @sa ::vl_kdforest_set_max_num_comparisons.
 **/

vl_size
vl_kdforest_get_max_num_comparisons (VlKDForest * self)
{
  return self->searchMaxNumComparisons ;
}

/** ------------------------------------------------------------------
 ** @brief Set the thresholding method
 ** @param self KDForest object.
 ** @param method one of ::VlKDTreeThresholdingMethod.
 **
 ** @sa ::vl_kdforest_get_thresholding_method
 **/

VL_INLINE void
vl_kdforest_set_thresholding_method (VlKDForest * self, VlKDTreeThresholdingMethod method)
{
  assert(method == VL_KDTREE_MEDIAN || method == VL_KDTREE_MEAN) ;
  self->thresholdingMethod = method ;
}

/** ------------------------------------------------------------------
 ** @brief Get the thresholding method
 **
 ** @param self KDForest object.
 ** @return thresholding method.
 **
 ** @sa ::vl_kdforest_set_thresholding_method
 **/

VL_INLINE VlKDTreeThresholdingMethod
vl_kdforest_get_thresholding_method (VlKDForest const * self)
{
  return self->thresholdingMethod ;
}

/** ------------------------------------------------------------------
 ** @brief Get the dimension of the data
 ** @param self KDForest object.
 ** @return dimension of the data.
 **/

VL_INLINE vl_size
vl_kdforest_get_data_dimension (VlKDForest const * self)
{
  return self->dimension ;
}

/** ------------------------------------------------------------------
 ** @brief Get the data type
 ** @param self KDForest object.
 ** @return data type (one of ::VL_TYPE_FLOAT, ::VL_TYPE_DOUBLE).
 **/

VL_INLINE vl_type
vl_kdforest_get_data_type (VlKDForest const * self)
{
  return self->dataType ;
}

VL_INLINE VlKDForest *
vl_kdforest_searcher_get_forest (VlKDForestSearcher const * self)
{
  return self->forest;
}


/* VL_KDTREE_H */
#endif
