/*---
This software is copyrighted by the Regents of the University of
California, and other parties. The following terms apply to all files
associated with the software unless explicitly disclaimed in
individual files.

The authors hereby grant permission to use, copy, modify, distribute,
and license this software and its documentation for any purpose,
provided that existing copyright notices are retained in all copies
and that this notice is included verbatim in any distributions. No
written agreement, license, or royalty fee is required for any of the
authorized uses. Modifications to this software may be copyrighted by
their authors and need not follow the licensing terms described here,
provided that the new terms are clearly indicated on the first page of
each file where they apply.

IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY PARTY
FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION, OR ANY
DERIVATIVES THEREOF, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
NON-INFRINGEMENT. THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, AND
THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

GOVERNMENT USE: If you are acquiring this software on behalf of the
U.S. government, the Government shall have only "Restricted Rights" in
the software and related documentation as defined in the Federal
Acquisition Regulations (FARs) in Clause 52.227.19 (c) (2). If you are
acquiring the software on behalf of the Department of Defense, the
software shall be classified as "Commercial Computer Software" and the
Government shall have only "Restricted Rights" as defined in Clause
252.227-7013 (c) (1) of DFARs. Notwithstanding the foregoing, the
authors grant the U.S. Government and others acting in its behalf
permission to use and distribute the software in accordance with the
terms specified in this license.
---*/

#define _GNU_SOURCE
#include "../check_includes.h"

#include <stasis/util/lhtable.h>
#include <stasis/util/random.h>

#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*
#include <sys/time.h>
#include <time.h>
*/
#define LOG_NAME   "check_lhtable.log"

/**
   @test
*/

#define NUM_ENTRIES 10000

START_TEST(lhtableTest)
{
  /*  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_sec + tv.tv_usec);
  */

  char** keys = malloc(NUM_ENTRIES * sizeof(char*));
  struct LH_ENTRY(table) * t = LH_ENTRY(create)(100);
  for(long i = 0; i < NUM_ENTRIES; i++) {
    int keyLen = asprintf(&(keys[i]), "--> %ld <--\n", i);
    assert(keyLen == strlen(keys[i]));
    assert(!LH_ENTRY(find)(t, keys[i], strlen(keys[i])));
    LH_ENTRY(insert)(t, keys[i], strlen(keys[i]), (void*)i);
    assert((void*)i == LH_ENTRY(find)(t, keys[i], strlen(keys[i])));

  }

  for(long i = 0; i < NUM_ENTRIES; i+=2) {
    char * key;
    if(-1 == asprintf(&key, "--> %ld <--\n", i))
      key = 0;

    assert(i == (long)LH_ENTRY(find)(t, key, strlen(key)));
    LH_ENTRY(remove)(t, keys[i], strlen(keys[i]));
    assert(!LH_ENTRY(find)(t, keys[i], strlen(keys[i])));
    free(key);
  }
  LH_ENTRY(destroy)(t);

  for(int i = 0; i < NUM_ENTRIES; i++) {
    free(keys[i]);
  }
  free(keys);


} END_TEST

#define MAXSETS   1000
#define MAXSETLEN 10000
#ifdef LONG_TEST
#define NUM_ITERS 10
#else
#define NUM_ITERS 1
#endif
char * itoa(int i) {
  char * ret;
  if(-1 == asprintf(&ret, "%d", i)) ret = 0;
  return ret;
}

