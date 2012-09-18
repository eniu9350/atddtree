#include "atddtree.h"

atddtree_node* atddtree_node_create(atddtree* t, atddtree_key* k)
{
	atddtree_node* n = MALLOC(1, atddtree_node);
	n->k = k;
	n->lchild = NULL;
	n->rchild = NULL;
	return n;
}


atddtree* atddtree_create(atddtree_key* kmin, atddtree_key* kmax)
{
	atddtree* t;
	t = MALLOC(1, atddtree);
	t->root = NULL;
	t->kmin = kmin;
	t->kmax = kmax;
	t->h = 0;
	return t;
}

/*
min: last level min
max: last level max
id: 
*/
void atddtree_getrange(int level, int id, atddtree_key* min, atddtree_key* max, atddtree_key* rmin, atddtree_key* rmax)
{
	atddtree_key totalrange = KEYMINUS(max, min);
	*rmin = (id-1)*totalrange/2+1;
	*rmax = id*totalrange/2;
}


int atddtree_find(atddtree* t, atddtree_key* k, atddtree_node** n, int* d)	{
	atddtree_node* root = t->root;
	atddtree_node* prev;
	atddtree_node* p;
	
	prev = p = root;
	
	int l;	//level
	//int id;	//id in level
	atddtree_key min;
	atddtree_key max;
	atddtree_key range;
	atddtree_key min1, max1, min2, max2;
	int flag = 0;	// 1: lchild, 2: rchild
	
	KEYCPY(min, *(t->kmin));
	KEYCPY(max, *(t->kmax));
	l = 1;
	while(p!=NULL)	{
		//id = 1;
		if(KEYCMP(*(p->k), *k) == 0)	{
			*n = p;
			*d = l;
			return 1;
		}else	{
			KEYCPY(range,KEYADD(KEYMINUS(max, min),1) );
			//choose 
			KEYCPY( max1, KEYADD(KEYMINUS(min,1), KEYDIV(range, 2)) );
			if(KEYCMP(*k, max1)<=0)	{	//left
				//min not change
				KEYCPY( max, max1);

				prev = p;
				p=p->lchild;
				flag = -1;
			}else	{	//right
				KEYCPY( min, max1+1);
				//max not change				

				prev = p;
				p=p->rchild;
				flag = -2;				
			}

			l++;
		}
	}

	*d = l;
	*n = prev;
	
	return flag;
}



int atddtree_insert(atddtree* t, atddtree_key* k)
{
	//find leaf node that is in charge of the range
	atddtree_node* n = NULL;
	int d = 0;
	int find;
	if(t->root==NULL)	{
		t->root = atddtree_node_create(t, k);
		t->h = 1;
	}else	{
		find = atddtree_find(t, k, &n, &d);
		if(find == 1)	{	//found
			return 0;	//mmm
		}else	{ // not found , decide whether to create left child or right child
			if(find ==-1)	{	//left
				n->lchild = atddtree_node_create(t, k);
			}else if(find==-2)	{	//right
				n->rchild = atddtree_node_create(t, k);
			}

			if(d>t->h)	{
				if(d-t->h!=1)	{
					printf("atddtree_insert: d-t->h!=1.\n");
				}
				t->h = d;
			}
		}
	}
	
	return 0;
}


