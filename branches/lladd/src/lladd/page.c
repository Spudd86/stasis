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
/************************************************
 * $Id$
 *
 * implementation of pages
 ************************************************/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lladd/bufferManager.h>
#include <lladd/constants.h>
/*#include "linkedlist.h"*/
#include <lladd/page.h>
#include <pbl/pbl.h>

/* TODO:  Combine with buffer size... */
static int nextPage = 0;


/************************************************************************

 STRUCTURE OF A PAGE

 +-------------------------------------------+-----------------------+--+
 | DATA SECTION                   +--------->| RID: (PAGE, 0)        |  |
 |          +-----------------+   |          +-----------------------+  |
 |      +-->| RID: (PAGE, 1)  |   |                                     |
 |      |   +-----------------+   |                                     |
 |      |                         |                                     |
 |      +-----------------+       |        +----------------------------+
 |                        |       |   +--->| RID: (PAGE, n)             |
 |                        |       |   |    +----------------------------+
 |======================================================================|
 |^ FREE SPACE            |       |   |                                 |
 |+-----------------------|-------|---|--------------------+            |
 |                        |       |   |                    |            |
 |          +-------------|-------|---+                    |            |
 |          |             |       |                        |            |
 |      +---|---+-----+---|---+---|---+--------------+-----|------+-----+
 |      | slotn | ... | slot1 | slot0 | num of slots | free space | LSN |
 +------+-------+-----+-------+-------+--------------+------------+-----+

 NOTE:
   - slots are zero indexed.
   - slots are of implemented as (offset, length)

************************************************************************/

static const byte *slotMemAddr(const byte *memAddr, int slotNum) ;

/** @todo:  Why does only one of the get/set First/Second HalfOfWord take an unsigned int? */
static int getFirstHalfOfWord(unsigned int *memAddr);
static int getSecondHalfOfWord(int *memAddr);
static void setFirstHalfOfWord(int *memAddr, int value);
static void setSecondHalfOfWord(int *memAddr, int value);

static int readFreeSpace(byte *memAddr);
static void writeFreeSpace(byte *memAddr, int newOffset);
static int readNumSlots(byte *memAddr);
static void writeNumSlots(byte *memAddr, int numSlots);

static int getSlotOffset(byte *memAddr, int slot) ;
static int getSlotLength(byte *memAddr, int slot) ;
static void setSlotOffset(byte *memAddr, int slot, int offset) ;
static void setSlotLength(byte *memAddr, int slot, int length) ;

static int SLOT_OFFSET_SIZE;
static int SLOT_LENGTH_SIZE;
static int SLOT_SIZE;

static int LSN_SIZE;
static int FREE_SPACE_SIZE;
static int NUMSLOTS_SIZE;

static int START_OF_LSN;
static int START_OF_FREE_SPACE;
static int START_OF_NUMSLOTS;

static int MASK_0000FFFF;
static int MASK_FFFF0000;


int isValidSlot(byte *memAddr, int slot);
void invalidateSlot(byte *memAddr, int slot);
void pageDeRalloc(Page page, recordid rid);

void pageCompact(Page page);

touchedBlob_t *touched;
size_t touchedLen;


/**
 * pageInit() initializes all the important variables needed in
 * all the functions dealing with pages.
 */
void pageInit() {

  nextPage = 0;
	/**
	 * For now, we will assume that slots are 4 bytes long, and that the
	 * first two bytes are the offset, and the second two bytes are the
	 * the length.  There are some functions at the bottom of this file
	 * that may be useful later if we decide to dynamically choose
	 * sizes for offset and length.
	 */

	/**
	 * the largest a slot length can be is the size of the page,
	 * and the greatest offset at which a record could possibly 
	 * start is at the end of the page
	 */
	SLOT_LENGTH_SIZE = SLOT_OFFSET_SIZE = 2; /* in bytes */
	SLOT_SIZE = SLOT_OFFSET_SIZE + SLOT_LENGTH_SIZE;

	LSN_SIZE = sizeof(long);
	FREE_SPACE_SIZE = NUMSLOTS_SIZE = 2;

	/* START_OF_LSN is the offset in the page to the lsn */
	START_OF_LSN = PAGE_SIZE - LSN_SIZE;
	START_OF_FREE_SPACE = START_OF_LSN - FREE_SPACE_SIZE;
	START_OF_NUMSLOTS = START_OF_FREE_SPACE - NUMSLOTS_SIZE;

	MASK_0000FFFF = (1 << (2*BITS_PER_BYTE)) - 1;
	MASK_FFFF0000 = ~MASK_0000FFFF;

	touchedLen = DEFAULT_TOUCHED;
	touched = calloc(touchedLen, sizeof(touchedBlob_t));
	
	/*	if( (blob_0_fd = open(BLOB0_FILE, O_RDWR, 0)) == -1 ) {
	  perror("page.c:opening blob file 0");
	  exit(-1);
	} 
	if( (blob_1_fd = open(BLOB1_FILE, O_RDWR, 0)) == -1 ) {
	  perror("page.c:opening blob file 1");
	  exit(-1);
	  } */

}

