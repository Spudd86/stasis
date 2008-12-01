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
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

/** For O_DIRECT.  It's unclear that this is the correct thing to \#define, but it works under linux. */
#define __USE_GNU
#include <fcntl.h>
#include <unistd.h>


#define _XOPEN_SOURCE 600
#include <stdlib.h>

#include <config.h>
#include <stasis/common.h>

#include <stasis/crc32.h>
#include <stasis/logger/logWriter.h>
#include <stasis/logger/logHandle.h>
#include <stasis/latches.h>
#include <stasis/logger/logWriterUtils.h>
#include <assert.h>


#include <stasis/bufferManager.h>

/**
   @todo remove all static vairables from logWriter.c
*/
static FILE * logFILE = 0;
static int roLogFD = 0;

static lsn_t debug_lsn = -1;

static char * log_filename = 0;
static char * log_scratch_filename = 0;
static int    log_filemode = 0;
static int    log_fileperm = 0;
static char   log_softcommit = 0;
/**
   @see flushedLSN_LogWriter()
*/
/**
   The first unstable LSN for write ahead logging purposes.
*/
static lsn_t flushedLSN_wal;
/**
   The first unstable LSN for commit purposes.  This can be greater than
   flushedLSN_wal.
*/
static lsn_t flushedLSN_commit;
/**
   The first LSN that hasn't made it into roLogFD.  This can be greater than
   flushedLSN_commit
*/
static lsn_t flushedLSN_internal;
/**
   Invariant: No thread is writing to flushedLSN.  Since
   flushedLSN is monotonically increasing, readers can immmediately
   release their locks after checking the value of flushedLSN.
*/
static rwl * flushedLSN_lock;

/**
   The LSN that will be assigned to the next log entry.
*/
static lsn_t nextAvailableLSN;

/**
   The global offset for the current version of the log file.
 */
static lsn_t global_offset;

// Lock order:  truncateLog_mutex, log_write_mutex, log_read_mutex

/**
   This mutex makes sequences of calls to lseek() and read() atomic.
   It is also used by truncateLog to block read requests while
   rename() is called.
*/
static pthread_mutex_t log_read_mutex;

/**
   Invariant: Any thread writing to the file must hold this lock.  The
   log truncation thread hold this lock from the point where it copies
   the tail of the old log to the new log, until after the rename call
   returns.
*/
static pthread_mutex_t log_write_mutex;
/**
   This mutex protects nextAvailableLSN, which has its own mutex
   because the routines for reading and writing log entries both need
   to acquire it, but only for a relatively short time.
*/
static pthread_mutex_t nextAvailableLSN_mutex;

/**
   Invariant:  We only want one thread in truncateLog at a time.
*/
static pthread_mutex_t truncateLog_mutex;

static char * buffer;

/** 
    CRC of the log between last CRC log entry, and the current end of
    the log.  The CRC includes everything except for the CRC log entry
    and the size fields preceeding each log entry.  This value is
    reset to zero each time a CRC entry is generated..
*/
static unsigned int log_crc;

/** The size of the in-memory log buffer.  When the buffer is full,
    the log is synchronously flushed to disk. */
#define BUFSIZE (1024 * 1024)

static inline void update_log_crc(const LogEntry * le, unsigned int * crc) { 
  *crc = stasis_crc32(le, sizeofLogEntry(le), *crc);
}
// internal methods
static void syncLogInternal();
static LogEntry* readLogEntry();
static inline lsn_t nextEntry(const LogEntry* e) {
  return e->LSN + sizeofLogEntry(e) + sizeof(lsn_t);
}

// implementations of log api methods
static lsn_t sizeofInternalLogEntry_LogWriter(stasis_log_t * log,
                                              const LogEntry * e);
static void syncLog_LogWriter();
static lsn_t flushedLSN_LogWriter();
static const LogEntry* readLSNEntry_LogWriter(stasis_log_t * log, lsn_t lsn);
static int truncateLog_LogWriter(stasis_log_t* log, lsn_t lsn);
static int close_LogWriter(stasis_log_t * log);
static int writeLogEntry_LogWriter(stasis_log_t* log, LogEntry * e);

