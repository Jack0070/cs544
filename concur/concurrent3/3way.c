/*
 * Concurrency project #3 - Searching, Inserting, Deleting turkey dinners
 * 
 * CS544 Fall 2014
 * 
 * Group 26 - Li Li, Hafed Alghamdi, David Stuve
 *
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Because this is November, concurrency exercise #3 will be
 *  about Turkey Dinners.  Three types of threads access
 *  a singly-linked list of turkey dinners:  searchers,
 *  inserters, and deleters.  Searchers can search for
 *  a certain dinner, inserters insert a new one at the end
 *  of the list, and deleters cancel and remove a dinner.
 *  Searchers can share access to the list with themselves and inserters,
 *  and are only blocked during a delete.  Only one Insert can
 *  happen at once, and they are also blocked by deletes.
 *  Only one delete can happen at a time.
 *
 *  mutex locks are used by inserters and deleters,
 *  and a read/write lock is used by searchers, which do read locks
 *  and deleters which do a write lock to ensure no search goes
 *  on during a delete.
 *
 *  Sleep time between operations:
 *  	searchers 1 to 2 seconds
 *  	inserters 1 to 3 seconds
 *  	deleters  1 to 4 seconds
 */


// hack becase I like to say while(TRUE) due to historic reasons
#define TRUE 1

// random range generator returns value inclusive of the end points
int random_range(int start, int end)
{
	long int r = random();
	
	// how big is the interval?
	double my_range = (double)(end-start+1);
	
	double my_multiplier = ((double)r)/RAND_MAX;
	
	int my_result = start + (int)(my_range*my_multiplier);
	
	return my_result;
}


// In honor of Thanksgiving, Turkey dinner  nodes for our singly_linked list
struct TurkeyDinner
{
	int number_guests;  // number of turkeys
	struct TurkeyDinner *next_dinner;
};


// list of Turkeys - NULL = empty
struct TurkeyDinner *turkey_dinners = NULL;
int dinner_count = 0;


// mutexes to keep things exclusive
pthread_rwlock_t searchers_lock;	// read/write lock=allows many readers, writing is exclusive
pthread_mutex_t inserters_lock;
pthread_mutex_t deleters_lock;


// will point to arrays of each kind of thread
pthread_t *searchers;
pthread_t *inserters;
pthread_t *deleters;

int *searcher_ids;
int *inserter_ids;
int *deleter_ids;


// helper, find the end of the list
struct TurkeyDinner *find_last_dinner(struct TurkeyDinner *first)
{
	// empty list?  return NULL
	if(first == NULL) return NULL;
	
	struct TurkeyDinner *my_last = first;
	
	// iterate through list until ->next_dinner is NULL
	while(my_last->next_dinner != NULL) {
		my_last = my_last->next_dinner;
	}
	
	// this must be the last dinner node in the list
	return my_last;
}


// given a dinner node, add it to the end of the list
void add_dinner(struct TurkeyDinner *new_dinner)
{
	// do insert at end of list
	struct TurkeyDinner *last_dinner = find_last_dinner(turkey_dinners);
	
	if( last_dinner == NULL ) {
		// handle the empty-list condition
		turkey_dinners = new_dinner;
	}
	else {
		// not empty
		last_dinner->next_dinner = new_dinner;
	}

	// increase the number of dinners
	dinner_count++;
}


// given the index of a dinner to cancel
// remove it from the list
void cancel_dinner(int dinner_index)
{
	// set up prev and current to do proper removal from list
	struct TurkeyDinner *prev_dinner = NULL;
	struct TurkeyDinner *curr_dinner = turkey_dinners;
	int count = 1;
	
	// iterate until we find our target node
	while( count != dinner_index ) {
		prev_dinner = curr_dinner;
		curr_dinner = curr_dinner->next_dinner;
		count++;
	}

	// we found or target node, remove it
	if( prev_dinner == NULL ) {
		// was first node, just change the head pointer
		turkey_dinners = curr_dinner->next_dinner;
	}
	else {
		// wasn't first node, snip from the list
		prev_dinner->next_dinner = curr_dinner->next_dinner;
	}

	// reduce the number of dinners
	dinner_count--;
	
	// free our node
	free(curr_dinner);
}


// find a dinner! return TRUE if found, FALSE if not
int find_dinner(int dinner_index)
{
	struct TurkeyDinner *curr_dinner = turkey_dinners;
	int count = 1;
	
	while( count != dinner_index ) {
		curr_dinner = curr_dinner->next_dinner;
		count++;
	}
	
	// must have found the dinner
	return TRUE;
}


// aka find the nth dinner
void searcher(int *arg)
{
	int my_id = *arg;
	fprintf(stderr, "Searcher %d waking up\n", my_id);
	
	while(TRUE) {
		// get a read lock for searchers
		pthread_rwlock_rdlock(&searchers_lock);
	
		if( dinner_count > 0 ) {
			// which dinner to search for?
			int to_find = random_range(1, dinner_count);
			
			if( find_dinner(to_find) ) {
				fprintf(stderr, "Searcher %d: found dinner %d\n", my_id, to_find); 
			}
			else {
				fprintf(stderr, "Searcher %d: ERROR, can't find dinner %d\n", my_id, to_find);
			}
		}
		// release read lock
		pthread_rwlock_unlock(&searchers_lock);
		sleep(random_range(1,2));
	}
}