static void rehashTouch() {

	int i;
	touchedBlob_t *touched_old = touched;
	touchedLen *= 2;
	touched = calloc(touchedLen, sizeof(touchedBlob_t));
	assert(touched);

	for( i = 0; i < touchedLen/2; i++ ) {
		if( touched_old[i].records ) {
			touched[touched_old[i].xid%touchedLen] = touched_old[i];
		}
	}

	free(touched_old);
}

static int touchBlob(int xid, recordid rid) {

	touchedBlob_t *t = &touched[xid%touchedLen];
	if( t->records ) {
		if( t->xid == xid ) {
			recordid ret = t->records[(rid.page+rid.slot)%t->len];
			if( ret.size ) {
				if( ret.page == rid.page && ret.slot == rid.slot ) {
					return 1;
				} else { /* there's another entry for this space */
					int i;
					recordid *old = t->records;
					t->len *= 2;
					t->records = calloc(t->len, sizeof(recordid));
					for( i = 0; i < t->len/2; i++ ) {
						if( old[i].size ) {
							t->records[ (old[i].page+old[i].slot) % t->len ] = old[i];
						}
					}
					return touchBlob(xid, rid);
				}
			} else { /* space is free, mark it */
				t->records[(rid.page+rid.slot)%t->len] = rid;
				return 0;
			}
		} else { /* this is not our transaction */
			do {
				rehashTouch();
			} while( touchBlob(xid, rid) );
			return 0;
		}
	} else { /* we haven't allocated for this xid */
		t->records = calloc(DEFAULT_TOUCHED, sizeof(recordid));
		t->records[(rid.page+rid.slot)%DEFAULT_TOUCHED] = rid;
		t->len = DEFAULT_TOUCHED;
		t->xid = xid;
		return 0;
	}

	assert(0);
	return 0;
}

static void rmTouch(int xid) {

	touchedBlob_t *t = &touched[xid%touchedLen];
	if( t ) {
		free( t->records );
		t->records = NULL;
		/* touched[xid%touchedLen].xid = -1; TODO: necessary? */
	}
}

void pageCommit(int xid) {
	 rmTouch(xid);
}

void pageAbort(int xid) {
	rmTouch(xid);
}

/*#define getFirstHalfOfWord(memAddr) (((*(int*)memAddr) >> (2*BITS_PER_BYTE)) & MASK_0000FFFF) */
 
static int getFirstHalfOfWord(unsigned int *memAddr) {
  unsigned int word = *memAddr;
  word = (word >> (2*BITS_PER_BYTE)); /* & MASK_0000FFFF; */
  return word;
}


static int getSecondHalfOfWord(int *memAddr) {
	int word = *memAddr;
	word = word & MASK_0000FFFF;
	return word;
}


void setFirstHalfOfWord(int *memAddr, int value){
	int word = *memAddr;
	word = word & MASK_0000FFFF;
	word = word | (value << (2*BITS_PER_BYTE));
	*memAddr = word;
}


void setSecondHalfOfWord(int *memAddr, int value) {
	int word = *memAddr;;
	word = word & MASK_FFFF0000;
	word = word | (value & MASK_0000FFFF);
	*memAddr = word;
}

/**
 * slotMemAddr() calculates the memory address of the given slot.  It does this 
 * by going to the end of the page, then walking backwards, past the LSN field
 * (LSN_SIZE), past the 'free space' and 'num of slots' fields (NUMSLOTS_SIZE),
 * and then past a slotNum slots (slotNum * SLOT_SIZE).
 */
static const byte *slotMemAddr(const byte *memAddr, int slotNum) {
	return (memAddr + PAGE_SIZE) - (LSN_SIZE + FREE_SPACE_SIZE + NUMSLOTS_SIZE + ((slotNum+1) * SLOT_SIZE));
}