static inline int isDurable_LogWriter(stasis_log_t* log) {
  return !log_softcommit;
}
static inline lsn_t firstLogEntry_LogWriter(stasis_log_t* log);

static inline lsn_t nextEntry_LogWriter(stasis_log_t* log,
                                        const LogEntry* e) {
  return nextEntry(e);
}

stasis_log_t* openLogWriter(const char * filename,
                            int filemode, int fileperm) {

  log_filename = strdup(filename);
  log_scratch_filename = malloc(strlen(log_filename) + 2);
  strcpy(log_scratch_filename, log_filename);
  strcat(log_scratch_filename, "~");
  log_filemode = filemode;
  log_fileperm = fileperm;
  log_softcommit = !(filemode & O_SYNC);

  stasis_log_t proto = {
    sizeofInternalLogEntry_LogWriter, // sizeof_internal_entry
    writeLogEntry_LogWriter,// write_entry
    readLSNEntry_LogWriter, // read_entry
    nextEntry_LogWriter,// next_entry
    flushedLSN_LogWriter, // first_unstable_lsn
    syncLog_LogWriter, // force_tail
    truncateLog_LogWriter, // truncate
    firstLogEntry_LogWriter,// truncation_point
    close_LogWriter, // deinit
    isDurable_LogWriter // is_durable
  };
  stasis_log_t* log = malloc(sizeof(*log));
  memcpy(log,&proto, sizeof(proto));

  // XXX hack; we call things that call into this object during init!
  stasis_log_file = log;

  buffer = malloc(BUFSIZE);
  
  if(!buffer) { return 0; /*LLADD_NO_MEM;*/ }

  /* The file is opened twice for a reason.  fseek() seems to call
     fflush() under Linux, which normally would be a minor problem.
     However, we open the log with O_SYNC, so the fflush() results in
     synchronous disk writes.  Therefore, all read accesses (and
     therefore all seeks) run through the second descriptor.  */

  int logFD;
  logFD = open(log_filename, log_filemode, log_fileperm);
  if(logFD == -1) {
    perror("Couldn't open log file for append.\n");
    abort();
  }
  logFILE = fdopen(logFD, "w+");

  if (logFILE==NULL) {
    perror("Couldn't open log file");
    abort();
    return 0; //LLADD_IO_ERROR; 
  }

  /* Increase the length of log's buffer, since it's in O_SYNC mode. */
  setbuffer(logFILE, buffer, BUFSIZE);

  /* fread() doesn't notice when another handle writes to its file,
     even if fflush() is used to push the changes out to disk.
     Therefore, we use a file descriptor, and read() instead of a FILE
     and fread(). */
  roLogFD = open(log_filename, O_RDONLY, 0);

  if(roLogFD == -1) {
    perror("Couldn't open log file for reads.\n");
  }

  /* Initialize locks. */

  flushedLSN_lock = initlock();
  pthread_mutex_init(&log_read_mutex, NULL);
  pthread_mutex_init(&log_write_mutex, NULL);
  pthread_mutex_init(&nextAvailableLSN_mutex, NULL);
  pthread_mutex_init(&truncateLog_mutex, NULL);

  flushedLSN_wal = 0;
  flushedLSN_commit = 0;
  flushedLSN_internal = 0;
  /*
    Seek append only log to the end of the file.  This is unnecessary,
    since the file was opened in append only mode, but it returns the
    length of the file.
  */

  if (myFseek(logFILE, 0, SEEK_END)==0) {
    /*if file is empty, write an LSN at the 0th position.  LSN 0 is
      invalid, and this prevents us from using it.  Also, the LSN at
      this position is used after log truncation to store the 
      global offset for the truncated log.
    */
    global_offset = 0;
    size_t nmemb = fwrite(&global_offset, sizeof(lsn_t), 1, logFILE);
    if(nmemb != 1) {
      perror("Couldn't start new log file!");
      return 0; //LLADD_IO_ERROR;
    }
  } else {

    off_t newPosition = lseek(roLogFD, 0, SEEK_SET);
    if(newPosition == -1) {
      perror("Could not seek to head of log");
      return 0; //LLADD_IO_ERROR;
    }
    
    ssize_t bytesRead = read(roLogFD, &global_offset, sizeof(lsn_t));
    
    if(bytesRead != sizeof(lsn_t)) {
      printf("Could not read log header.");
      return 0;//LLADD_IO_ERROR;
    }

  }

  // Initialize nextAvailableLSN.

  const LogEntry * le;
  
  nextAvailableLSN =  sizeof(lsn_t) + global_offset;

  unsigned int crc = 0;
  
  if(lseek(roLogFD, sizeof(lsn_t), SEEK_SET) != sizeof(lsn_t)) { 
    perror("Couldn't seek to first log entry!");
  }

  // Using readLogEntry() bypasses checks to see if we're past the end
  // of the log.
  while((le = readLogEntry())) {   
    if(le->type == INTERNALLOG) { 
      if (!(le->prevLSN) || (crc == (unsigned int) le->prevLSN)) { 
	nextAvailableLSN = nextEntry(le);
	crc = 0;
      } else { 
	printf("Log corruption: %x != %x (lsn = %lld)\n", (unsigned int) le->prevLSN, crc, le->LSN);
	// The log wasn't successfully forced to this point; discard
	// everything after the last CRC.
	freeLogEntry(le);
	break;
      }
    } else { 
      update_log_crc(le, &crc);
    }
    freeLogEntry(le);
  }
  
  if(ftruncate(fileno(logFILE), nextAvailableLSN-global_offset) == -1) { 
    perror("Couldn't discard junk at end of log");
  }

  // If there was trailing garbage at the end of the log, overwrite
  // it.
  if(myFseek(logFILE, nextAvailableLSN-global_offset, SEEK_SET) != nextAvailableLSN-global_offset) { 
    perror("Error repositioning log");
    abort();
  }

  // Reset log_crc to zero (nextAvailableLSN immediately follows a crc
  // entry).

  flushedLSN_wal      = nextAvailableLSN;
  flushedLSN_commit   = nextAvailableLSN;
  flushedLSN_internal = nextAvailableLSN;
  log_crc = 0;

  return log;
}


