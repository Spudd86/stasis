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
   @file

   Abstract log implementation.  Provides access to methods that
   directly read and write log entries, force the log to disk, etc.
*/
#include <stasis/logger/logger2.h>

#include <stasis/logger/safeWrites.h>
#include <stasis/logger/inMemoryLog.h>
#include <stasis/page.h>

static lsn_t stasis_log_write_common(stasis_log_t* log, stasis_transaction_table_entry_t * l, int type) {
  LogEntry * e = allocCommonLogEntry(log, l->prevLSN, l->xid, type);
  lsn_t ret;

  log->write_entry(log, e);

//  pthread_mutex_lock(&l->mut);
  if(l->prevLSN == INVALID_LSN) { l->recLSN = e->LSN; }
  l->prevLSN = e->LSN;
//  pthread_mutex_unlock(&l->mut);

  DEBUG("Log Common %d, LSN: %ld type: %ld (prevLSN %ld)\n", e->xid,
	 (long int)e->LSN, (long int)e->type, (long int)e->prevLSN);

  ret = e->LSN;

  log->write_entry_done(log, e);

  return ret;
}

static lsn_t stasis_log_write_prepare(stasis_log_t* log, stasis_transaction_table_entry_t * l) {
  LogEntry * e = allocPrepareLogEntry(log, l->prevLSN, l->xid, l->recLSN);
  lsn_t ret;

  DEBUG("Log prepare xid = %d prevlsn = %lld reclsn = %lld, %lld\n",
        e->xid, e->prevLSN, l->recLSN, getPrepareRecLSN(e));
  log->write_entry(log, e);

//  pthread_mutex_lock(&l->mut);
  if(l->prevLSN == INVALID_LSN) { l->recLSN = e->LSN; }
  l->prevLSN = e->LSN;
//  pthread_mutex_unlock(&l->mut);
  DEBUG("Log Common prepare XXX %d, LSN: %ld type: %ld (prevLSN %ld)\n",
        e->xid, (long int)e->LSN, (long int)e->type, (long int)e->prevLSN);

  ret = e->LSN;

  log->write_entry_done(log, e);

  return ret;

}

LogEntry * stasis_log_write_update(stasis_log_t* log, stasis_transaction_table_entry_t * l,
                                   pageid_t page, Page * p, unsigned int op,
                                   const byte * arg, size_t arg_size) {
  stasis_transaction_table_entry_t dummy = { -1, -1, INVALID_LSN, INVALID_LSN, {0,0,0}, -1 };
  if(l == 0) { l = &dummy; }

  LogEntry * e = allocUpdateLogEntry(log, l->prevLSN, l->xid, op,
                                     page, arg_size);
  memcpy(stasis_log_entry_update_args_ptr(e), arg, arg_size);

  log->write_entry(log, e);
  DEBUG("Log Update %d, LSN: %ld type: %ld (prevLSN %ld) (arg_size %ld)\n", e->xid,
	 (long int)e->LSN, (long int)e->type, (long int)e->prevLSN, (long int) arg_size);
//  pthread_mutex_lock(&l->mut);
  if(l->prevLSN == INVALID_LSN) { l->recLSN = e->LSN; }
  l->prevLSN = e->LSN;
//  pthread_mutex_unlock(&l->mut);
  return e;
}
// XXX change nta interface so that arg gets passed into end_nta, not begin_nta.
void * stasis_log_begin_nta(stasis_log_t* log, stasis_transaction_table_entry_t * l, unsigned int op,
                                const byte * arg, size_t arg_size) {
  LogEntry * e = mallocScratchUpdateLogEntry(INVALID_LSN, l->prevLSN, l->xid, op, INVALID_PAGE, arg_size);
  memcpy(stasis_log_entry_update_args_ptr(e), arg, arg_size);
  return e;
}