/**
 * pageReadLSN() assumes that the page is already loaded in memory.  It takes
 * as a parameter a Page and returns the LSN that is currently written on that
 * page in memory.
 */
long pageReadLSN(Page page) {
	return *(long *)(page.memAddr + START_OF_LSN);
}

/**
 * pageWriteLSN() assumes that the page is already loaded in memory.  It takes
 * as a parameter a Page.  The Page struct contains the new LSN and the page
 * number to which the new LSN must be written to.
 */
void pageWriteLSN(Page page) {
	*(long *)(page.memAddr + START_OF_LSN) = page.LSN;
}

/**
 * freeSpace() assumes that the page is already loaded in memory.  It takes 
 * as a parameter a Page, and returns an estimate of the amount of free space
 * available to a new slot on this page.  (This is the amount of unused space 
 * in the page, minus the size of a new slot entry.)  This is either exact, 
 * or an underestimate.
 */
size_t freespace(Page page) {
	int space = (slotMemAddr(page.memAddr, readNumSlots(page.memAddr)) - (page.memAddr + readFreeSpace(page.memAddr)));
	return (space < 0) ? 0 : space;
}

/**
 * readFreeSpace() assumes that the page is already loaded in memory.  It takes
 * as a parameter the memory address of the loaded page in memory and returns
 * the offset at which the free space section of this page begins.
 */
static int readFreeSpace(byte *memAddr) {
	return getSecondHalfOfWord((int*)(memAddr + START_OF_NUMSLOTS));
}

/**
 * writeFreeSpace() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the loaded page in memory and a new offset
 * in the page that will denote the point at which free space begins.
 */
static void writeFreeSpace(byte *memAddr, int newOffset) {
	setSecondHalfOfWord((int*)(memAddr + START_OF_NUMSLOTS), newOffset);
}

/**
 * readNumSlots() assumes that the page is already loaded in memory.  It takes
 * as a parameter the memory address of the loaded page in memory, and returns
 * the memory address at which the free space section of this page begins.
 */
static int readNumSlots(byte *memAddr) {
	return getFirstHalfOfWord((unsigned int*)(memAddr + START_OF_NUMSLOTS));
}

/**
 * writeNumSlots() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the loaded page in memory and an int
 * to which the value of the numSlots field in the page will be set to.
 */
static void writeNumSlots(byte *memAddr, int numSlots) {
	setFirstHalfOfWord((int*)(unsigned int*)(memAddr + START_OF_NUMSLOTS), numSlots);
}

/**
 * pageRalloc() assumes that the page is already loaded in memory.  It takes
 * as parameters a Page and the size in bytes of the new record.  pageRalloc()
 * returns a recordid representing the newly allocated record.
 *
 * NOTE: might want to pad records to be multiple of words in length, or, simply
 *       make sure all records start word aligned, but not necessarily having 
 *       a length that is a multiple of words.  (Since Tread(), Twrite() ultimately 
 *       call memcpy(), this shouldn't be an issue)
 *
 * NOTE: pageRalloc() assumes that the caller already made sure that sufficient
 * amount of freespace exists in this page.  (REF: freespace())
 */
recordid pageRalloc(Page page, size_t size) {
	int freeSpace = readFreeSpace(page.memAddr);
	int numSlots = readNumSlots(page.memAddr);
	recordid rid;

	rid.page = page.id;
	rid.slot = numSlots;
	rid.size = size;
	/*int i; */

	/* Make sure there's enough free space... */
	/*	assert (freespace(page) >= (int)size); */

	/* Reuse an old (invalid) slot entry */
	/*	for (i = 0; i < numSlots; i++) { 
		if (!isValidSlot(page.memAddr, i)) {
			rid.slot = i;
			break;
		}
	}

	if (rid.slot == numSlots) {*/
		writeNumSlots(page.memAddr, numSlots+1);
		/*	}*/

	setSlotOffset(page.memAddr, rid.slot, freeSpace);
	setSlotLength(page.memAddr, rid.slot, rid.size);  
	writeFreeSpace(page.memAddr, freeSpace + rid.size);

	return rid;
}


/** Only used for recovery, to make sure that consistent RID's are created 
 * on log playback. */