/** 
    @internal 

    Unfortunately, this function can't just seek to the end of the
    log.  If it did, and a prior instance of Stasis crashed (and wrote
    a partial entry), then the log would be corrupted.  Therefore, we
    need to be a little bit smarter, and track the next LSN value
    manually.  Calculating it the first time would require a scan over
    the entire log, so we use the following optimization:

    Every time readLSN is called, we check to see if it is called with
    the highest LSN that we've seen so far.  (If writeLogEntry has not
    been called yet.)

    The first time writeLogEntry is called, we seek from the highest
    LSN encountered so far to the end of the log.

*/

static int writeLogEntryUnlocked(stasis_log_t* log, LogEntry * e) {

  const lsn_t size = sizeofLogEntry(e);
  
  /* Set the log entry's LSN. */
  pthread_mutex_lock(&nextAvailableLSN_mutex);
  e->LSN = nextAvailableLSN;
  pthread_mutex_unlock(&nextAvailableLSN_mutex);

  update_log_crc(e, &log_crc);

  //printf("Writing Log entry type = %d lsn = %ld, size = %ld\n", e->type, e->LSN, size);

  //  off_t current_offset = ftell(log);
  //  assert(e->LSN == (current_offset + global_offset));
  //  off_t oldOffset = ftell(log);

  size_t nmemb = fwrite(&size, sizeof(lsn_t), 1, logFILE);

  if(nmemb != 1) {
    if(feof(logFILE))   { abort();  /* feof makes no sense here */  }
    if(ferror(logFILE)) {
      fprintf(stderr, "writeLog couldn't write next log entry: %d\n",
              ferror(logFILE));
      abort();
    }
    abort();
    // XXX nextAvailableLSN not set...
    return LLADD_IO_ERROR;
  }
  //  off_t newOffset = ftell(log);
  //  assert(nmemb == 1);
  //  assert(oldOffset + sizeof(lsn_t) == newOffset);

  //  current_offset = ftell(log);
  //  assert(e->LSN == (current_offset + global_offset - sizeof(lsn_t)));
  
  nmemb = fwrite(e, size, 1, logFILE);

  //  current_offset = ftell(log);
  //  assert(e->LSN == current_offset + global_offset - sizeof(lsn_t) - size);
  
  if(nmemb != 1) {
    if(feof(logFILE)) { abort();  /* feof makes no sense here */ }
    if(ferror(logFILE)) {
      fprintf(stderr, "writeLog couldn't write next log entry: %d\n", ferror(logFILE));
      abort();
    }
    abort();
    // XXX nextAvailableLSN not set...
    return LLADD_IO_ERROR;
  }

  pthread_mutex_lock(&nextAvailableLSN_mutex);
  assert(nextAvailableLSN == e->LSN);
  nextAvailableLSN = nextEntry(e);
  pthread_mutex_unlock(&nextAvailableLSN_mutex);

  return 0;
}

