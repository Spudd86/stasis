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
 * New version of logger.  Based on logger.h
 * 
 * $Id$
 * 
 */


#ifndef __LOGGER2_H__
#define __LOGGER2_H__

#include "logEntry.h"
#include "logHandle.h"
#include <lladd/operations.h>
/**
   Contains the state needed by the logging layer to perform
   operations on a transaction.
 */
typedef struct {
  int xid; 
  lsn_t prevLSN;
  LogHandle lh;
} TransactionLog;

/**
   Inform the logging layer that a new transaction has begun.
   Currently a no-op.
*/
TransactionLog LogTransBegin(int xid);

/**
  Write a transaction COMMIT to the log tail, then flush the log tail immediately to disk

  @return 0 if the transaction succeeds, an error code otherwise.
*/
void LogTransCommit(TransactionLog * l);

/**
  Write a transaction ABORT to the log tail

  @return 0 if the transaction was successfully aborted.
*/
void LogTransAbort(TransactionLog * l);

/**
  LogUpdate writes an UPDATE log record to the log tail
*/
LogEntry * LogUpdate(TransactionLog * l, recordid rid, int operation, const byte * args);
/* *
   (Was folded into LogUpdate.)

   Logs the fact that a rid has been allocated for a transaction.
   @ todo Should this be folded into LogUpdate?  (Make "alloc" an operation...)
   @ todo Surely, we need a dealloc!
 */
/*lsn_t LogTransAlloc(TransactionLog * l, recordid rid);*/


/**
   Write a compensation log record.  These records are used to allow
   for efficient recovery, and possibly for log truncation.  They
   record the completion of undo operations.

   @return the lsn of the CLR entry that was written to the log.
   (Needed so that the lsn slot of the page in question can be
   updated.)  

   @todo Remove this from this interface?  Should it be internal to
   the recovery routines?
*/
lsn_t LogCLR (LogEntry * undone); /*TransactionLog * l, long ulLSN, recordid ulRID, long ulPrevLSN); */

/**
   Write a end transaction record @todo What does this do exactly?  Indicate completion of aborts?
   @todo Move into recovery-only code?
*/
void LogEnd (TransactionLog * l);

#endif