recordid pageSlotRalloc(Page page, recordid rid) {
	int freeSpace = readFreeSpace(page.memAddr);
	int numSlots = readNumSlots(page.memAddr);

	/*	assert(rid.slot >= numSlots); */
	if(rid.slot >= numSlots) {

	  if (freeSpace < rid.size) {
	    pageCompact(page);
	    freeSpace = readFreeSpace(page.memAddr);
	    assert (freeSpace < rid.size);
	  }
	  
	  setSlotOffset(page.memAddr, rid.slot, freeSpace);
	  setSlotLength(page.memAddr, rid.slot, rid.size);  
	  writeFreeSpace(page.memAddr, freeSpace + rid.size);
	} else {
	  /*  assert(rid.size == getSlotLength(page.memAddr, rid.slot)); */ /* Fails.  Why? */
	}
	return rid;
}


int isValidSlot(byte *memAddr, int slot) {
	return getSlotOffset(memAddr, slot) != INVALID_SLOT ? 1 : 0;
}

void invalidateSlot(byte *memAddr, int slot) {
	setSlotOffset(memAddr, slot, INVALID_SLOT);
}


void pageDeRalloc(Page page, recordid rid) {
	invalidateSlot(page.memAddr, rid.slot);
}

/**

 	Move all of the records to the beginning of the page in order to 
	increase the available free space.

	TODO: If we were supporting multithreaded operation, this routine 
	      would need to pin the pages that it works on.
*/
void pageCompact(Page page) {

	int i;
	byte buffer[PAGE_SIZE];
	  /*	char *buffer = (char *)malloc(PAGE_SIZE); */
	int freeSpace = 0;
	int numSlots = readNumSlots(page.memAddr);
	int meta_size = LSN_SIZE + FREE_SPACE_SIZE + NUMSLOTS_SIZE + (SLOT_SIZE*numSlots);
	int slot_length;
	int last_used_slot = 0;
	/* Can't compact in place, slot numbers can come in different orders than 
	   the physical space allocated to them. */
	memcpy(buffer + PAGE_SIZE - meta_size, page.memAddr + PAGE_SIZE - meta_size, meta_size);

	for (i = 0; i < numSlots; i++) {
		if (isValidSlot(page.memAddr, i)) {
			slot_length = getSlotLength(page.memAddr, i);
			memcpy(buffer + freeSpace, page.memAddr + getSlotOffset(page.memAddr, i), slot_length);
			setSlotOffset(buffer, i, freeSpace);
			freeSpace += slot_length;
			last_used_slot = i;
		} 
	}

	if (last_used_slot < numSlots) {
		writeNumSlots(buffer, last_used_slot + 1);
	}

	memcpy(page.memAddr, buffer, PAGE_SIZE);
}

/**
 * getSlotOffset() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the page loaded in memory, and a slot
 * number.  It returns the offset corresponding to that slot.
 */
static int getSlotOffset(byte *memAddr, int slot) {
	return getFirstHalfOfWord((unsigned int*)slotMemAddr(memAddr, slot));
}

/**
 * getSlotLength() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the page loaded in memory, and a slot
 * number.  It returns the length corresponding to that slot.
 */
static int getSlotLength(byte *memAddr, int slot) {
	return getSecondHalfOfWord((int*)(unsigned int*)slotMemAddr(memAddr, slot));
}

/**
 * setSlotOffset() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the page loaded in memory, a slot number,
 * and an offset.  It sets the offset of the given slot to the offset passed in
 * as a parameter.
 */
static void setSlotOffset(byte *memAddr, int slot, int offset) {
	setFirstHalfOfWord((int*)slotMemAddr(memAddr, slot), offset);
}

/**
 * setSlotLength() assumes that the page is already loaded in memory.  It takes
 * as parameters the memory address of the page loaded in memory, a slot number,
 * and a length.  It sets the length of the given slot to the length passed in
 * as a parameter.
 */
static void setSlotLength(byte *memAddr, int slot, int length) {
	setSecondHalfOfWord((int*)(unsigned int*)slotMemAddr(memAddr, slot), length);
}

int isBlobSlot(byte *pageMemAddr, int slot) {
	return BLOB_SLOT == getSlotLength(pageMemAddr, slot);
}

