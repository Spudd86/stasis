#include <config.h>
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>

#include <stasis/common.h>
#include <stasis/io/handle.h>
#include <pthread.h>

/**
   @file

   Implementation of a file-backed io handle.  This implementation uses
   pread() and pwrite() to access the handle.

   The functions defined here implement interfaces documented in handle.h

   @see handle.h
*/

typedef struct pfile_impl {
  pthread_mutex_t mut;
  lsn_t start_pos;
  lsn_t end_pos;
  int fd;
  int file_flags;
  int file_mode;
  char * filename;
} pfile_impl;

static int pfile_num_copies(stasis_handle_t *h) { return 0; }
static int pfile_num_copies_buffer(stasis_handle_t *h) { return 0; }
static int pfile_close(stasis_handle_t *h) {

  pfile_impl * impl = (pfile_impl*)h->impl;
  DEBUG("Closing pfile: end = %lld\n", impl->end_pos);
  int fd = impl->fd;
  free((void*)impl->filename);
  free(impl);
  free(h);
  int ret = close(fd);
  if(!ret) return 0;
  else     return errno;
}
static lsn_t pfile_start_position(stasis_handle_t *h) {
  pfile_impl * impl = (pfile_impl*)h->impl;
  return impl->start_pos;
}
static lsn_t pfile_end_position(stasis_handle_t *h) {
  pfile_impl * impl = (pfile_impl*)h->impl;
  pthread_mutex_lock(&impl->mut);
  lsn_t ret = impl->end_pos;
  pthread_mutex_unlock(&impl->mut);
  return ret;
}
inline static int pfile_write_unlocked(int fd, lsn_t off, const byte * dat,
                                       lsn_t len) {

  int error = 0;

  ssize_t bytes_written = 0;

  while(bytes_written < len) {

    ssize_t count = pwrite(fd,
                           dat + bytes_written,
                           len - bytes_written,
                           off + bytes_written);

    if(count == -1) {
      if(errno == EAGAIN || errno == EINTR) {
        // @see file.c for an explanation; basically; we ignore these,
        // and try again.
        count = 0;
      } else {
        if(errno == EBADF) {
          error = EBADF;
        } else {
          error = errno;
        }
        break;
      }
    }
    bytes_written += count;
    if(bytes_written != len) {
      DEBUG("pwrite spinning\n");
    }
  }
  return error;
}
static int pfile_read(stasis_handle_t * h, lsn_t off, byte * buf, lsn_t len) {
  pfile_impl * impl = (pfile_impl*)(h->impl);
  int error = 0;

  if(off < impl->start_pos) {
    error = EDOM;
  } else if(off + len > impl->end_pos) {
    error = EDOM;
  } else {
    ssize_t bytes_read = 0;
    while(bytes_read < len) {

      ssize_t count = pread(impl->fd,
                            buf + bytes_read,
                            len - bytes_read,
                            off + bytes_read - impl->start_pos);
      if(count == -1) {
        if(errno == EAGAIN || errno == EINTR) {
          count = 0;
        } else {
          if(errno == EBADF) {
            h->error = EBADF;
          } else {
            abort(); // XXX other errors?
          }
          error = errno;
          break;
        }
      }
      bytes_read += count;
      if(bytes_read != len) {
        DEBUG("pread spinning\n");
      }
    }
    assert(bytes_read == len);
  }

  return error;
}
static int pfile_write(stasis_handle_t *h, lsn_t off, const byte *dat, lsn_t len) {
  pfile_impl *impl = (pfile_impl*)(h->impl);
  int error = 0;
  lsn_t phys_off;
  if(impl->start_pos > off) {
    error = EDOM;
  } else {
    pthread_mutex_lock(&impl->mut);
    if(impl->end_pos < off+len) {
      impl->end_pos = off+len;
    }
    pthread_mutex_unlock(&impl->mut);
    phys_off = off - impl->start_pos;
    error = pfile_write_unlocked(impl->fd, phys_off, dat, len);
  }
  return error;
}
static int pfile_append(stasis_handle_t *h, lsn_t *off, const byte *dat, lsn_t len) {
  pfile_impl *impl = (pfile_impl*)(h->impl);
  pthread_mutex_lock(&impl->mut);
  *off = impl->end_pos;
  impl->end_pos += len;
  pthread_mutex_unlock(&impl->mut);
  lsn_t phys_off = *off - impl->start_pos;
  int error = pfile_write_unlocked(impl->fd, phys_off, dat,len);
  return error;
}
static stasis_write_buffer_t * pfile_write_buffer(stasis_handle_t * h,
                                                 lsn_t off, lsn_t len) {

  stasis_write_buffer_t * ret = malloc(sizeof(stasis_write_buffer_t));

  if(!ret) {
    h->error = ENOMEM;
    return NULL;
  }

  pfile_impl * impl = (pfile_impl*)h->impl;

  int error = 0;

  if(impl->start_pos > off) {
    error = EDOM;
  }

  pthread_mutex_lock(&impl->mut);

  if(off + len > impl->end_pos) {
    impl->end_pos = off+len;
  }
  pthread_mutex_unlock(&impl->mut);

  byte * buf;
  if(!error) {
    buf = malloc(len);
    if(!buf) { error = ENOMEM; }
  }
  if(error) {
    ret->h = h;
    ret->off = 0;
    ret->buf = 0;
    ret->len = 0;
    ret->impl = 0;
    ret->error = error;
  } else {
    ret->h = h;
    ret->off = off;
    ret->buf = buf;
    ret->len = len;
    ret->impl = 0;
    ret->error = 0;
  }
  return ret;
}
static stasis_write_buffer_t *pfile_append_buffer(stasis_handle_t * h,
						 lsn_t len) {

   // Allocate the handle
  stasis_write_buffer_t * ret = malloc(sizeof(stasis_write_buffer_t));
  if(!ret) { return NULL; }

  pfile_impl * impl = (pfile_impl*)h->impl;

  // Obtain an appropriate offset
  pthread_mutex_lock(&(impl->mut));

  off_t off = impl->end_pos;
  impl->end_pos += len;
  pthread_mutex_unlock(&(impl->mut));

  // Allocate the buffer
  byte * buf = malloc(len);
  int error = 0;
  if(!buf) {
    error = ENOMEM;
  }

  if(error) {
    ret->h = h;
    ret->off = 0;
    ret->buf = 0;
    ret->len = 0;
    ret->impl = 0;
    ret->error = error;
  } else {
    ret->h = h;
    ret->off = off;
    ret->buf = buf;
    ret->len = len;
    ret->impl = 0;
    ret->error = 0;
  }
  return ret;

}
static int pfile_release_write_buffer(stasis_write_buffer_t * w) {

  pfile_impl * impl = (pfile_impl*)(w->h->impl);

  pthread_mutex_lock(&(impl->mut));
  int error = 0;
  if(impl->end_pos < w->off + w->len ||
     impl->start_pos > w->off) {
    error = EDOM;
  }
  pthread_mutex_unlock(&(impl->mut));

  if(!error) {
    error = pfile_write_unlocked(impl->fd, w->off-impl->start_pos, w->buf, w->len);
  }
  free(w->buf);
  free(w);
  return error;
}

