/*
 * Concurrency project #4 - Barber, customers, waiting chairs and barber chair.
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
int waiting_chair_count;

// mutexes to keep things exclusive
pthread_mutex_t barber_chair_mutex;
pthread_mutex_t hair_cut_mutex;

void * barber()
{
	while(TRUE){

	pthread_mutex_lock(&hair_cut_mutex);
	//hair_cut()
	fprintf(stdout,"barber started cut_hair\n");
	int rd_bar = 1+rand()%2;
	sleep(rd_bar);
	pthread_mutex_unlock(&barber_chair_mutex);
	fprintf(stdout,"barber finished cut_hair\n");
	}
}

void * customer(void* arg)
{
	int cust_no = *((int*)arg);
	
	if (waiting_chair_count>0)
	{
		waiting_chair_count--;
		
		//lock the barber chair
		pthread_mutex_lock(&barber_chair_mutex);
		waiting_chair_count++;
		
		//get_hair_cut();
		pthread_mutex_unlock(&hair_cut_mutex);
		fprintf(stdout,"customer No.%d get_hair_cut\n",cust_no);
	}
	else 
	{
	fprintf(stdout,"customer No.%d had no waiting chair and left.\n",cust_no);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	// seed the random number generator
	srand(time(NULL));

	// initialize the read/write lock and other mutexes
	pthread_mutex_init(&barber_chair_mutex, NULL);
	pthread_mutex_init(&hair_cut_mutex, NULL);
	//want hair_cut to be init as locked, otherwise barber will start to haircut some one. It is only invoked by unlock hair_cut_mutex which rests in customers().
	pthread_mutex_lock(&hair_cut_mutex);

	int num_waiting_chairs;
	int num_customers;
	
	if( argc != 3 ) {
		printf("usage: %s NUM_WAITING_CHAIRS NUM_CUSTOMERS\n(2 integer args)\n%s 3 20 is a good default\n", argv[0],argv[0]);
		exit(0);
	}
	
	num_waiting_chairs = atoi(argv[1]);
	num_customers = atoi(argv[2]);
	
	waiting_chair_count = num_waiting_chairs;

	pthread_t thread_barber;
	pthread_t thread_customers[num_customers];
	
	int args[num_customers];
	for (int i=0; i<num_customers; i++)
		args[i] = i;
	
	pthread_create(&thread_barber, NULL, barber, NULL);
	
	//every second, it generates 0-2 customers;
	int rd_cus = 0;
	for(int i=0;i<num_customers;i=i+rd_cus)
		{
			
			rd_cus= rand()%3;
			sleep(1);
			if (i+rd_cus >= num_customers)
				rd_cus = num_customers-i;
			
			for (int j=1; j<=rd_cus; j++)
				pthread_create(&thread_customers[i+j-1], NULL, customer, (void *)&args[i+j-1]);
		}
	
	void* status;
	for (int i=0; i<num_customers; i++)
		pthread_join(thread_customers[i], &status);
	
	pthread_join(thread_barber, &status);
	
	pthread_mutex_destroy(&barber_chair_mutex);
	pthread_mutex_destroy(&hair_cut_mutex);
	
	return 0;
}