lsn_t stasis_log_end_nta(stasis_log_t* log, stasis_transaction_table_entry_t * l, LogEntry * e) {
  LogEntry * realEntry = allocUpdateLogEntry(log, e->prevLSN, e->xid, e->update.funcID, e->update.page, e->update.arg_size);
  memcpy(stasis_log_entry_update_args_ptr(realEntry), stasis_log_entry_update_args_cptr(e), e->update.arg_size);

  log->write_entry(log, realEntry);
//  pthread_mutex_lock(&l->mut);
  if(l->prevLSN == INVALID_LSN) { l->recLSN = realEntry->LSN; }
  lsn_t ret = l->prevLSN = realEntry->LSN;
//  pthread_mutex_unlock(&l->mut);
  log->write_entry_done(log, realEntry);

  free(e);
  return ret;
}

lsn_t stasis_log_write_clr(stasis_log_t* log, const LogEntry * old_e) {
  LogEntry * e = allocCLRLogEntry(log, old_e);
  log->write_entry(log, e);

  DEBUG("Log CLR %d, LSN: %ld (undoing: %ld, next to undo: %ld)\n", xid,
  	 e->LSN, LSN, prevLSN);
  lsn_t ret = e->LSN;
  log->write_entry_done(log, e);

  return ret;
}

lsn_t stasis_log_write_dummy_clr(stasis_log_t* log, int xid, lsn_t prevLSN) {
  // XXX waste of log bandwidth.
  LogEntry * e = mallocScratchUpdateLogEntry(INVALID_LSN, prevLSN, xid, OPERATION_NOOP,
              INVALID_PAGE, 0);
  lsn_t ret = stasis_log_write_clr(log, e);
  free(e);
  return ret;
}

void stasis_log_begin_transaction(stasis_log_t* log, int xid, stasis_transaction_table_entry_t* tl) {
  tl->xid = xid;

  DEBUG("Log Begin %d\n", xid);
  tl->prevLSN = INVALID_LSN;
  tl->recLSN = INVALID_LSN;
}

lsn_t stasis_log_abort_transaction(stasis_log_t* log, stasis_transaction_table_t *table, stasis_transaction_table_entry_t * l) {
  stasis_transaction_table_invoke_callbacks(table, l, PRE_COMMIT);
  lsn_t ret = stasis_log_write_common(log, l, XABORT);
  // rest of callbacks happen after rollback completes, in end_aborted_transaction.
  return ret;
}
lsn_t stasis_log_end_aborted_transaction(stasis_log_t* log, stasis_transaction_table_t *table, stasis_transaction_table_entry_t * l) {
  lsn_t ret = stasis_log_write_common(log, l, XEND);
  stasis_transaction_table_invoke_callbacks(table, l, AT_COMMIT);
  stasis_transaction_table_invoke_callbacks(table, l, POST_COMMIT);
  return ret;
}
lsn_t stasis_log_prepare_transaction(stasis_log_t* log, stasis_transaction_table_entry_t * l) {
  lsn_t lsn = stasis_log_write_prepare(log, l);
  stasis_log_force(log, lsn, LOG_FORCE_COMMIT);
  return lsn;
}


lsn_t stasis_log_commit_transaction(stasis_log_t* log, stasis_transaction_table_t *table, stasis_transaction_table_entry_t * l, int force) {
  stasis_transaction_table_invoke_callbacks(table, l, PRE_COMMIT);
  lsn_t lsn = stasis_log_write_common(log, l, XCOMMIT);
  stasis_transaction_table_invoke_callbacks(table, l, AT_COMMIT);
  if(force) {
    stasis_log_force(log, lsn, LOG_FORCE_COMMIT);
  }
  stasis_transaction_table_invoke_callbacks(table, l, POST_COMMIT);
  return lsn;
}

void stasis_log_force(stasis_log_t* log, lsn_t lsn,
              stasis_log_force_mode_t mode) {
  //  if(lsn == INVALID_LSN) {
  //    log->force_tail(log,mode);
  //  } else {
  if((mode == LOG_FORCE_COMMIT) && log->group_force) {
    stasis_log_group_force(log->group_force, lsn);
  } else {
    if(log->first_unstable_lsn(log,mode) <= lsn) {
      log->force_tail(log,mode);
    }
  }
    //  }
}