static stasis_read_buffer_t * pfile_read_buffer(stasis_handle_t * h,
					      lsn_t off, lsn_t len) {
  stasis_read_buffer_t * ret = malloc(sizeof(stasis_read_buffer_t));
  if(!ret) { return NULL; }

  byte * buf = malloc(len);
  int error = 0;

  if(!buf) { error = ENOMEM; }

  if(!error) {
    error = pfile_read(h, off, buf, len);
  }

  if(error) {
    ret->h = h;
    ret->buf = 0;
    ret->off = 0;
    ret->len = 0;
    ret->impl = 0;
    ret->error = error;
    if(buf) { free(buf); }
  } else {
    ret->h = h;
    ret->buf = buf;
    ret->off = off;
    ret->len = len;
    ret->impl = 0;
    ret->error = 0;
  }
  return ret;
}
static int pfile_release_read_buffer(stasis_read_buffer_t * r) {
  free((void*)r->buf);
  free(r);
  return 0;
}
static int pfile_truncate_start(stasis_handle_t * h, lsn_t new_start) {
  static int truncate_warned = 0;
  if(!truncate_warned) {
    printf("\nWarning: pfile doesn't support truncation; ignoring truncation request\n");
    truncate_warned = 1;
  }
  return 0;
}
struct stasis_handle_t pfile_func = {
  .num_copies = pfile_num_copies,
  .num_copies_buffer = pfile_num_copies_buffer,
  .close = pfile_close,
  .start_position = pfile_start_position,
  .end_position = pfile_end_position,
  .write = pfile_write,
  .append = pfile_append,
  .write_buffer = pfile_write_buffer,
  .append_buffer = pfile_append_buffer,
  .release_write_buffer = pfile_release_write_buffer,
  .read = pfile_read,
  .read_buffer = pfile_read_buffer,
  .release_read_buffer = pfile_release_read_buffer,
  .truncate_start = pfile_truncate_start,
  .error = 0
};
stasis_handle_t * stasis_handle(open_pfile)(lsn_t start_offset, char * filename, int flags, int mode) {
  stasis_handle_t * ret = malloc(sizeof(stasis_handle_t));
  if(!ret) { return NULL; }
  *ret = pfile_func;

  pfile_impl * impl = malloc(sizeof(pfile_impl));
  ret->impl = impl;
  pthread_mutex_init(&(impl->mut), 0);
  impl->fd = open(filename, flags, mode);
  assert(sizeof(off_t) >= (64/8));
  if(impl->fd == -1) {
    ret->error = errno;
  }

  impl->start_pos = start_offset;

  off_t file_len = lseek(impl->fd,0,SEEK_END);
  if(file_len == (off_t)-1) {
    ret->error = errno;
  }
  impl->end_pos = file_len + start_offset;
  DEBUG("file len = %lld, start_off = %lld, end = %lld\n", file_len, start_offset, impl->end_pos);

  impl->filename = strdup(filename);
  impl->file_flags = flags;
  impl->file_mode = mode;
  assert(!ret->error);
  return ret;
}
