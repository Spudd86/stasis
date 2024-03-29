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

/**
 * @file
 *
 * Provides raw access to entire pages.
 *
 * LLADD's pages are PAGE_SIZE bytes long.  Currently, two integers are
 * reserved for the LSN and the page type. providing PAGE_SIZE-8 bytes
 * of usable space.
 *
 * @ingroup OPERATIONS
 *
 * @see page.h
 *
 * $Id$
 */
#ifndef __PAGE_OPERATIONS_H__
#define __PAGE_OPERATIONS_H__
#include <stasis/operations.h>

pageid_t TpageAlloc(int xid);
pageid_t TfixedPageAlloc(int xid, int size);
pageid_t TpageAllocMany(int xid, int count);
int TpageDealloc(int xid, pageid_t page);
void TpageDeallocMany(int xid, pageid_t page);
int TpageSet(int xid, pageid_t page, const void* dat);
int TpageSetRange(int xid, pageid_t page, int offset, const void* dat, int len);
int TpageGet(int xid, pageid_t page, void* buf);

void TinitializeSlottedPage(int xid, pageid_t page);
void TinitializeFixedPage(int xid, pageid_t page,
					       int slotLength);
/**
 *  Format a contiguous range of pages as slotted pages.  This method writes a single, constant
 *  length log entry that does not contain the preimages of the pages in question.
 *  Therefore, it is only safe to call this function against newly allocated pages
 *  with uninteresting contents (such as those returned by TregionAlloc().
 *
 *  @see TinitializeFixedPageRange for an analogous function that initializes "fixed" pages.
 */
int TinitializeSlottedPageRange(int xid, pageid_t start, pageid_t count);
/**
 * Initialize a contiguous range of pages for storage of fixed size records.
 *
 * This method uses blind-writes to conserve log bandwidth.  It is only safe
 * to call this function against newly allocated pages.
 *
 * @see fixed.c for more information about the page format.
 */
int TinitializeFixedPageRange(int xid, pageid_t start, pageid_t count, size_t size);
/**
 * Initialize a contiguous range of pages for storage of a blob (large object).
 *
 * @see blobManager.c for more information about the page format.
 */
int TinitializeBlobPageRange(int xid, pageid_t start, pageid_t count);

int TpageGetType(int xid, pageid_t page);

stasis_operation_impl stasis_op_impl_page_set_range();
stasis_operation_impl stasis_op_impl_page_set_range_inverse();
stasis_operation_impl stasis_op_impl_page_initialize();
stasis_operation_impl stasis_op_impl_multipage_initialize();

stasis_operation_impl stasis_op_impl_fixed_page_alloc();

void pageOperationsInit(stasis_log_t *log);

#endif
