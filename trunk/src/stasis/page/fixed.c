#include <assert.h>
#include "../page.h"
#include "fixed.h"
/** @todo should page implementations provide readLSN / writeLSN??? */
#include <stasis/truncation.h>



int fixedRecordsPerPage(size_t size) {
  return (USABLE_SIZE_OF_PAGE - 2*sizeof(short)) / size;
}
/** @todo CORRECTNESS  Locking for fixedPageInitialize? (should hold writelock)*/
void fixedPageInitialize(Page * page, size_t size, int count) {
  assertlocked(page->rwlatch);
  // XXX fixed page asserts it's been given an UNINITIALIZED_PAGE...  Why doesn't that crash?
  assert(*page_type_ptr(page) == UNINITIALIZED_PAGE);
  *page_type_ptr(page) = FIXED_PAGE;
  *recordsize_ptr(page) = size;
  assert(count <= fixedRecordsPerPage(size));
  *recordcount_ptr(page)= count;
}

static int checkRidWarnedAboutUninitializedKludge = 0;
static void checkRid(Page * page, recordid rid) {
  assertlocked(page->rwlatch);
  if(! *page_type_ptr(page)) { 
    if(!checkRidWarnedAboutUninitializedKludge) { 
      checkRidWarnedAboutUninitializedKludge = 1;
      printf("KLUDGE detected in checkRid. Fix it ASAP\n");
      fflush(stdout);
    }
    fixedPageInitialize(page, rid.size, fixedRecordsPerPage(rid.size));
  }

  assert(page->id == rid.page);
  assert(*recordsize_ptr(page) == rid.size);
  assert(fixedRecordsPerPage(rid.size) > rid.slot);
}

//-------------- New API below this line

static const byte* fixedRead(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  checkRid(p, rid);
  assert(rid.slot < *recordcount_ptr(p));
  return fixed_record_ptr(p, rid.slot);
}

static byte* fixedWrite(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  checkRid(p, rid);
  assert(rid.slot < *recordcount_ptr(p));
  return fixed_record_ptr(p, rid.slot);
}

static int fixedGetType(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  //  checkRid(p, rid);
  if(rid.slot < *recordcount_ptr(p)) {
    int type = *recordsize_ptr(p);
    if(type > 0) { 
      type = NORMAL_SLOT;
    }
    return type;
  } else {
    return INVALID_SLOT;
  }
}
static void fixedSetType(int xid, Page *p, recordid rid, int type) {
  assertlocked(p->rwlatch);
  checkRid(p,rid);
  assert(rid.slot < *recordcount_ptr(p));
  assert(physical_slot_length(type) == physical_slot_length(*recordsize_ptr(p)));
  *recordsize_ptr(p) = rid.size;
}
static int fixedGetLength(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  assert(*page_type_ptr(p));
  return rid.slot > *recordcount_ptr(p) ?
      INVALID_SLOT : physical_slot_length(*recordsize_ptr(p));
}
static recordid fixedNext(int xid, Page *p, recordid rid) {
  short n = *recordcount_ptr(p);
  rid.slot++;
  rid.size = *recordsize_ptr(p);
  if(rid.slot >= n) {
    return NULLRID;
  } else {
    return rid;
  }
}
static recordid fixedFirst(int xid, Page *p) {
  recordid rid = { p->id, -1, 0 };
  rid.size = *recordsize_ptr(p);
  return fixedNext(xid, p, rid);
}

static int notSupported(int xid, Page * p) { return 0; }

static int fixedFreespace(int xid, Page * p) {
  assertlocked(p->rwlatch);
  if(fixedRecordsPerPage(*recordsize_ptr(p)) > *recordcount_ptr(p)) {
    // Return the size of a slot; that's the biggest record we can take.
    return physical_slot_length(*recordsize_ptr(p));
  } else {
    // Page full; return zero.
    return 0;
  }
}
static void fixedCompact(Page * p) {
  // no-op
}
static recordid fixedPreAlloc(int xid, Page *p, int size) {
  assertlocked(p->rwlatch);
  if(fixedRecordsPerPage(*recordsize_ptr(p)) > *recordcount_ptr(p)) {
    recordid rid;
    rid.page = p->id;
    rid.slot = *recordcount_ptr(p);
    rid.size = *recordsize_ptr(p);
    return rid;
  } else {
    return NULLRID;
  }
}
static void fixedPostAlloc(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  assert(*recordcount_ptr(p) == rid.slot);
  assert(*recordsize_ptr(p) == rid.size);
  (*recordcount_ptr(p))++;
}
static void fixedFree(int xid, Page *p, recordid rid) {
  assertlocked(p->rwlatch);
  if(*recordsize_ptr(p) == rid.slot+1) {
    (*recordsize_ptr(p))--;
  } else {
    // leak space; there's no way to track it with this page format.
  }
}

// XXX dereferenceRID

void fixedLoaded(Page *p) {
  p->LSN = *lsn_ptr(p);
}
void fixedFlushed(Page *p) {
  *lsn_ptr(p) = p->LSN;
}
page_impl fixedImpl() {
  static page_impl pi = {
    FIXED_PAGE,
    fixedRead,
    fixedWrite,
    0,// readDone
    0,// writeDone
    fixedGetType,
    fixedSetType,
    fixedGetLength,
    fixedFirst,
    fixedNext,
    notSupported, // notSupported,
    pageGenericBlockFirst,
    pageGenericBlockNext,
    pageGenericBlockDone,
    fixedFreespace,
    fixedCompact,
    fixedPreAlloc,
    fixedPostAlloc,
    fixedFree,
    0, // XXX dereference
    fixedLoaded, // loaded
    fixedFlushed, // flushed
  };
  return pi;
}

/**
 @todo arrayListImpl belongs in arrayList.c
*/
page_impl arrayListImpl() {
  page_impl pi = fixedImpl();
  pi.page_type = ARRAY_LIST_PAGE;
  return pi;
}

void fixedPageInit() { } 
void fixedPageDeinit() { }
