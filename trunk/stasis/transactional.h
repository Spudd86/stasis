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
 * @defgroup LLADD_CORE  Core API
 *
 * The minimal subset of Stasis necessary to implement transactional consistency.
 *
 * This module includes the standard API (excluding operations), the
 * logger, the buffer manager, and recovery code.
 *
 * In theory, the other .h files that are installed in /usr/include
 * aren't needed for application developers.
 *
 * @todo Move as much of the stuff in stasis/ to src/stasis/ as possible.  Alternatively, move all headers to stasis/, and be done with it!
 *
 */
/**
   @mainpage Introduction to Stasis
   
   This is the main section.
   <ul>
     <li>@ref gettingStarted</li>
     <li>@ref pageFormats</li>
     <li>@ref LLADD_CORE</li>
     <li>@ref OPERATIONS</li>
   </ul>
*/

/**
   @page gettingStarted Getting Started
   @section compiling Compiling and installation

   Prerequisites:
   
   - automake 1.8+: needed to build from CVS 
   - <a href="http://check.sourceforge.net">check</a>: A unit testing 
     framework (needed to run the self-tests)

   Optional:

   - libconfuse: Used by older networking code to parse configuration options.
   - BerkeleyDB: Used by the benchmarking code for purposes of comparison. 
   
   Development is currently performed under Debian's Testing branch.
   
   To compile Stasis, first check out a copy with SVN.  If you have commit access:

   @verbatim
   svn co --username username https://stasis.googlecode.com/svn/trunk stasis
   @endverbatim

   For anonymous checkout:

   @verbatim
   svn co http://stasis.googlecode.com/svn/trunk stasis
   @endverbatim

   then:
 
   @code
   
   $ ./reconf
   $ ./configure --quiet
   $ make -j4 > /dev/null
   $ cd test/stasis
   $ make check
   
   @endcode
   
   This will fail if your system defaults to an old (pre-1.7) version
   of autotools.  Fortunately, multiple versions of autotools may
   exist on the same system.  Execute the following commands to
   compile with version 1.8 of autotools:

   @code

   $ ./reconf-1.8
   $ ./configure --quiet
   $ make -j4 > /dev/null
   $ cd test/stasis
   $ make check

   @endcode

   Of course, you can omit "--quiet" and "> /dev/null", but configure
   and make both produce quite a bit of output that may obscure useful
   warning messages.

   'make install' installs the Stasis library and python SWIG
   bindings, but none of the extra programs that come with Stasis.
   utilities/ contains a number of utility programs that are useful
   for debugging Stasis.  The examples/ directory contains a number of
   simple C examples.

   @section usage Using Stasis in your software

   Synopsis:

   @include examples/ex1.c

   Hopefully, Tbegin(), Talloc(), Tset(), Tcommit(), Tabort() and Tdealloc() are 
   self explanatory.  If not, they are covered in detail elsewhere.  Tinit() and 
   Tdeinit() initialize the library, and clean up when the program is finished.
   
   Other particularly useful functions are ThashCreate(), ThashDelete(),
   ThashInsert(), ThashRemove(), and ThashLookup() which provide a
   re-entrant linear hash implementation.  ThashIterator() and
   ThashNext() provide an iterator over the hashtable's values.
   
   @subsection bootstrap Reopening a closed data store
   
   Stasis imposes as little structure upon the application's data structures as 
   possible.  Therefore, it does not maintain any information about the contents
   or naming of objects within the page file.  This means that the application 
   must maintain such information manually.
   
   In order to facilitate this, Stasis provides the function TgetRecordType() and
   guarantees that the first recordid returned by any allocation will point to 
   the same page and slot as the constant ROOT_RECORD.  TgetRecordType 
   will return NULLRID if the record passed to it does not exist.  
   
   Therefore, the following code will safely initialize or reopen a data 
   store:
   
   @include examples/ex2.c

   @see test.c for a complete, executable example of reopening an existing store.

   @todo Explain how to determine the correct value of rootEntry.size in the case
         of a hashtable.


   @see OPERATIONS for more operations that may be useful for your software.

   @subsection consistency  Using Stasis in multithreaded applications.

   Unless otherwise noted, Stasis' operations are re-entrant.  This
   means that an application may call them concurrently without
   corrupting Stasis' internal data structures.  However, if two
   threads attempt to write the same data value simultaneously, the
   result is undefined.

   In database terms, Stasis uses latches to protect its own data
   structures' consistency (including those on disk), but does not
   obtain short term read or write locks to protect data as it is
   being written.  This is less consistency than SQL's Level 0 (Dirty
   Reads) provides.  Some of Stasis' data structures do obtain short
   read and write locks automatically.  Refer to individual data
   structures for more information.

   Stasis' allocation functions, such as Talloc(), do not reuse space
   that was freed by an ongoing transaction.  This means that you may
   safely overwrite freshly allocated space without writing undo
   entries, and allows concurrent transactions to safely allocate
   space.

   From the point of view of conventional multithreaded software
   development, Stasis closely matches the semantics provided by
   typical operating system thread implementations.  However, it
   allows transactions to abort and rollback independently of each
   other.  This means that transactions may observe the effects of
   transactions that will eventually abort.

   Finally, Stasis assumes that each thread has its own transaction;
   concurrent calls within the same transaction are not supported.
   This restriction may be removed in the future.

   @section selfTest The test suite

   Stasis includes an extensive unit test suite which may be invoked
   by running 'make check' in Stasis' root directory.  Some of the
   tests are for older, unmaintained code that was built on top of
   Stasis.  Running 'make check' in test/stasis runs all of the Stasis
   tests without running the obsolete tests.

   @section architecture Stasis' structure

   This section is geared toward people that would like to extend
   Stasis.  The OSDI paper provides a higher level description and
   motivation for the architecture.  This section describes naming
   conventions used to distinguish between different portions of
   Stasis, and provides an overview of memory management and mutex
   acquisition conventions.

   This section does not describe recovery, transaction initiation,
   etc.  Those methods change less frequently.  Instead of focusing on
   them, this text focuses on the issues faced by transactional data
   structures.

   Stasis components can be classified as follows:

   - I/O utilities (file handles, OS compatibility wrappers)
   - Write ahead logging component interfaces (logger.h, XXX)
   - Write ahead logging component implementations (hash based buffer manager, in memory log, etc...)
   - Page formats and associated operations (page/slotted.c page/fixed.c)
   - Application visible methods (Talloc, Tset, ThashInsert, etc)

   @subsection layoutNaming Directory layout

   The Stasis repository contains the following "interesting" directories:

   @par $STASIS/stasis/

   Contains the header directory structure.

   In theory, this contains all of the .h files that need to be
   installed for a fully functional Stasis development environment.
   In practice, .h files in src/ are also  needed in some cases.  The
   separation of .h files between src/ and stasis/ continues for
   various obscure reasons, including CVS's lack of a "move" command.
   For now, .h files should be placed with similar .h files, or in
   stasis/ if no such files exist.

   The directory structure of stasis/ mirrors that of src/

   @par $STASIS/src/

   Contains the .c files

   @par $STASIS/src/stasis

   Contains Stasis and the implementations of its standard modules.
   The subdirectories group files by the type of module they
   implement.

   @note By convention, when the rest of this document says
   <tt>foo/</tt>, it is referring to two directories:
   <tt>stasis/foo/</tt> and <tt>src/stasis/foo/</tt>.  Unless it's clear
   from context, a file without an explicit directory name is in
   <tt>stasis/</tt> or <tt>src/stasis/</tt>.  In order to refer to files
   and directories outside of these two locations, but still in the
   repository, this document will use the notation
   <tt>$STASIS/dir</tt>.

   @note This is done for brevity, and to avoid coupling documentation
   to the (deprecated) placement of .h files under src/.

   @note <b>Example:</b> The transactional data structure
   implementations in <tt>operations/</tt> can be found in
   <tt>$STASIS/src/stasis/operations/</tt> and
   <tt>$STASIS/stasis/operations/</tt>.

   @subsection Modules

   Stasis is implemented in C, but is structured in a somewhat object
   oriented style.  There are a number of different "modules", for
   lack of a better term.  Each implementation in the module lives in
   the module's subdirectory.  Code that is common to many
   implementation, and headers that define per-module functions live
   in files named after the module.

   <b>Example:</b> The <tt>io</tt> module contains the following files:

   @code
      io.h
      io.c
      io/handle.h
      io/debug.c
      io/file.c
      io/memory.c
      io/non_blocking.c
      io/rangeTracker.h
      io/rangeTracker.c
   @endcode

   In this case, rangeTracker.c and io.c are the only files containing
   more than one non-static method, so they are the only ones that
   have corresponding .h files.  rangeTracker.c is implementing a data
   structure that is being used by the other files.  debug.c, file.c,
   memory.c and non_blocking.c each implements a different type of
   handle.

   Some modules are simply groups of files that perform similar tasks,
   or make use of the same set of interfaces (eg: <tt>page/</tt> and
   <tt>operations/</tt>).  Files in these directories may make use of the same
   utility functions, but aren't implementing the same interface.

   Other modules provide multiple implementations of the same
   interface (eg: <tt>io/</tt> and <tt>logger/</tt>).  C doesn't have
   inheritance, so Stasis "fakes it" using one of two methods.  In
   both cases, a struct is defined to contain a void pointer, which
   the implementation manually casts to the appropriate type:

   @par Dispatch functions

   The dispatch functions contain a switch statement or conditional
   that decides which implementation to call. Calling convention:

   @code bird_carry(african_swallow, coconut); @endcode

   @par struct of function pointers

   These functions use the following calling convention:

   @code african_swallow->carry(african_swallow,coconut) @endcode

   @subsection ioutil I/O utilities

   The I/O utilities live in <tt>io/</tt>.  They provide reentrant
   interfaces.  This was written to insulate Stasis from Linux's
   ever-evolving I/O system calls, for portability, and to allow (for
   example) in-memory operation.

   @subsection walin WAL Modules

   None of these modules understand page formats; at this level
   everything is either

   - a page with an LSN (a version number),or

   - a log entry with an associated operation (redo / undo
   functions).

   Interesting files in this part of Stasis include logger2.c,
   bufferManager.c, and recovery2.c.

   @subsection page Page types

   Page types define the layout of data on pages.  Currently, all
   pages contain a header with an LSN and a page type in it.  This
   information is used by recovery and the buffer manager to invoke
   callbacks at appropriate times.  (LSN-free pages are currently not
   supported.)

   XXX: This section is not complete.

   @todo Discuss readRecord, writeRecord (high level page access
   methods)

   @todo Explain the latching convention.  (Also, explain which
   latches are not to be used by page implementations, and which
   latches may not be used by higher level code.)

   @par Registering new page type implementations

   Page implementations are registered with Stasis by passing a
   page_impl struct into registerPageType().  page_impl.page_type
   should contain an integer that is unique across all page types,
   while the rest of the fields contain function pointers to the page
   type's implementation.

   @par Pointer arithmetic

   Stasis page type implementations typically do little more than
   pointer arithmetic.  However, implementing page types cleanly and
   portably is a bit tricky.  Stasis has settled upon a compromise in
   this matter.  Its page file formats are compatible within a single
   architecture, but not across systems with varying lengths of
   primitive types, or that vary in endianness.

   Over time, types that vary in length such as "int", "long", etc
   will be removed from Stasis, but their usage still exists in a few
   places.  Once they have been removed, file compatibility problems
   should be limited to endianness (though application code will still
   be free to serialize objects in a non-portable manner).

   Most page implementations leverage C's pointer manipulation
   semantics to lay out pages.  Rather than casting pointers to
   char*'s and then manually calculating byte offsets using sizeof(),
   the existing page types prefer to cast pointers to appropriate
   types, and then add or subtract the appropriate number of values.

   For example, instead of doing this:

   @code
   // p points to an int, followed by a two bars, then the foo whose address
   // we want to calculate

   int * p;
   foo* f = (foo*)( ((char*)p) + sizeof(int) + 2 * sizeof(bar))
   @endcode

   the implementations would do this:

   @code
   int * p;
   foo * f = (foo*)( ((bar*)(p+1)) + 2 )
   @endcode

   The main disadvantage of this approach is the large number of ()'s
   involved.  However, it lets the compiler deal with the underlying
   multiplications, and often reduces the number of casts, leading to
   slightly more readable code.  Take this implementation of
   stasis_page_type_ptr(), for example:

   @code
   int * stasis_page_type_ptr(Page *p) { 
      return ( (int*)stasis_page_lsn_ptr(Page *p) ) - 1; 
   }
   @endcode

   Here, the page type is stored as an integer immediately before the
   LSN pointer.  Using arithmetic over char*'s would require an extra
   cast to char*, and a multiplication by sizeof(int).

   @par A note on storage allocation

   Finally, while Stasis will correctly call appropriate functions
   when it encounters a properly registered third party page type, it
   currently provides few mechanisms to allocate such pages in the
   first place.  There are examples of three approaches in the current
   code base:

   # Implement a full-featured, general purpose allocator, like the
     one in alloc.h.  This is more difficult than it sounds.

   # Allocate entire regions at a time, and manually initialize pages
     within them.  arrayList.h attempts to do this, but gets it wrong
     by relying upon lazy initialization to eventually set page types
     correctly.  Doing so is problematic if the page was deallocated,
     then reused without being reset.

   # Allocate a single page at a time using TallocPage(), and
     TsetPage().  This is currently the most attractive route, though
     TsetPage() does not call pageLoaded() when it resets page types,
     which can lead to trouble.

   @see page.h, fixed.h, and slotted.h for more information on the
   page API's, and the implementations of two common page formats.

   @subsection appfunc Application visible methods

   These methods start with "T".  Look at the examples above.  These
   are the "wrapper functions" from the OSDI paper.  They are
   supported by operation implementations, which can be found in the
   operations/ directory.

   @section extending Implementing you own operations

   @todo Provide a tutorial that explains how to extend Stasis with new operations.

   @see increment.h for an example of a very simple logical operation.
   @see linearHashNTA.h for a more sophisticated example that makes use of Nested Top Actions.

*/
/**
 * @defgroup pageFormats Page format implementations
 */
