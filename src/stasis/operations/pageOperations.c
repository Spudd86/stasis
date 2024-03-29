#include <stasis/page.h>
#include <stasis/page/fixed.h>
#include <stasis/bufferManager.h>
#include <stasis/logger/logEntry.h>
#include <stasis/operations/pageOperations.h>
#include <stasis/operations/regions.h>
#include <assert.h>

static int op_page_set_range(const LogEntry* e, Page* p) {
  assert(e->update.arg_size >= sizeof(int));
  assert(!((e->update.arg_size - sizeof(int)) % 2));

  int off = *(const int*)stasis_log_entry_update_args_cptr(e);
  int len = (e->update.arg_size - sizeof(int)) >> 1;

  assert(off+len <=PAGE_SIZE);

  memcpy(p->memAddr + off, ((const byte*)stasis_log_entry_update_args_cptr(e))+sizeof(int), len);
  return 0;
}
static int op_page_set_range_inverse(const LogEntry* e, Page* p) {
  assert(e->update.arg_size >= sizeof(int));
  assert(!((e->update.arg_size - sizeof(int)) % 2));

  int off = *(const int*)stasis_log_entry_update_args_cptr(e);
  int len = (e->update.arg_size - sizeof(int)) >> 1;

  assert(off+len <=PAGE_SIZE);

  memcpy(p->memAddr + off, ((const byte*)stasis_log_entry_update_args_cptr(e))+sizeof(int)+len,
	 len);
  return 0;
}

int TpageGet(int xid, pageid_t page, void *memAddr) {
  Page * q = loadPage(xid, page);
  memcpy(memAddr, q->memAddr, PAGE_SIZE);
  releasePage(q);
  return 0;
}

int TpageSet(int xid, pageid_t page, const void * memAddr) {
  return TpageSetRange(xid, page, 0, memAddr, PAGE_SIZE);
}

int TpageSetRange(int xid, pageid_t page, int offset, const void * memAddr, int len) {
  // XXX need to pack offset into front of log entry

  Page * p = loadPage(xid, page);
  byte * logArg = malloc(sizeof(int) + 2 * len);

  *(int*)logArg = offset;
  memcpy(logArg+sizeof(int),     ((const byte*)memAddr), len);
  memcpy(logArg+sizeof(int)+len, p->memAddr+offset,         len);

  releasePage(p);

  Tupdate(xid,page,logArg,sizeof(int)+len*2,OPERATION_PAGE_SET_RANGE);

  free(logArg);
  return 0;
}

/** @todo region sizes should be dynamic. */
#define TALLOC_PAGE_REGION_SIZE 128 // 512K

/**
    This calls loadPage and releasePage directly, and bypasses the
    logger.
*/
void pageOperationsInit(stasis_log_t *log) {

  regionsInit(log);

  boundary_tag t;
  recordid rid = {0, 0, sizeof(boundary_tag)};
  // Need to find a region with some free pages in it.
  Tread(-1, rid, &t);

}

int TpageDealloc(int xid, pageid_t page) {
  TregionDealloc(xid, page); // @todo inefficient hack!
  return 0;
}

pageid_t TpageAlloc(int xid) {
  return TregionAlloc(xid, 1, STORAGE_MANAGER_NAIVE_PAGE_ALLOC);
}

/**
    @return a pageid_t.  The page field contains the page that was
    allocated, the slot field contains the number of slots on the
    apge, and the size field contains the size of each slot.
*/
pageid_t TfixedPageAlloc(int xid, int size) {
  pageid_t page = TpageAlloc(xid);
  TinitializeFixedPage(xid, page, size);
  return page;
}

pageid_t TpageAllocMany(int xid, int count) {
  return TregionAlloc(xid, count, STORAGE_MANAGER_NAIVE_PAGE_ALLOC);
}
void TpageDeallocMany(int xid, pageid_t pid) {
  TregionDealloc(xid, pid);
}
int TpageGetType(int xid, pageid_t page) {
  Page * p = loadPage(xid, page);
  int ret = p->pageType;
  releasePage(p);
  return ret;
}

/** Safely allocating and freeing pages is suprisingly complex.  Here is a summary of the process:

   Alloc:

     obtain mutex
        choose a free page using in-memory data
	load page to be used, and update in-memory data.  (obtains lock on loaded page)
	T update() the page, zeroing it, and saving the old successor in the log.
	relase the page (avoid deadlock in next step)
	T update() LLADD's header page (the first in the store file) with a new copy of
	                              the in-memory data, saving old version in the log.
     release mutex

   Free:

     obtain mutex
        determine the current head of the freelist using in-memory data
        T update() the page, initializing it to be a freepage, and physically logging the old version
	release the page
	T update() LLADD's header page with a new copy of the in-memory data, saving old version in the log
     release mutex

*/

/** frees a page by zeroing it, setting its type to LLADD_FREE_PAGE,
    and setting the successor pointer. This operation physically logs
    a whole page, which makes it expensive.  Doing so is necessary in
    general, but it is possible that application specific logic could
    avoid the physical logging here.

    Instead, we should just record the fact that the page was freed
    somewhere.  That way, we don't need to read the page in, or write
    out information about it.  If we lock the page against
    reallocation until the current transaction commits, then we're
    fine.

*/