/**
   Blob format:

	If the slot entry's size is BLOB_SLOT, then the slot points to a blob record instead of data.  The format of this record is:

	Int:   Version number (0/1)
	Int:   Archive offset
	Int:   Blob size.

   TODO: BufferManager should pass in file descriptors so that this function doesn't have to open and close the file on each call.

*/
void pageReadRecord(int xid, Page page, recordid rid, byte *buff) {
		byte *recAddress = page.memAddr + getSlotOffset(page.memAddr, rid.slot);

	/*look at record, if slot offset == blob_slot, then its a blob, else its normal.	*/
	if(isBlobSlot(page.memAddr, rid.slot)) {
		int fd = -1;
		int version;
		int offset;
		int length;

		version = *(int *)recAddress;
		offset = *(int *)(recAddress + 4);
		length = *(int *)(recAddress + 8);

		fd = version == 0 ? blobfd0 : blobfd1;

		/*		if( (fd = open(version == 0 ? BLOB0_FILE : BLOB1_FILE, O_RDWR, 0)) == -1 ) {
			printf("page.c:pageReadRecord error and exiting\n");
			exit(-1);
			} */
		lseek(fd, offset, SEEK_SET);
		read(fd, buff, length);
		/*  	close(fd); */
	} else {
		int size = getSecondHalfOfWord((int*)slotMemAddr(page.memAddr, rid.slot));

		memcpy(buff, recAddress,  size);
	}
}

void pageWriteRecord(int xid, Page page, recordid rid, const byte *data) {
  	byte *rec; 
	int version = -1;
	int fd = -1;
	int blobRec[3];

	if (isBlobSlot(page.memAddr, rid.slot)) {
	  /* TODO:  Rusty's wild guess as to what's supposed to happen. Touch blob appears to lookup the blob,
	     and allocate it if its not there.  It returns 0 if it's a new blob, 1 otherwise, so I think we 
	     just ignore its return value...*/

	        if( !touchBlob(xid, rid) ) { 
		  /* record hasn't been touched yet */
		  rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);
		  version = *(int *)rec;	
		  blobRec[0] = version == 0 ? 1 : 0;
		  blobRec[1] = *(int *)(rec + 4);
		  blobRec[2] = *(int *)(rec + 8); 
		  memcpy(rec, blobRec,  BLOB_REC_SIZE);

		} else {
		  rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);
		}
	
		fd = version == 0 ? blobfd0 : blobfd1;

		if(-1 == lseek(fd, *(int *)(rec +4), SEEK_SET)) {
		  perror("lseek");
		}
		if(-1 == write(fd, data, *(int *)(rec +8))) {
		  perror("write");
		}

		/* Flush kernel buffers to hard drive. TODO: the
		   (standard) fdatasync() call only flushes the data
		   instead of the data + metadata.  Need to have
		   makefile figure out if it's available, and do some
		   macro magic in order to use it, if possible...

		   This is no longer called here, since it is called at commit.
		*/
		/*		fsync(fd);  */
	} else { /* write a record that is not a blob */
		rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);

		if(memcpy(rec, data,  rid.size) == NULL ) {
			printf("ERROR: MEM_WRITE_ERROR on %s line %d", __FILE__, __LINE__);
			exit(MEM_WRITE_ERROR);
		}
	}
}


/* Currently not called any where, or tested. */
byte * pageMMapRecord(int xid, Page page, recordid rid) {
  	byte *rec; 
	int version = -1;
	int fd = -1;
	int blobRec[3];
	byte * ret;
	if (isBlobSlot(page.memAddr, rid.slot)) {
	  /* TODO:  Rusty's wild guess as to what's supposed to happen. Touch blob appears to lookup the blob,
	     and allocate it if its not there.  It returns 0 if it's a new blob, 1 otherwise, so I think we 
	     just ignore its return value...*/

	        if( !touchBlob(xid, rid) ) { 
		  /* record hasn't been touched yet */
		  rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);
		  version = *(int *)rec;	
		  blobRec[0] = version == 0 ? 1 : 0;
		  blobRec[1] = *(int *)(rec + 4);
		  blobRec[2] = *(int *)(rec + 8); 
		  memcpy(rec, blobRec,  BLOB_REC_SIZE);

		} else {
		  rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);
		}
	
		fd = version == 0 ? blobfd0 : blobfd1;

		if((ret = mmap((byte*) 0, *(int *)(rec +8), (PROT_READ | PROT_WRITE), MAP_SHARED, fd, *(int *)(rec +4))) == (byte*)-1) {
		  perror("pageMMapRecord");
		}

		/*		if(-1 == lseek(fd, *(int *)(rec +4), SEEK_SET)) {
		  perror("lseek");
		}
		if(-1 == write(fd, data, *(int *)(rec +8))) {
		  perror("write");
		  } */

		/* Flush kernel buffers to hard drive. TODO: the
		   (standard) fdatasync() call only flushes the data
		   instead of the data + metadata.  Need to have
		   makefile figure out if it's available, and do some
		   macro magic in order to use it, if possible...*/
		/*		fsync(fd);  */

	} else { /* write a record that is not a blob */
		rec = page.memAddr + getSlotOffset(page.memAddr, rid.slot);

		ret = rec;
		/*		if(memcpy(rec, data,  rid.size) == NULL ) {
			printf("ERROR: MEM_WRITE_ERROR on %s line %d", __FILE__, __LINE__);
			exit(MEM_WRITE_ERROR);
			}*/
	}
	return ret;
}