/**
 * @defgroup OPERATIONS  Logical Operations
 *
 * Implementations of logical operations, and the interfaces that allow new operations to be added.
 *
 * @todo Write a brief howto to explain the implementation of new operations.
 *
 */

/**
 * @file 
 *
 * Defines Stasis' primary interface.
 *
 *
 *
 * @todo error handling 
 *
 * @ingroup LLADD_CORE
 * $Id$
 */




#ifndef __TRANSACTIONAL_H__
#define __TRANSACTIONAL_H__

#include "common.h"
#include "flags.h"

BEGIN_C_DECLS

/**
 * represents how to look up a record on a page
 * @todo recordid.page should be 64bit.
 * @todo int64_t (for recordid.size) is a stopgap fix.
 */
#pragma pack(push)
#pragma pack(1)
typedef struct {
  int page;  // XXX needs to be pageid_t, but that breaks unit tests.
  int slot;
  int64_t size; //signed long long size;
} recordid;

typedef struct {
  size_t offset;
  size_t size;
  // unsigned fd : 1;
} blob_record_t;
#pragma pack(pop)


extern const recordid ROOT_RECORD;
extern const recordid NULLRID;

/**
   If a recordid's slot field is set to this, then the recordid
   represents an array of fixed-length records starting at slot zero
   of the recordid's page.

   @todo Support read-only arrays of variable length records, and then
   someday read / write / insert / delete arrays...
*/
#define RECORD_ARRAY (-1)