stasis_operation_impl stasis_op_impl_page_set_range() {
  stasis_operation_impl o = {
    OPERATION_PAGE_SET_RANGE,
    UNKNOWN_TYPE_PAGE,
    OPERATION_PAGE_SET_RANGE,
    OPERATION_PAGE_SET_RANGE_INVERSE,
    op_page_set_range
  };
  return o;
}

stasis_operation_impl stasis_op_impl_page_set_range_inverse() {
  stasis_operation_impl o = {
    OPERATION_PAGE_SET_RANGE_INVERSE,
    UNKNOWN_TYPE_PAGE,
    OPERATION_PAGE_SET_RANGE_INVERSE,
    OPERATION_PAGE_SET_RANGE,
    &op_page_set_range_inverse
  };
  return o;
}

typedef struct {
  slotid_t slot;
  int64_t type;
} page_init_arg;


void TinitializeSlottedPage(int xid, pageid_t page) {
  page_init_arg a = { SLOTTED_PAGE, 0 };
  Tupdate(xid, page, &a, sizeof(a), OPERATION_INITIALIZE_PAGE);
}
void TinitializeFixedPage(int xid, pageid_t page, int slotLength) {
  page_init_arg a = { FIXED_PAGE, slotLength };
  Tupdate(xid, page, &a, sizeof(a), OPERATION_INITIALIZE_PAGE);
}

static int op_initialize_page(const LogEntry* e, Page* p) {
  assert(e->update.arg_size == sizeof(page_init_arg));
  const page_init_arg* arg = stasis_log_entry_update_args_cptr(e);

  switch(arg->slot) {
  case SLOTTED_PAGE:
    stasis_page_slotted_initialize_page(p);
    break;
  case FIXED_PAGE:
    stasis_page_fixed_initialize_page(p, arg->type,
                                 stasis_page_fixed_records_per_page
                                 (stasis_record_type_to_size(arg->type)));
    break;
  default:
    abort();
  }
  return 0;
}

typedef struct {
  pageid_t firstPage;
  pageid_t numPages;
  pageid_t recordSize;      // *has* to be smaller than a page; passed into TinitializeFixedPage()
} init_multipage_arg;

static int op_init_multipage_impl(const LogEntry *e, Page *ignored) {
  const init_multipage_arg* arg = stasis_log_entry_update_args_cptr(e);

  for(pageid_t i = 0; i < arg->numPages; i++) {
    Page * p = loadPage(e->xid, arg->firstPage + i);
    if(stasis_operation_multi_should_apply(e, p)) {
      writelock(p->rwlatch, 0);
      if(arg->recordSize == 0) {
        stasis_page_slotted_initialize_page(p);
      } else if(arg->recordSize == BLOB_SLOT) {
        stasis_page_blob_initialize_page(p);
      } else {
        stasis_page_fixed_initialize_page(p, arg->recordSize,
                                     stasis_page_fixed_records_per_page
                                     (stasis_record_type_to_size(arg->recordSize)));
      }
      stasis_page_lsn_write(e->xid, p, e->LSN);
      unlock(p->rwlatch);
    }
    releasePage(p);
  }
  return 0;
}
int TinitializeSlottedPageRange(int xid, pageid_t start, pageid_t count) {
  init_multipage_arg arg;
  arg.firstPage = start;
  arg.numPages = count;
  arg.recordSize = 0;

  Tupdate(xid, MULTI_PAGEID, &arg, sizeof(arg), OPERATION_INITIALIZE_MULTIPAGE);
  return 0;
}
int TinitializeFixedPageRange(int xid, pageid_t start, pageid_t count, size_t size) {
  init_multipage_arg arg;
  arg.firstPage = start;
  arg.numPages = count;
  arg.recordSize = size;

  Tupdate(xid, MULTI_PAGEID, &arg, sizeof(arg), OPERATION_INITIALIZE_MULTIPAGE);
  return 0;
}
int TinitializeBlobPageRange(int xid, pageid_t start, pageid_t count) {
  init_multipage_arg arg;
  arg.firstPage = start;
  arg.numPages = count;
  arg.recordSize = BLOB_SLOT;

  Tupdate(xid, MULTI_PAGEID, &arg, sizeof(arg), OPERATION_INITIALIZE_MULTIPAGE);
  return 0;
}
stasis_operation_impl stasis_op_impl_page_initialize() {
  stasis_operation_impl o = {
    OPERATION_INITIALIZE_PAGE,
    UNINITIALIZED_PAGE,
    OPERATION_INITIALIZE_PAGE,
    OPERATION_NOOP,
    op_initialize_page
  };
  return o;
}
stasis_operation_impl stasis_op_impl_multipage_initialize() {
  stasis_operation_impl o = {
      OPERATION_INITIALIZE_MULTIPAGE,
      MULTI_PAGE,
      OPERATION_INITIALIZE_MULTIPAGE,
      OPERATION_NOOP,
      op_init_multipage_impl
  };
  return o;
}
