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
#include <lladd/logger/logWriter.h>

#include <assert.h>
#include <config.h>

#include <malloc.h>
#include <unistd.h>
/** 
    Invariant: This file stream is at EOF, or positioned so that the
    next read will pull in the size of the next log entry.

    @todo Should the log file be global? 
*/
static FILE * log;


int openLogWriter() {
  log = fopen(LOG_FILE, "a+");
  if (log==NULL) {
    assert(0);
    /*there was an error opening this file */
    return FILE_WRITE_OPEN_ERROR;
  }

  /* Note that the position of the file between calls to this library
     does not matter, since none of the functions in logWriter.h
     assume anything about the position of the stream before they are
     called.

     However, we need to do this seek to check the length of the file.

  */
  fseek(log, 0, SEEK_END); 

  if (ftell(log)==0) {
    /*if file is empty, write an LSN at the 0th position.  LSN 0 is
      invalid, and this prevents us from using it.  Also, the LSN at
      this position can be used after log truncation to define a
      global offset for the truncated log.  (Not implemented yet)
    */
    lsn_t zero = 0;
    int nmemb = fwrite(&zero, sizeof(lsn_t), 1, log);
    if(nmemb != 1) {
      perror("Couldn't start new log file!");
      assert(0);
      return FILE_WRITE_OPEN_ERROR;
    }
  }

  return 0;
}

int writeLogEntry(LogEntry * e) {
  int nmemb;
  const size_t size = sizeofLogEntry(e);

  /* Set the log entry's LSN. */
  fseek(log, 0, SEEK_END);
  e->LSN = ftell(log);
 
  /* Print out the size of this log entry.  (not including this item.) */
  nmemb = fwrite(&size, sizeof(size_t), 1, log);

  if(nmemb != 1) {
    perror("writeLog couldn't write next log entry size!");
    assert(0);
    return FILE_WRITE_ERROR;
  }
  
  nmemb = fwrite(e, size, 1, log);

  if(nmemb != 1) {
    perror("writeLog couldn't write next log entry!");
    assert(0);
    return FILE_WRITE_ERROR;
  }
  
  /* We're done. */
  return 0;
}

void syncLog() {
  fflush(log);
#ifdef HAVE_FDATASYNC
  /* Should be available in linux >= 2.4 */
  fdatasync(fileno(log)); 
#else
  /* Slow - forces fs implementation to sync the file metadata to disk */
  fsync(fileno(log));  
#endif
}
void closeLogWriter() {
  /* Get the whole thing to the disk before closing it. */
  syncLog();  
  fclose(log);
}

void deleteLogWriter() {
  remove(LOG_FILE);
}

static LogEntry * readLogEntry() {
  LogEntry * ret = NULL;
  size_t size;
  int nmemb;
  
  if(feof(log)) return NULL;

  nmemb = fread(&size, sizeof(size_t), 1, log);
  
  if(nmemb != 1) {
    if(feof(log)) return NULL;
    if(ferror(log)) {
      perror("Error reading log!");
      assert(0);
      return 0;
    }
  }

  ret = malloc(size);

  nmemb = fread(ret, size, 1, log);

  if(nmemb != 1) {
    /* Partial log entry. */
    if(feof(log)) return NULL;
    if(ferror(log)) {
      perror("Error reading log!");
      assert(0);
      return 0;
    }
    assert(0);
    return 0;
  }

  /** Sanity check -- Did we get the whole entry? */
  assert(size == sizeofLogEntry(ret));

  return ret;
}

LogEntry * readLSNEntry(lsn_t LSN) {
  LogEntry * ret;

  fseek(log, LSN, SEEK_SET);
  ret = readLogEntry();

  return ret;
}

/*lsn_t nextLSN() {
  lsn_t orig_pos = ftell(log);
  lsn_t ret; 
  
  fseek(log, 0, SEEK_END);
  
  ret = ftell(log);

  fseek(log, orig_pos, SEEK_SET);

  return ret;
  }*/
