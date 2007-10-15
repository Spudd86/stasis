#include <limits.h>

#ifndef _ROSE_COMPRESSION_COMPRESSION_H__
#define _ROSE_COMPRESSION_COMPRESSION_H__

namespace rose {

typedef int8_t record_size_t;
typedef uint16_t byte_off_t;
typedef uint32_t slot_index_t;
typedef uint8_t plugin_id_t;
typedef uint8_t column_number_t;
typedef uint16_t column_offset_t;

static const record_size_t VARIABLE_SIZE =  CHAR_MAX;
static const slot_index_t  NOSPACE       =  UINT_MAX;
static const slot_index_t  EXCEPTIONAL   =  UINT_MAX-1;
static const slot_index_t  MAX_INDEX     =  UINT_MAX-2;

/**
   This function computes a page type (an integer stored in the page header)
   so that Stasis can dispatch calls to the appropriate page implemenation.

   Most page types choose a single constant, but pstar's page layout varies
   across different template instantiations.  In particular, the page layout
   depends on sizeof(TYPE), and upon COMPRESOR.  Finally, pstar and the
   compressors behave differently depending on whether or not TYPE is signed
   or unsigned.  (Non integer types are currently not supported.)

   Right now, everything happens to be a power of two and the page
   type is of this form:

   BASE_PAGEID + 00PCSII(base2)

   P stores the page format PAGE_FORMAT_ID
   C stores the compressor PLUGIN_ID.
   S is 1 iff the type is signed
   II is 00, 01, 10, 11 depending on the sizeof(type)

   Although the on disk representation is bigger; stasis tries to keep page
   types within the range 0 - 255.

*/
template <class PAGEFORMAT, class COMPRESSOR, class TYPE>
plugin_id_t plugin_id() {
  /* type_idx maps from sizeof(TYPE) to a portion of a page type:

     (u)int8_t  -> 0
     (u)int16_t -> 1
     (u)int32_t -> 2
     (u)int64_t -> 3

  */

  // Number of bytes in type --->      1   2       4               8
  static const int type_idx[] = { -1,  0,  1, -1,  2, -1, -1, -1,  3 };
  static const int idx_count = 4;
  static const TYPE is_signed = 0 - 1;

  //  assert(sizeof(TYPE) <= 8 && type_idx[sizeof(TYPE)] >= 0);

  // XXX first '2' hardcodes the number of COMPRESSOR implementations...

  plugin_id_t ret = USER_DEFINED_PAGE(0)
      // II         S    C
      + idx_count * 2 *  2 * PAGEFORMAT::PAGE_FORMAT_ID
      + idx_count * 2 *  COMPRESSOR::PLUGIN_ID
      + idx_count * (is_signed < 0)
      + type_idx[sizeof(TYPE)];

  return ret;
}

}


#endif  // _ROSE_COMPRESSION_COMPRESSION_H__
