/*
 * Concurrency project #5 - Problem 1
 * 
 * CS544 Fall 2014
 * 
 * Group 26 - David Stuve, Hafed Alghamdi, Li Li
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/*
 * Every second, main generate 0-2 customers. There is one 
 * barber keep serving the customers or sleep.
 */


// hack becase I like to say while(TRUE) due to historic reasons
#define TRUE 1

//shared variable
int resource_count;

// mutexes to keep things exclusive
pthread_mutex_t resource_mutex;


void * handler()
{
	while(TRUE){

	if (resource_count == 3)
		pthread_mutex_unlock(&resource_mutex);
	}
}

void * customer(void* arg)
{
	int cust_no = *((int*)arg);
	
	while(TRUE){
	
	if (resource_count>0)
	{	
		//working
		resource_count--;
		
		
		fprintf(stdout,"customer No.%d using resource \n",cust_no);
		sleep(rand()%3);
		resource_count++;
		return 0;
		
	}
	else 
	{
		fprintf(stdout,"customer No.%d blocked because lack of resource.\n",cust_no);
		pthread_mutex_lock(&resource_mutex);
	
	}
	}
	
	
}

int main(int argc, char *argv[])
{
	// seed the random number generator
	srand(time(NULL));

	// initialize the read/write lock and other mutexes
	pthread_mutex_init(&resource_mutex, NULL);
	


	// int num_waiting_chairs;
	int num_customers;
	
	if( argc != 2 ) {
		printf("usage: %s  NUM_CUSTOMERS\n(1 integer arg)\n%s  30 is a good default\n", argv[0],argv[0]);
		exit(0);
	}
	
	// num_waiting_chairs = atoi(argv[1]);
	num_customers = atoi(argv[1]);
	
	resource_count = 3;

	pthread_t thread_handler;
	pthread_t thread_customers[num_customers];
	
	int args[num_customers];
	for (int i=0; i<num_customers; i++)
		args[i] = i;
	
	pthread_create(&thread_handler, NULL, handler, NULL);
	
	//every second, it generates 0-4 customers;
	int rd_cus = 0;
	for(int i=0;i<num_customers;i=i+rd_cus)
		{
			
			rd_cus= rand()%5;
			sleep(1);
			if (i+rd_cus >= num_customers)
				rd_cus = num_customers-i;
			
			for (int j=1; j<=rd_cus; j++)
				pthread_create(&thread_customers[i+j-1], NULL, customer, (void *)&args[i+j-1]);
		}
	
	void* status;
	for (int i=0; i<num_customers; i++)
		pthread_join(thread_customers[i], &status);
	
	pthread_join(thread_handler, &status);
	

	pthread_mutex_destroy(&resource_mutex);
	
	return 0;
}