#include "operations.h"

/**
 * Currently, Stasis has a fixed number of transactions that may be
 * active at one time.
 */
#define EXCEED_MAX_TRANSACTIONS 1

/**
 * @param xid transaction ID
 * @param LSN last log that this transaction used
 */
typedef struct {
	int xid;
	long LSN;
} Transaction;



/**
 * Initialize Stasis.  This opens the pagefile and log, initializes
 * subcomponents, and runs recovery.
 *
 * @return 0 on success
 */
int Tinit();

/**
 * Start a new transaction, and return a new transaction id (xid).
 *
 * @return positive transaction ID (xid) on success, negative return value on error
 */
int Tbegin();

/**
 * Used when extending Stasis.
 * Operation implementers should wrap around this function to provide more mnemonic names.
 *
 * @param xid          The current transaction.
 * @param rid          The record the operation pertains to.  For some logical operations, this will be a dummy record.
 * @param dat          Application specific data to be recorded in the log (for undo/redo), and to be passed to the implementation of op.
 * @param op           The operation's offset in operationsTable
 *
 * @see operations.h set.h
 */
compensated_function void Tupdate(int xid, recordid rid, 
				  const void *dat, int op);
compensated_function void TupdateStr(int xid, recordid rid, 
                                     const char *dat, int op);
