#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <pthread.h>

// if we're using linux's crazy version of the pthread header, 
// it probably forgot to include PTHREAD_STACK_MIN 

#ifndef PTHREAD_STACK_MIN
#include <limits.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <db.h>

#include "genericBerkeleyDBCode.c"

#define MAX_SECONDS 100
#define COUNTER_RESOLUTION 240

int buckets[COUNTER_RESOLUTION];

int activeThreads = 0;
int max_active = 0;

pthread_cond_t never;
pthread_mutex_t mutex;

int num_xact;
int insert_per_xact;
void * runThread(void * arg);
int
main(int argc, char *argv[])
{
	extern int optind;

	int ch, ret;

	assert(argc == 3);
	/* threads have static thread sizes.  Ughh. */
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&never, NULL);
  
	pthread_attr_setstacksize (&attr, 4 * PTHREAD_STACK_MIN);
	
	pthread_mutex_lock(&mutex);

	
	int l;
	
	for(l = 0; l < COUNTER_RESOLUTION; l++) {
	  buckets[l] = 0;
	}
	
	int r;
	int num_threads         = atoi(argv[1]);

	num_xact = 1;
	insert_per_xact = atoi(argv[2]);

	pthread_t * threads = malloc(num_threads * sizeof(pthread_t));
	int i ;
	for(i = 0; i < num_threads; i++) {

	  if ((ret = pthread_create(&(threads[i]), &attr, runThread, (void *)i)) != 0){
	    fprintf(stderr,
		    "txnapp: failed spawning worker thread: %s\n",
		    strerror(ret));
	    exit (1);
	  }

	}     
	
	pthread_mutex_unlock(&mutex);

	for(i = 0; i < num_threads; i++) {
	  pthread_join(threads[i], NULL);
	}

	free(threads);

	int k;
	double log_multiplier = (COUNTER_RESOLUTION / log(MAX_SECONDS * 1000000000.0));
	for(k = 0; k < COUNTER_RESOLUTION; k++) {
	  printf("%3.4f\t%d\n", exp(((double)k)/log_multiplier)/1000000000.0, buckets[k]);
	}

	return (0);
}


void * runThread(void * arg) {
  int offset = (int) arg;
  
  pthread_mutex_lock(&mutex);
  activeThreads++;
  if(activeThreads > max_active) {
    max_active = activeThreads;
  }
  pthread_mutex_unlock(&mutex);


  int r;

  double sum_x_squared = 0;
  double sum = 0;

  double log_multiplier = COUNTER_RESOLUTION / log(MAX_SECONDS * 1000000000.0);

  struct timeval timeout_tv;
  struct timespec timeout;

  gettimeofday(&timeout_tv, NULL);

  timeout.tv_sec = timeout_tv.tv_sec;
  timeout.tv_nsec = 1000 * timeout_tv.tv_usec;

  timeout.tv_nsec = (int)(1000000000.0 * ((double)random() / (double)RAND_MAX));

  timeout.tv_sec++;

  //  struct timeval start;

  pthread_mutex_lock(&mutex);
  pthread_cond_timedwait(&never, &mutex, &timeout);
  pthread_mutex_unlock(&mutex);
  

  for(r = 0; r < num_xact; r ++) {

    struct timeval endtime_tv;
    struct timespec endtime;

    run_xact(dbenv, db_cats, offset*(1+r)*insert_per_xact, insert_per_xact);

   gettimeofday(&endtime_tv, NULL);

    endtime.tv_sec = endtime_tv.tv_sec;
    endtime.tv_nsec = 1000 * endtime_tv.tv_usec;

    double microSecondsPassed = 1000000000.0 * (double)(endtime.tv_sec - timeout.tv_sec);


    microSecondsPassed = (microSecondsPassed + (double)endtime.tv_nsec) - (double)timeout.tv_nsec;

    assert(microSecondsPassed > 0.0);


    sum += microSecondsPassed;
    sum_x_squared += (microSecondsPassed * microSecondsPassed) ;

    int bucket = (log_multiplier * log(microSecondsPassed));
    
    if(bucket >= COUNTER_RESOLUTION) { bucket = COUNTER_RESOLUTION - 1; }
    
    timeout.tv_sec++;
    pthread_mutex_lock(&mutex);
    buckets[bucket]++;
    pthread_cond_timedwait(&never, &mutex, &timeout);
    pthread_mutex_unlock(&mutex);

  }

  pthread_mutex_lock(&mutex);
  activeThreads--;
  pthread_mutex_unlock(&mutex);


  //  printf("%d done\n", offset);
}

