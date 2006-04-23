
#include <lladd/crc32.h>

#ifndef __HASH_H
#define __HASH_H
unsigned int max_bucket(unsigned char tableBits, unsigned long nextExtension);
unsigned int hash(const void * val, long val_length, unsigned char tableBits, unsigned long nextExtension);
#define twoToThe(x) (1 << (x))

#endif /*__HASH_H */