static int writeLogEntry_LogWriter(stasis_log_t* log, LogEntry * e) { 
  pthread_mutex_lock(&log_write_mutex);
  int ret = writeLogEntryUnlocked(log, e);
  pthread_mutex_unlock(&log_write_mutex);
  return ret;
}

static lsn_t sizeofInternalLogEntry_LogWriter(stasis_log_t * log,
                                              const LogEntry * e) {
  return sizeof(struct __raw_log_entry);
}

static void syncLogInternal() {
  lsn_t newFlushedLSN;

  pthread_mutex_lock(&nextAvailableLSN_mutex);
  newFlushedLSN = nextAvailableLSN;
  if(newFlushedLSN > flushedLSN_internal) {
    pthread_mutex_unlock(&nextAvailableLSN_mutex);
    fflush(logFILE);
    writelock(flushedLSN_lock, 0);
  }
  if(newFlushedLSN > flushedLSN_internal) {
    flushedLSN_internal = newFlushedLSN;
  }
  unlock(flushedLSN_lock);

}

static void syncLog_LogWriter(stasis_log_t * log,
                              stasis_log_force_mode_t mode) {
  lsn_t newFlushedLSN;

  pthread_mutex_lock(&log_write_mutex);

  pthread_mutex_lock(&nextAvailableLSN_mutex);
  newFlushedLSN = nextAvailableLSN;
  pthread_mutex_unlock(&nextAvailableLSN_mutex);

  LogEntry * crc_entry = allocCommonLogEntry(log_crc, -1, INTERNALLOG);
  writeLogEntryUnlocked(log, crc_entry);
  free(crc_entry);
  // Reset log_crc to zero each time a crc entry is written.
  log_crc = 0;

  pthread_mutex_unlock(&log_write_mutex);

  fflush(logFILE);
  // If we opened the logfile with O_SYNC, fflush() is sufficient.
  // Otherwise, we're running in soft commit mode and need to manually force
  // the log before allowing page writeback.
  if(log_softcommit && mode == LOG_FORCE_WAL) {
    fsync(fileno(logFILE));
  }

  // update flushedLSN after fflush returns.
  writelock(flushedLSN_lock, 0);
  if((!log_softcommit) || mode == LOG_FORCE_WAL) {
    if(newFlushedLSN > flushedLSN_wal) {
      flushedLSN_wal = newFlushedLSN;
    }
  }
  if(newFlushedLSN > flushedLSN_commit) {
    flushedLSN_commit = newFlushedLSN;
  }
  if(newFlushedLSN > flushedLSN_internal) {
    flushedLSN_internal = newFlushedLSN;
  }

  writeunlock(flushedLSN_lock);
}

static lsn_t flushedLSN_LogWriter(stasis_log_t* log,
                                  stasis_log_force_mode_t mode) {
  readlock(flushedLSN_lock, 0);
  lsn_t ret;
  if(mode == LOG_FORCE_COMMIT) {
    ret = flushedLSN_commit;
  } else if(mode == LOG_FORCE_WAL) {
    ret = flushedLSN_wal;
  } else {
    abort();
  }
  readunlock(flushedLSN_lock);
  return ret;
}
static lsn_t flushedLSNInternal() {
  readlock(flushedLSN_lock, 0);
  lsn_t ret = flushedLSN_internal;
  readunlock(flushedLSN_lock);
  return ret; 
}