void pageRealloc(Page *p, int id) {
	p->id = id;
	p->LSN = 0;
	p->dirty = 0;
}

Page pool[MAX_BUFFER_SIZE];

/** 
	Allocate a new page. 
        @param id The id of the new page.
	@return A pointer to the new page.  This memory is part of a pool, 
	        and should not be freed by the application.
 */
Page *pageAlloc(int id) {
  Page *p = &(pool[nextPage]);
  nextPage++;
  assert(nextPage <= MAX_BUFFER_SIZE);
  /*
    Page *p = (Page*)malloc(sizeof(Page));*/ /* freed in bufDeinit */
  /*	assert(p); */
	pageRealloc(p, id);
	return p;
}

void printPage(byte *memAddr) {
	int i = 0;
	for (i = 0; i < PAGE_SIZE; i++) {
		if((*(char *)(memAddr+i)) == 0) {
			printf("#");
		}else {
			printf("%c", *(char *)(memAddr+i));
		}
		if((i+1)%4 == 0)
			printf(" ");
	}
}

#define num 20
int pageTest() {

	Page page;

	recordid rid[num];
	char *str[num] = {"one",
		"two",
		"three",
		"four",
		"five",
		"six",
		"seven",
		"eight",
		"nine",
		"ten",
		"eleven",
		"twelve",
		"thirteen",
		"fourteen",
		"fifteen",
		"sixteen",
		"seventeen",
		"eighteen",
		"nineteen",
		"twenty"};
		int i;

		page.memAddr = (byte *)malloc(PAGE_SIZE);
		memset(page.memAddr, 0, PAGE_SIZE);
		for (i = 0; i < num; i++) {
			rid[i] = pageRalloc(page, strlen(str[i]) + 1);
			pageWriteRecord(0, page, rid[i], (byte*)str[i]);    
		}
		printPage(page.memAddr);

		for (i = 0; i < num; i+= 2)
			pageDeRalloc(page, rid[i]);

		pageCompact(page);
		printf("\n\n\n");
		printPage(page.memAddr);
		return 0;
}

/**
 * 
 */
recordid pageBalloc(Page page, int size, int fileOffset) {
	int freeSpace = readFreeSpace(page.memAddr);
	int numSlots = readNumSlots(page.memAddr);
	recordid rid;

	int i;

	rid.page = page.id;
	rid.slot = numSlots;
	rid.size = size;

	if (freespace(page) < BLOB_REC_SIZE) {
		printf("Error in pageRalloc()\n");
		exit(-1);
	}

	for (i = 0; i < numSlots; i++) {
		if (!isValidSlot(page.memAddr, i)) {
			rid.slot = i;
			break;
		}
	}

	if (rid.slot == numSlots) {
		writeNumSlots(page.memAddr, numSlots+1);
	}

	setSlotOffset(page.memAddr, rid.slot, freeSpace);
	setSlotLength(page.memAddr, rid.slot, BLOB_SLOT);  
	writeFreeSpace(page.memAddr, freeSpace + BLOB_REC_SIZE);


	*(int *)(page.memAddr + freeSpace) = 0;
	*(int *)(page.memAddr + freeSpace + 4) = fileOffset;
	*(int *)(page.memAddr + freeSpace + 8) = size;

	return rid;
}

int getBlobOffset(int page, int slot) {
printf("Error: not yet implemented!!\n");
exit(-1);
}

int getBlobSize(int page, int slot) {
printf("Error: not yet implemented!!\n");
exit(-1);
}
