#include <stdio.h>
#include <stdlib.h>
#include <gsl_rng.h>
#include <gsl_randist.h>
#include <time.h>


#include "atddtree.h"


int main()
{
	atddtree_key min = 1;
	atddtree_key max;// = 1000;
	atddtree_key keys[10] = {2,3,5,8,16,23,26,35,48,50};
	int i;
	atddtree* t;

	int nuser = 1024*1024-1;	//1024*1024*64-1;
	double mean_ia = 205;

	
	gsl_rng *r;
		const gsl_rng_type *T;
		int n=5;
		double u;
			
		T=gsl_rng_ranlxs0;		//设随机数生成器类型是 ranlxs0
		
		
		//gen arrival
		gsl_rng_default_seed = ((unsigned long)(time(NULL))); 	 //设seed值为当前时间
		r=gsl_rng_alloc(T); 	 //生成实例
		
		double* exp_sample_ir = MALLOC(nuser, double);
		double abstemp = 0;
		for(i=0;i<nuser;i++)	{
				exp_sample_ir[i] = gsl_ran_exponential(r, mean_ia);
				//exp_sample_ir[i] =  2+(i%10000)*0.3;
#ifdef LOGISTIC				
				abstemp = gsl_ran_logistic(r, 1);
				if(abstemp<0)	{
					abstemp=0-abstemp;
				}
				exp_sample_ir[i] = abstemp;				
#endif	
				//exp_sample_ir[i] = 5*gsl_ran_beta(r, 5, 1);
				//exp_sample_ir[i] = 5*gsl_ran_lognormal(r, 5, 0.25);
				//printf("exp: %f\n", exp_sample_ir[i]);
		}
		
		
		double* arrival_real	= MALLOC(nuser, double);
		arrival_real[0] = 1.0;
		for(i=1;i<nuser;i++)	{
			arrival_real[i] = arrival_real[i-1]+exp_sample_ir[i-1];
			//printf("arrival_real: %f\n", arrival_real[i]);
		}

		atddtree_key* arrival	= MALLOC(nuser, atddtree_key);
		for(i=0;i<nuser;i++)	{
					arrival[i] = (atddtree_key)arrival_real[i];
					//printf("arrival: %ld\n", arrival[i]);
		}


		max = 0;
	for(i=0;i<nuser;i++)	{
		
		if(KEYCMP(arrival[i],max)>0)	{
			KEYCPY(max,arrival[i]);
		}
		
	}

	printf("---max=%ld\n", max);









	
	t = atddtree_create(&min, &max);
	for(i=0;i<nuser;i++)	{
		atddtree_insert(t, arrival+i);
		//printf("insert %ld, height=%d\n", arrival[i], t->h);
	}
	printf("height=%d\n", t->h);
}