static int close_LogWriter(stasis_log_t* log) {
  /* Get the whole thing to the disk before closing it. */
  syncLog_LogWriter(log, LOG_FORCE_WAL);

  fclose(logFILE);
  close(roLogFD);
  logFILE = NULL;
  roLogFD = 0;

  flushedLSN_wal = 0;
  flushedLSN_commit = 0;
  flushedLSN_internal = 0;
  nextAvailableLSN = 0;
  global_offset = 0;

  /* Free locks. */

  deletelock(flushedLSN_lock);
  pthread_mutex_destroy(&log_read_mutex);
  pthread_mutex_destroy(&log_write_mutex);
  pthread_mutex_destroy(&nextAvailableLSN_mutex);
  pthread_mutex_destroy(&truncateLog_mutex);
  free(buffer);
  buffer = 0;
  log_crc = 0;
  free(log_filename);
  free(log_scratch_filename);
  free(log);
  return 0;
}

void deleteLogWriter() {
  remove(log_filename);
}

static LogEntry * readLogEntry() {
  LogEntry * ret = 0;
  lsn_t size;

  lsn_t bytesRead = read(roLogFD, &size, sizeof(lsn_t));

  if(bytesRead != sizeof(lsn_t)) { 
    if(bytesRead == 0) {
      return NULL;
    } else if(bytesRead == -1) {
      perror("error reading log");
      abort();
      return (LogEntry*)LLADD_IO_ERROR;
    } else { 
      lsn_t newSize = size - bytesRead;
      lsn_t newBytesRead = read (roLogFD, ((byte*)&size)+bytesRead, newSize);

      fprintf(stdout, "Trying to piece together short read.\n"); fflush(stderr);

      if(newBytesRead == 0) { 
	return NULL;
      }
      fprintf(stderr, "short read from log.  Expected %lld bytes, got %lld.\nFIXME: This is 'normal', but currently not handled", (long long) sizeof(lsn_t), (long long) bytesRead);
      fflush(stderr);
      fprintf(stderr, "\nattempt to read again produced newBytesRead = %lld, newSize was %lld\n", newBytesRead, newSize);
      fflush(stderr);

      abort();  // XXX really abort here.  This code should attempt to piece together short log reads...
    }
  }
  
  if(!size) { 
    return NULL;
  }
  ret = malloc(size);

  bytesRead = read(roLogFD, ret, size);

  if(bytesRead != size) {
    if(bytesRead == 0) {
      fprintf(stderr, "eof reading entry\n");
      fflush(stderr);
      free(ret);
      return(NULL);
    } else if(bytesRead == -1) {
      perror("error reading log");
      abort();
      return (LogEntry*)LLADD_IO_ERROR;
    } else { 
      lsn_t newSize = size - bytesRead;
      lsn_t newBytesRead = read (roLogFD, ((byte*)ret)+bytesRead, newSize);

      if(newBytesRead == 0) {
	free(ret);
	return NULL;
      }

      fprintf(stdout, "Trying to piece together short log entry.\n"); fflush(stderr);

      fprintf(stderr, "short read from log w/ lsn %lld.  Expected %lld bytes, got %lld.\nFIXME: This is 'normal', but currently not handled", debug_lsn, size, bytesRead);
      fprintf(stderr, "\nattempt to read again produced newBytesRead = %lld, newSize was %lld\n", newBytesRead, newSize);
      fflush(stderr);

      abort();
      return (LogEntry*)LLADD_IO_ERROR;
    }
  }

  // Would like to do this, but we could reading a partial log entry.
  //assert(sizeofLogEntry(ret) == size);

  return ret;
}

