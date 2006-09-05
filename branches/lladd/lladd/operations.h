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
 * Interface for defining new logical operations.
 *
 * @ingroup LLADD_CORE OPERATIONS
 * $Id$
 */

#ifndef __OPERATIONS_H__
#define __OPERATIONS_H__

/*#include <stddef.h>*/
/*#include "common.h"*/




#include <lladd/constants.h>
#include <lladd/transactional.h>
#include <lladd/logger/logEntry.h>

BEGIN_C_DECLS


/* @type Function
 * function pointer that the operation will run
 */
typedef int (*Function)(int xid, recordid r, const void *d);

/* @type Operation

 * @param sizeofData size of the data that function accepts (as void*)
 * @param undo index into operations table of undo function (takes same args)
 * @param run what function to actually run
 */

/* @type Special cases
 */
#define SIZEOF_RECORD -1
#define NO_INVERSE -1
typedef struct {
  /**
   * ID of operation, also index into operations table
   */
	int id;
  /** 
      This value is the size of the arguments that this operation
      takes.  If set to SIZEOF_RECORD, then the size of the record
      that the operation affects will be used instead.
  */
	size_t sizeofData;
  /**
      Does this operation supply an undo operation?

      --Unneeded; just set undo to the special value NO_INVERSE.  
  */
  /*        int invertible; */
  /**
     Implementing operations that may span records is subtle.
     Recovery assumes that page writes (and therefore logical
     operations) are atomic.  This isn't the case for operations that
     span records.  Instead, there are two (and probably other) choices:

      - Periodically checkpoint, syncing the data store to disk, and
        writing a checkpoint operation.  No writes can be serviced
        during the sync, and this implies 'no steal'.  See: 

        @inproceedings{ woo97accommodating,
	author = "Seung-Kyoon Woo and Myoung-Ho Kim and Yoon-Joon Lee",
	title = "Accommodating Logical Logging under Fuzzy Checkpointing in Main Memory Databases",
	booktitle = "International Database Engineering and Application Symposium",
	pages = "53-62",
	year = "1997",
	url = "citeseer.ist.psu.edu/135200.html" }

	for a more complex scheme involving a hybrid logical/physical
	logging system that does not implement steal.

	The other option: 

      - Get rid of operations that span records entirely by
        splitting complex logical operations into simpler one.

	We chose the second option for now.
	
   */
	int undo;
	Function run;
} Operation;

/* These need to be installed, since they are required by applications that use LLADD. */
/*#include "constants.h"
  #include <lladd/bufferManager.h>*/

#include "operations/increment.h"
#include "operations/decrement.h"
#include "operations/set.h"
#include "operations/prepare.h"
#include "operations/lladdhash.h"
#include "operations/alloc.h"


extern Operation operationsTable[]; /* [MAX_OPERATIONS];  memset somewhere */

/** Performs an operation during normal execution. 

    Does not write to the log, and assumes that the operation's
    results are not already in the buffer manager.
*/
void doUpdate(const LogEntry * e);
/** Undo the update under normal operation, and during recovery. 

    Assumes that the operation's results are reflected in the contents of the buffer manager.

    Does not write to the log.

    @todo Currently, undos do not result in CLR entries, but they should.  (Should this be done here?)

*/
void undoUpdate(const LogEntry * e);
/** 
    Redoes an operation during recovery.  This is different than
    doUpdate because it checks to see if the operation needs to be redone
    before redoing it. (if(e->lsn > e->rid.lsn) { doUpdate(e); } return)

    Also, this is the only function in operations.h that can take
    either CLR or UPDATE log entries.  The other functions can only
    handle update entries.

    Does not write to the log.
*/
void redoUpdate(const LogEntry * e);

END_C_DECLS

#endif