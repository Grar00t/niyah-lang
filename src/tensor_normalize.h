#ifndef NIYAH_TENSOR_NORMALIZE_H
#define NIYAH_TENSOR_NORMALIZE_H
#include "lexer.h"
/* NFKC, then Alif 0622/0623/0625/0671 -> 0627, then Unicode mark removal.
 * Raw spans remain provenance. Normalized lexical keys are not root identities.
 * Source tokens are bounded to 128 UTF-8 bytes; failure leaves output untouched. */
int tensor_normalize(const u8 *source, u32 length, u8 *output,
                     u32 capacity, u32 *output_length);
const char *tensor_unicode_version(void);
/* Additional Arabic spellings accepted only by the integrated core lexer. */
int tensor_arabic_letter(u32 cp);
int tensor_arabic_mark(u32 cp);
#endif