// aka add a dinner
void inserter(int *arg)
{
	int my_id = *arg;
	fprintf(stderr, "Inserter %d waking up\n", my_id);

	while( TRUE ) {
		// get our node first since we WILL insert it eventually
		struct TurkeyDinner *my_dinner = malloc(sizeof(struct TurkeyDinner));
		my_dinner->number_guests = 4;	// 4 guests always for now
		my_dinner->next_dinner = NULL;	// inserts are at end of list
		
		// ensure inserters are mutually exclusive
		pthread_mutex_lock(&inserters_lock);

		// add the dinner to the end of the list
		fprintf(stderr,"Trying to add dinner\n");
		add_dinner(my_dinner);
		fprintf(stderr,"Done adding dinner\n");

		fprintf(stderr, "Inserter %d: dinner_count is now %d\n", my_id, dinner_count);

		// release the inserters
		pthread_mutex_unlock(&inserters_lock);
		sleep(random_range(1,3));
	}	
}


// aka remove a dinner every second
void deleter(int *arg)
{
	int my_id = *arg;
	fprintf(stderr, "Deleter %d waking up\n", my_id);

	while( TRUE ) {
		pthread_rwlock_wrlock(&searchers_lock);	// write lock on searchers - exclusive access
		pthread_mutex_lock(&inserters_lock);	// lock out the inserters
                if(dinner_count > 0) {
			pthread_mutex_lock(&deleters_lock);	// lock out the deleters

			// which one are we going to delete?
			int to_delete = random_range(1, dinner_count);

			// and cancel that dinner - handles list maintenance and dinner_count
			cancel_dinner(to_delete);

			fprintf(stderr, "Deleter  %d: dinner %d cancelled, dinner_count now %d\n", my_id, to_delete, dinner_count);

			// reverse the unlocking order
			pthread_mutex_unlock(&deleters_lock);	// unlock deleters
		}
		pthread_mutex_unlock(&inserters_lock);	// unlock inserters
		pthread_rwlock_unlock(&searchers_lock); // release write lock on searchers	
		sleep(random_range(1,4));
	}
}


int main(int argc, char *argv[])
{
	// seed the random number generator
	srandom(time(NULL));

	// initialize the read/write lock and other mutexes
	pthread_rwlock_init(&searchers_lock, NULL);
	pthread_mutex_init(&inserters_lock, NULL);
	pthread_mutex_init(&deleters_lock, NULL);

	int num_searchers;
	int num_inserters;
	int num_deleters;
	
	if( argc != 4 ) {
		printf("usage: %s NUM_SEARCHERS NUM_INSERTERS NUM_DELETERS\n(3 integer args)\n%s 2 2 2 is a good default\n", argv[0],argv[0]);
		exit(0);
	}
	
	num_searchers = atoi(argv[1]);
	num_inserters = atoi(argv[2]);
	num_deleters  = atoi(argv[3]);
	
	printf("%s invoked with Searchers=%d Inserters=%d Deleters=%d\n", argv[0], num_searchers, num_inserters, num_deleters);
	
	printf("Allocating memory for threads...\n");
	// tunable numbers of searchers, inserters, and deleters should be fun
	searchers = malloc(sizeof(pthread_t)*num_searchers);
	inserters = malloc(sizeof(pthread_t)*num_inserters);
	deleters  = malloc(sizeof(pthread_t)*num_deleters );
	
	// allocate some ids to pass into them for the print statements
	searcher_ids = malloc(sizeof(int)*num_searchers);
	inserter_ids = malloc(sizeof(int)*num_inserters);
	deleter_ids  = malloc(sizeof(int)*num_deleters );
	printf("Done allocating memory for threads...\n");
	
	int i;
	int *id_ref;	// temp pointer to ID ints make casting more sane

	// create the inserters
	for(i = 0; i < num_inserters; i++) {
		inserter_ids[i] = i;
		id_ref = &searcher_ids[i];
		
		if(pthread_create(&inserters[i], NULL, inserter, (void *)id_ref)) {
			fprintf(stderr, "Error creating inserter %d\n", i);
			exit(-1);
		}
	}

	// create the searchers
	for(i = 0; i < num_searchers; i++) {
		searcher_ids[i] = i;
		id_ref = &searcher_ids[i];
		
		if(pthread_create(&searchers[i], NULL, searcher, (void *)id_ref)) {
			fprintf(stderr, "Error creating searcher %d\n", i);
			exit(-1);
		}
	}

	// create the deleters
	for(i = 0; i < num_deleters; i++) {
		deleter_ids[i] = i;
		id_ref = &deleter_ids[i];
		
		if(pthread_create(&deleters[i], NULL, deleter, (void *)id_ref)) {
			fprintf(stderr, "Error creating deleter %d\n", i);
			exit(-1);
		}
	}

	/* wait for searcher 1 to finish - which it won't of course - hack hack */
	if(pthread_join(searchers[0], NULL)) {
		fprintf(stderr, "Error joining thread\n");
		exit(EXIT_FAILURE);
	}

	// clean up, although code won't get here
	pthread_rwlock_destroy(&searchers_lock);		
	return 0;
}