//static lsn_t lastPosition_readLSNEntry = -1;
const LogEntry * readLSNEntry_LogWriter(stasis_log_t * log, const lsn_t LSN) {
  LogEntry * ret;

  pthread_mutex_lock(&nextAvailableLSN_mutex);

  if(LSN >= nextAvailableLSN) { 
    pthread_mutex_unlock(&nextAvailableLSN_mutex);
    return 0;
  } 
  pthread_mutex_unlock(&nextAvailableLSN_mutex); 

  pthread_mutex_lock(&log_read_mutex);

  /** Because we use two file descriptors to access the log, we need
      to flush the log write buffer before concluding we're at EOF. */
  if(flushedLSNInternal() <= LSN) { // && LSN < nextAvailableLSN) {
    syncLogInternal();
    assert(flushedLSNInternal() > LSN); // @todo move up into if() 
  }

  assert(global_offset <= LSN);

  debug_lsn = LSN;
  
  off_t newPosition = LSN - global_offset;
  newPosition = lseek(roLogFD, newPosition, SEEK_SET);

  if(newPosition == -1) {
    perror("Could not seek for log read");
    abort();
  }
  assert(newPosition == LSN-global_offset);

  ret = readLogEntry();

  assert(ret);

  pthread_mutex_unlock(&log_read_mutex);

  return ret;
  
}
/**
   Truncates the log file.  In the single-threaded case, this works as
   follows:

   First, the LSN passed to this function, minus sizeof(lsn_t) is
   written to a new file, called logfile.txt~.  (If logfile.txt~
   already exists, then it is truncated.)

   Next, the contents of the log, starting with the LSN passed into
   this function are copied to logfile.txt~

   Finally, logfile.txt~ is moved on top of logfile.txt

   As long as the move system call is atomic, this function should
   maintain the system's durability.

   The multithreaded case is a bit more complicated, as we need
   to deal with latching:

   With no lock, copy the log.  Upon completion, if the log has grown,
   then copy the part that remains.  Next, obtain a read/write latch
   on the logfile, and copy any remaining portions of the log.
   Perform the move, and release the latch.

*/
int truncateLog_LogWriter(stasis_log_t* log, lsn_t LSN) {
  FILE *tmpLog;

  const LogEntry * le;
  LogHandle* lh;

  lsn_t size;

  pthread_mutex_lock(&truncateLog_mutex);

  if(global_offset + sizeof(lsn_t) >= LSN) {
    /* Another thread beat us to it...the log is already truncated
       past the point requested, so just return. */
    pthread_mutex_unlock(&truncateLog_mutex);
    return 0;
  }

  /* w+ = truncate, and open for writing. */
  tmpLog = fopen(log_scratch_filename, "w+");  

  if (tmpLog==NULL) {
    pthread_mutex_unlock(&truncateLog_mutex);
    perror("logTruncate() couldn't create scratch log file!");
    abort();
    return LLADD_IO_ERROR;
  }

  /* Need to write LSN - sizeof(lsn_t) to make room for the offset in
     the file.  If we truncate to lsn 10, we'll put lsn 10 in position
     4, so the file offset is 6. */
  LSN -= sizeof(lsn_t);  

  myFwrite(&LSN, sizeof(lsn_t), tmpLog);
  
  LSN += sizeof(lsn_t);

  /**
     @todo truncateLog blocks writers too early.  Instead, read until EOF, then
     lock, and then finish the truncate.
  */
  pthread_mutex_lock(&log_write_mutex);

  fflush(logFILE);

  lh = getLSNHandle(log, LSN);
  lsn_t lengthOfCopiedLog = 0;
  int firstInternalEntry = 1;
  lsn_t nextLSN = 0;
  while((le = nextInLog(lh))) {
    size = sizeofLogEntry(le);
    if(nextLSN) { 
      assert(nextLSN == le->LSN);
    } 
    nextLSN = nextEntry_LogWriter(log, le);

    if(firstInternalEntry && le->type == INTERNALLOG) { 
      LogEntry * firstCRC = malloc(size);
      memcpy(firstCRC, le, size);
      freeLogEntry(le);
      firstCRC->prevLSN = 0;
      le = firstCRC;
    }

    lengthOfCopiedLog += (size + sizeof(lsn_t));

    myFwrite(&size, sizeof(lsn_t), tmpLog);
    myFwrite(le, size, tmpLog);
    if(firstInternalEntry && le->type == INTERNALLOG) { 
      free((void*)le); // remove const qualifier + free
      firstInternalEntry = 0;
    } else { 
      freeLogEntry(le);
    }
  }
  freeLogHandle(lh);
  LogEntry * crc_entry = allocCommonLogEntry(0, -1, INTERNALLOG);
  assert(crc_entry->prevLSN == 0);

  pthread_mutex_lock(&nextAvailableLSN_mutex);
  crc_entry->LSN = nextAvailableLSN;
  //  printf("Crc entry: lsn = %ld, crc = %x\n", crc_entry->LSN, crc_entry->prevLSN);

  assert(nextAvailableLSN == LSN + lengthOfCopiedLog);

  size = sizeofLogEntry(crc_entry);

  nextAvailableLSN = nextEntry_LogWriter(log, crc_entry);

  log_crc = 0;

  pthread_mutex_unlock(&nextAvailableLSN_mutex);

  myFwrite(&size, sizeof(lsn_t), tmpLog);
  myFwrite(crc_entry, size, tmpLog);
  lengthOfCopiedLog += (size + sizeof(lsn_t));

  assert(nextAvailableLSN == (LSN + lengthOfCopiedLog));
  free(crc_entry);

  fflush(tmpLog);
#ifdef HAVE_FDATASYNC
  fdatasync(fileno(tmpLog));
#else
  fsync(fileno(tmpLog));
#endif

  /** Time to shut out the readers */


  pthread_mutex_lock(&log_read_mutex);

  /* closeLogWriter calls sync, and does some extra stuff that we don't want, so we
     basicly re-implement closeLogWriter and openLogWriter here...
  */
  fclose(logFILE);
  close(roLogFD);
  fclose(tmpLog);

  if(rename(log_scratch_filename, log_filename)) {
    pthread_mutex_unlock(&log_read_mutex);
    pthread_mutex_unlock(&log_write_mutex);
    pthread_mutex_unlock(&truncateLog_mutex);

    perror("Error replacing old log file with new log file");
    return LLADD_IO_ERROR;
  } else {
    //    printf("Truncation complete.\n");
    fflush(stdout);
  }

  int logFD = open(log_filename, log_filemode, log_fileperm);

  if(logFD == -1) {
    perror("Couldn't open log file for append.\n");
    abort();
  }
  logFILE = fdopen(logFD, "w+");

  if (logFILE==NULL) {
    pthread_mutex_unlock(&log_read_mutex);
    pthread_mutex_unlock(&log_write_mutex);
    pthread_mutex_unlock(&truncateLog_mutex);

    perror("Couldn't reopen log after truncate");
    abort();
    return LLADD_IO_ERROR;
  }
  
  setbuffer(logFILE, buffer, BUFSIZE);
  
  global_offset = LSN - sizeof(lsn_t);

  lsn_t logPos;
  if((logPos = myFseek(logFILE, 0, SEEK_END)) != nextAvailableLSN - global_offset) { 
    if(logPos == -1) { 
      perror("Truncation couldn't seek");
    } else { 
      printf("logfile was wrong length after truncation.  Expected %lld, found %lld\n", nextAvailableLSN - global_offset, logPos);
      fflush(stdout);
      abort();
    }
  }

  roLogFD = open(log_filename, O_RDONLY, 0);

  if(roLogFD == -1) {
    perror("Couldn't open log file for reads.\n");
    abort();
    return LLADD_IO_ERROR;
  }

  pthread_mutex_unlock(&log_read_mutex);
  pthread_mutex_unlock(&log_write_mutex);
  pthread_mutex_unlock(&truncateLog_mutex);

  return 0;

}

lsn_t firstLogEntry_LogWriter(stasis_log_t* log) {
  assert(logFILE);
  pthread_mutex_lock(&log_read_mutex); // for global offset...
  lsn_t ret = global_offset + sizeof(lsn_t);
  pthread_mutex_unlock(&log_read_mutex);
  return ret;
}
