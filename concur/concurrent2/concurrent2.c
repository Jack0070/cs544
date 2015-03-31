/*
cs544 Concurrency 2

Group 26 Li Li, Hafed Alghamdi

Oct 18 2014

*/
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

// 2+5 semaphores, two of them are for left and right forks. 
// The other 5 just indicate if the philosopher has eaten sth.

sem_t sem_left;
sem_t sem_right;

sem_t sem_phi[5];

bool phi[5];
char *phi_name[5];

int phi_forks[5];

// philosopher status: 0,1,2.
// 0 means blocking, 1 means thinking, 2 means eating
int phi_status[5];




// philosopher function try to do following steps:
// 1. thinking, when thinking we assign them a thinking status.
// 2. try to entre the geting fork step, we have separate semaphores for each 
// philosophers. They can only eat once in a cycle. 
// 3. get fork, left first and then right
// 4. eating and putfork, release the fork. left first and then right.
void * philosopher(void* arg)
{
	int phi_no = *((int*)arg);
	for(;;)
	{

		//think();
		int rd_think = 1+rand()%20;
		phi_status[phi_no] = 1;
		sleep(rd_think);
		phi_status[phi_no] = 0;
		
		sem_wait(&sem_phi[phi_no]);
		
		//get_fork();
		
		sem_wait(&sem_left);
		phi_forks[phi_no] += 1;
		
		sem_wait(&sem_right);
		phi_forks[phi_no] += 1;
		
		phi[phi_no] = 1;
		if(phi[0]&&phi[1]&&phi[2]&&phi[3]&&phi[4] )
			{
				for(int i=0;i<5;i++)
					{
					sem_post(&sem_phi[i]);
					phi[i] = 0;
					}
			}
			
		//eat();
		int rd_eat = 2+rand()%8;
		phi_status[phi_no] = 2;
		sleep(rd_eat);
		phi_status[phi_no] = 0;	
		

		//put_fork();
		
		sem_post(&sem_left);
		phi_forks[phi_no] -= 1;
		
		sem_post(&sem_right);
		phi_forks[phi_no] -= 1;
		

	 }
	
	return 0;
}

// separate thread printer is doing printing every second to check if program meets the requirements.
void * printer()
{
	while(1)
	{
		sleep(1);	
		fprintf(stdout,"Name:\t ");
		for(int i=0;i<5;i++)
			fprintf(stdout, "%10s\t",phi_name[i]);
		fprintf(stdout,"\n");
		
		fprintf(stdout,"Forks got: ");
		for(int i=0;i<5;i++)
			fprintf(stdout, "%10d\t",phi_forks[i]);
		fprintf(stdout,"\n");
		
		fprintf(stdout,"Name:\t ");
		for(int i=0;i<5;i++)
			switch(phi_status[i]){
				case 0:
				fprintf(stdout, " Blocking \t");
				break;
				
				case 1:
				fprintf(stdout, " Thinking \t");
				break;
				
				case 2:
				fprintf(stdout, "  Eating  \t");
				break;
				
				default:
				fprintf(stdout, "switch has sth wrong\n");
			}
		fprintf(stdout,"\n");

		fprintf(stdout, "----------------------------------------------\n");
	}
		
	return 0;
}


int main(int argc, char** argv)
{

// 	if (argc != 3) {
// 		fprintf(stdout, "incorrect number of arguments\n");
// 		printf("input two arguments:1. No. of producers \n 2. No. of consumers  \n");
// 		exit(EXIT_FAILURE);
// 	}
	
	srand(time(NULL));
	// I init all semaphores here and the flags for all philosophers.
	sem_init(&sem_left, 0, 3);
	sem_init(&sem_right,0,2);
	
	for(int i=0;i<5;i++)
		{
		sem_init(&sem_phi[i],0,1);
		phi[i] = false;
		phi_forks[i] = 0;
		phi_status[i] = 0;
		}
		
	phi_name[0] = "Dijkstra";
	phi_name[1] = "Confucius";
	phi_name[2] = "Li";
	phi_name[3] = "Hafed";
	phi_name[4] = "KingFaysal";
	

		
		
	
	pthread_t threads[5];
	pthread_t thread_print;
	
	int args[5];
	for (int i=0; i<5; i++)
		args[i] = i;

	for (int i=0; i<5; i++)
		pthread_create(&threads[i], NULL, philosopher, (void *)&args[i]);
	
	pthread_create(&thread_print, NULL, printer, NULL);

	void* status;
	for (int i=0; i<5; i++)
		pthread_join(threads[i], &status);
    
    return 0;
}