START_TEST(lhtableRandomized) {
 for(int jjj = 0; jjj < NUM_ITERS; jjj++) {
  time_t seed = time(0);

#ifdef LONG_TEST
  if(jjj) {
    printf("\nSeed = %ld\n", seed);
    srandom(seed);
  } else {
    printf("\nSeed = %d\n", 1150241705);
    srandom(1150241705);  // This seed gets the random number generator to hit RAND_MAX, which makes a good test for myrandom()
  }
#else
  printf("\nSeed = %ld\n", seed);
  srandom(seed);
#endif

  struct LH_ENTRY(table) * t = LH_ENTRY(create)(stasis_util_random64(10000));
  int numSets = stasis_util_random64(MAXSETS);
  int* setLength = malloc(numSets * sizeof(int));
  long** sets = malloc(numSets * sizeof(long*));
  int64_t nextVal = 1;
  int64_t eventCount = 0;

  int* setNextAlloc = calloc(numSets, sizeof(int));
  int* setNextDel   = calloc(numSets, sizeof(int));
  int* setNextRead  = calloc(numSets, sizeof(int));

  for(int i =0; i < numSets; i++) {
    setLength[i] = stasis_util_random64(MAXSETLEN);
    sets[i] = malloc(setLength[i] * sizeof(long));
    eventCount += setLength[i];
    for(int j =0; j < setLength[i]; j++) {
      sets[i][j] = nextVal;
      nextVal++;
    }
  }

  eventCount = stasis_util_random64(eventCount * 4);
  printf("Running %lld events.\n", (long long) eventCount);

  for(int iii = 0; iii < eventCount; iii++) {
    int eventType = stasis_util_random64(3);  // 0 = insert; 1 = read; 2 = delete.
    int set = stasis_util_random64(numSets);
    switch(eventType) {
    case 0: // insert
      if(setNextAlloc[set] != setLength[set]) {
	long keyInt = sets[set][setNextAlloc[set]];
	char * key = itoa(keyInt);
	assert(!LH_ENTRY(find)(t, key, strlen(key)+1));
	LH_ENTRY(insert)(t, key, strlen(key)+1, (void*)keyInt);
	//	printf("i %d\n", keyInt);
	assert(LH_ENTRY(find)(t, key, strlen(key)+1));
	free(key);
	setNextAlloc[set]++;
      }
      break;
    case 1: // read
      if(setNextAlloc[set] != setNextDel[set]) {
	setNextRead[set]++;
	if(setNextRead[set] < setNextDel[set]) {
	  setNextRead[set] = setNextDel[set];
	} else if(setNextRead[set] == setNextAlloc[set]) {
	  setNextRead[set] = setNextDel[set];
	}
	assert(setNextRead[set] < setNextAlloc[set] && setNextRead[set] >= setNextDel[set]);
	long keyInt = sets[set][setNextRead[set]];
	char * key = itoa(keyInt);
	long fret = (long)LH_ENTRY(find)(t, key, strlen(key)+1);
	assert(keyInt == fret);
	assert(keyInt == (long)LH_ENTRY(insert)(t, key, strlen(key)+1, (void*)(keyInt+1)));
	assert(keyInt+1 == (long)LH_ENTRY(insert)(t, key, strlen(key)+1, (void*)keyInt));
	free(key);
      }
      break;
    case 2: // delete
      if(setNextAlloc[set] != setNextDel[set]) {
	long keyInt = sets[set][setNextDel[set]];
	char * key = itoa(keyInt);
	assert(keyInt == (long)LH_ENTRY(find)  (t, key, strlen(key)+1));
	assert(keyInt == (long)LH_ENTRY(remove)(t, key, strlen(key)+1));
	assert(0 == (long)LH_ENTRY(find)(t, key, strlen(key)+1));
	//	printf("d %d\n", keyInt);
	free(key);
	setNextDel[set]++;
      }
      break;
    default:
      abort();
    }
  }

  for(int i = 0; i < numSets; i++) {
    free(sets[i]);
  }
  free(setNextAlloc);
  free(setNextDel);
  free(setNextRead);
  free(setLength);
  LH_ENTRY(destroy)(t);
 }
} END_TEST

Suite * check_suite(void) {
  Suite *s = suite_create("lhtable");
  /* Begin a new test */
  TCase *tc = tcase_create("lhtable");

  tcase_set_timeout(tc, 0); // disable timeouts


  /* Sub tests are added, one per line, here */

  tcase_add_test(tc, lhtableTest);
  tcase_add_test(tc, lhtableRandomized);

  /* --------------------------------------------- */

  tcase_add_checked_fixture(tc, setup, teardown);


  suite_add_tcase(s, tc);
  return s;
}

#include "../check_setup.h"
