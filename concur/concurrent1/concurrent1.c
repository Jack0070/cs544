/*
cs544 cConcurrency 1

Group 26 Li Li, Hafed Alghamdi, Sultan AlAnazi

Oct 14 2014

*/
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// two semaphores, one for consumers and on for producers

sem_t sem_c;
sem_t sem_p;

// item struct
struct item
{
	int a;
	int b;
}buff[32];


// producer items, it has two semaphores in it.
// sem_c for number of items that we currently have and sem_p for
// number of space in buff we have. So before producing, we check if
// sem_p have anything space left to produce, if we have than we decrease 
// the space and produce. After producing, we increase the number of items-- sem_c.
void *producers(void* arg)
{
	int pro_no = *((int*)arg);
	
	for(int cnt=0;cnt<pro_no;cnt++)
	{
		fprintf(stderr, "producer is running\n");
		//sem_c++, sem_p--
		int val;
		
		if(sem_getvalue(&sem_p, &val)==-1)
			fprintf(stderr, "semget err\n");
		if (val==0)
			fprintf(stderr, "producer is blocked\n");
		sem_wait(&sem_p);
		// 
		for(int i =0;i<32;i++)
		{
			if(buff[i].b== 0 )
				{
				//ACM code randm()
				
				int rd1;
				//int rd1 = 3+rand()%6;
		__asm__(
					"movl	$0, %eax\n\t"
					"call	rand\n\t"
					"movl	%eax, -12(%rbp)\n\t"

				);
				
				//fprintf(stderr, "%d\n",rd1);
				rd1 = 3+ rd1%6;

				sleep(rd1);
				//
				//int rd2 = rand()%10;
				int rd2;
		__asm__(
					"movl	$0, %eax\n\t"
					"call	rand\n\t"
					"movl	%eax, -8(%rbp)\n\t"

				);
				//fprintf(stderr, "%d\n",rd2);
				rd2 = rd2%10;
				
				buff[i].a = rd2;
				
				//int rd3 = 2+rand()%8;
				int rd3;
		__asm__(
					"movl	$0, %eax\n\t"
					"call	rand\n\t"
					"movl	%eax, -4(%rbp)\n\t"

				);
				//fprintf(stderr, "%d\n",rd3);
				rd3 = 2+rd3%8;
				buff[i].b = rd3;
			
				break;			
				}
		}
		
		sem_post(&sem_c);
	}
	return 0;
}


// Consume items, like producers(), here we use both of the sem_c and sem_p too.
// sem_c for number of items that we currently have and sem_p for
// number of space in buff we have. So before consuming, we check if
// sem_p have anything items left to consume, if we have than we decrease 
// the number of items to consume. After consuming, we increase 
// the number of space that we can produce.
void *consumers(void* arg)
{
	int com_no = *((int*)arg);
	
	for(int cnt=0;cnt<com_no;cnt++)
	{
		fprintf(stderr, "consumers running\n");
	
		//sem_c--, sem_p++
		
		int val;
		
		if(sem_getvalue(&sem_c, &val)==-1)
			fprintf(stderr, "semget err\n");
		if (val==0)
			fprintf(stderr, "consumer is blocked\n");
		sem_wait(&sem_c);
	
		// 
		for(int i =0;i<32;i++)
		{
			if(buff[i].b!= 0)
				{
				fprintf(stderr, "%d\n",buff[i].a);
				sleep(buff[i].b);
				fprintf(stderr, "%d\n",buff[i].b);
				buff[i].a = 0;
				buff[i].b = 0;
				break;			
				}
		}
		sem_post(&sem_p);
	}
	return 0;
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "incorrect number of arguments\n");
		printf("input two arguments:1. No. of producers \n 2. No. of consumers  \n");
		exit(EXIT_FAILURE);
	}
	
	srand(time(NULL));
	
	sem_init(&sem_c, 0, 0);
	sem_init(&sem_p,0,32);
	
	pthread_t thread1, thread2;
	
	int no_p = atoi(argv[1]);
	int no_c = atoi(argv[2]);
	
    pthread_create(&thread1, NULL, producers, (void *)&no_p);
    pthread_create(&thread2, NULL, consumers, (void *)&no_c);
    
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    
    return 0;
}