compensated_function void TupdateRaw(int xid, recordid rid, 
				     const void *dat, int op);
compensated_function void TupdateDeferred(int xid, recordid rid, 
					  const void *dat, int op);
/**
 * Read the value of a record.
 * 
 * @param xid transaction ID
 * @param rid reference to page/slot
 * @param dat buffer into which data goes
 */
compensated_function void Tread(int xid, recordid rid, void *dat);
compensated_function void TreadStr(int xid, recordid rid, char *dat);

/**
 * Commit an active transaction.  Each transaction should be completed 
 * with exactly one call to Tcommit() or Tabort().
 * 
 * @param xid transaction ID
 * @return 0 on success
 */
int Tcommit(int xid);

/**
 * Abort (rollback) an active transaction.  Each transaction should be
 * completed with exactly one call to Tcommit() or Tabort().
 * 
 * @param xid transaction ID 
 * @return 0 on success, -1 on error.
 */
int Tabort(int xid);

/**
 * Cleanly shutdown Stasis.  After this function is called, you should
 * call Tinit() before attempting to access data stored in Stasis.
 * This function flushes all pages, cleans up log, and frees any
 * resources that Stasis is holding.
 *
 * @return 0 on success
 */
int Tdeinit();
/**
 * Uncleanly shutdown Stasis.  This function frees any resources that
 * Stasis is holding, and flushes the log, but it does not flush dirty
 * pages to disk.  This is used by testing to exercise the recovery
 * logic.
 *
 * @return 0 on success
*/
int TuncleanShutdown();
/**
 *  Used by the recovery process.
 *  Revives Tprepare'ed transactions.
 *
 * @param xid  The xid that is to be revived. 
 * @param lsn  The lsn of that xid's most recent PREPARE entry in the log.
 */
void Trevive(int xid, long lsn);

/**
 *  Used by the recovery process. 
 *
 *  Sets the number of active transactions. 
 *  Should not be used elsewhere.
 *
 * @param xid  The new active transaction count. 
 */
void TsetXIDCount(int xid);

/**
 * Checks to see if a transaction is still active.
 *
 * @param xid The transaction id to be tested.
 * @return true if the transaction is still running, false otherwise.
 */
int TisActiveTransaction(int xid);

/** 
    This is used by log truncation.
*/
lsn_t transactions_minRecLSN();


/**
   Report Stasis' current durability guarantees.

   @return VOLATILE if the data will be lost after Tdeinit(), or a
   crash, PERSISTENT if the data will be written back to disk after
   Tdeinit(), but may be corrupted at crash, or DURABLE if Stasis will
   apply committed transactions, and roll back active transactions
   after a crash.
*/
int TdurabilityLevel();
END_C_DECLS

#endif
