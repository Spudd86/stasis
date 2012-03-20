/*
 * bloomFilter.h
 *
 * Copyright 2010-2012 Yahoo! Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *      Author: sears
 */
#ifndef STASIS_BLOOM_FILTER_H
#define STASIS_BLOOM_FILTER_H

#include <stasis/common.h>

BEGIN_C_DECLS

typedef struct stasis_bloom_filter_t stasis_bloom_filter_t;

/**
   @return 0 if there is not enough memory, or some other error occurred; a
             pointer to the new bloom filter otherwise.
 */
stasis_bloom_filter_t * stasis_bloom_filter_create(uint64_t(*hash_func_a)(const char*,int),
                                     uint64_t(*hash_func_b)(const char*,int),
                                     uint64_t num_expected_items,
                                     double false_positive_rate);

void stasis_bloom_filter_destroy(stasis_bloom_filter_t*);

void stasis_bloom_filter_insert(stasis_bloom_filter_t * bf, const char* key, int len);
/**
   @return 1 if the value might be in the bloom filter, 0 otherwise
 */
int stasis_bloom_filter_lookup(stasis_bloom_filter_t * bf, const char* key, int len);

void stasis_bloom_filter_print_stats(stasis_bloom_filter_t * bf);

END_C_DECLS

#endif
