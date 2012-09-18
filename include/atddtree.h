#ifndef _ATDDTREE_ATDDTREE_H_
#define _ATDDTREE_ATDDTREE_H_

/* 
Arrival Time Distribution Driven Tree

Inspired by "Distribution Driven Binary Tree Balancing with R-Trees".
Basically, it's a binary "R-Tree" denoted by the paper.
*/

#include "common.h"

/**
@name atddtree key macros
@{
*/

typedef __u32 atddtree_key;

#define KEYCMP(k1, k2) ((k1)==(k2)?0:( (k1)>(k2)?1:-1) )
#define KEYCPY(k1, k2) ((k1)=(k2))

#define KEYADD(k1, k2) ((k1)+(k2))
#define KEYMINUS(k1, k2) ((k1)-(k2))
#define KEYDIV(k, d) ((k)/(d))
/**
@}
*/


typedef struct _atddtree_node	{
	atddtree_key* k;
	struct _atddtree_node* lchild;
	struct _atddtree_node* rchild;
}atddtree_node;


typedef struct _atddtree	{
	atddtree_key* kmin;
	atddtree_key* kmax;
	atddtree_node* root;
	int h;	//height
}atddtree;



/**
@name atddtree node operations
@{
*/

atddtree_node* atddtree_node_create(atddtree* t, atddtree_key* k);
/**
@}
*/

/**
@name atddtree tree operations
@{
*/
atddtree* atddtree_create(atddtree_key* kmin, atddtree_key* kmax);
/*
\brief 
\param[in] t tree to find
\param[in] n Where the result pointer stores.When key is found, n point to the pointer of the found key;when not found, n point to the pointer of the key
\param[in] h if found, h is the depth of the node found; if not found h is the depth of the node to be created

\return 1 if found, -1 if not found and in left child, -2 if found and in right 
*/
int atddtree_find(atddtree* t, atddtree_key* k, atddtree_node** n, int* d);
int atddtree_insert(atddtree* t, atddtree_key* k);
/**
@}
*/

#endif

