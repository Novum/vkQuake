#include "miniz.h"
/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

typedef unsigned char mz_validate_uint16[sizeof(mz_uint16) == 2 ? 1 : -1];
typedef unsigned char mz_validate_uint32[sizeof(mz_uint32) == 4 ? 1 : -1];
typedef unsigned char mz_validate_uint64[sizeof(mz_uint64) == 8 ? 1 : -1];

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS
/* Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/ */
#if 0
    static mz_ulong mz_crc32(mz_ulong crc, const mz_uint8 *ptr, size_t buf_len)
    {
        static const mz_uint32 s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
                                               0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
        mz_uint32 crcu32 = (mz_uint32)crc;
        if (!ptr)
            return MZ_CRC32_INIT;
        crcu32 = ~crcu32;
        while (buf_len--)
        {
            mz_uint8 b = *ptr++;
            crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)];
            crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)];
        }
        return ~crcu32;
    }
#elif defined(USE_EXTERNAL_MZCRC)
/* If USE_EXTERNAL_CRC is defined, an external module will export the
 * mz_crc32() symbol for us to use, e.g. an SSE-accelerated version.
 * Depending on the impl, it may be necessary to ~ the input/output crc values.
 */
mz_ulong mz_crc32(mz_ulong crc, const mz_uint8 *ptr, size_t buf_len);
#else
/* Faster, but larger CPU cache footprint.
 */
static mz_ulong mz_crc32(mz_ulong crc, const mz_uint8 *ptr, size_t buf_len)
{
    static const mz_uint32 s_crc_table[256] =
        {
          0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535,
          0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD,
          0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D,
          0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
          0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4,
          0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
          0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC,
          0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
          0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB,
          0xB6662D3D, 0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F,
          0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB,
          0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
          0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA,
          0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE,
          0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A,
          0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
          0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409,
          0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
          0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739,
          0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
          0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1, 0xF00F9344, 0x8708A3D2, 0x1E01F268,
          0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0,
          0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8,
          0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
          0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF,
          0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703,
          0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7,
          0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
          0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE,
          0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
          0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6,
          0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
          0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D,
          0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5,
          0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605,
          0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
          0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
        };

    mz_uint32 crc32 = (mz_uint32)crc ^ 0xFFFFFFFF;
    const mz_uint8 *pByte_buf = (const mz_uint8 *)ptr;

    while (buf_len >= 4)
    {
        crc32 = (crc32 >> 8) ^ s_crc_table[(crc32 ^ pByte_buf[0]) & 0xFF];
        crc32 = (crc32 >> 8) ^ s_crc_table[(crc32 ^ pByte_buf[1]) & 0xFF];
        crc32 = (crc32 >> 8) ^ s_crc_table[(crc32 ^ pByte_buf[2]) & 0xFF];
        crc32 = (crc32 >> 8) ^ s_crc_table[(crc32 ^ pByte_buf[3]) & 0xFF];
        pByte_buf += 4;
        buf_len -= 4;
    }

    while (buf_len)
    {
        crc32 = (crc32 >> 8) ^ s_crc_table[(crc32 ^ pByte_buf[0]) & 0xFF];
        ++pByte_buf;
        --buf_len;
    }

    return ~crc32;
}
#endif
#endif /* MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS */

static /*MINIZ_EXPORT*/ void *miniz_def_alloc_func(void *opaque, size_t items, size_t size)
{
    (void)opaque, (void)items, (void)size;
    return MZ_MALLOC(items * size);
}
static /*MINIZ_EXPORT*/ void miniz_def_free_func(void *opaque, void *address)
{
    (void)opaque, (void)address;
    MZ_FREE(address);
}
static /*MINIZ_EXPORT*/ void *miniz_def_realloc_func(void *opaque, void *address, size_t items, size_t size)
{
    (void)opaque, (void)address, (void)items, (void)size;
    return MZ_REALLOC(address, items * size);
}

#ifdef __cplusplus
}
#endif

/*
  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org/>
*/

#ifndef MINIZ_NO_INFLATE_APIS

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------- Low-level Decompression (completely independent from all compression API's) */

#define TINFL_MEMCPY(d, s, l) memcpy(d, s, l)
#define TINFL_MEMSET(p, c, l) memset(p, c, l)

#define TINFL_CR_BEGIN  \
    switch (r->m_state) \
    {                   \
        case 0:
#define TINFL_CR_RETURN(state_index, result) \
    do                                       \
    {                                        \
        status = result;                     \
        r->m_state = state_index;            \
        goto common_exit;                    \
        case state_index:;                   \
    }                                        \
    MZ_MACRO_END
#define TINFL_CR_RETURN_FOREVER(state_index, result) \
    do                                               \
    {                                                \
        for (;;)                                     \
        {                                            \
            TINFL_CR_RETURN(state_index, result);    \
        }                                            \
    }                                                \
    MZ_MACRO_END
#define TINFL_CR_FINISH }

#define TINFL_GET_BYTE(state_index, c)                                                                                                                           \
    do                                                                                                                                                           \
    {                                                                                                                                                            \
        while (pIn_buf_cur >= pIn_buf_end)                                                                                                                       \
        {                                                                                                                                                        \
            TINFL_CR_RETURN(state_index, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS); \
        }                                                                                                                                                        \
        c = *pIn_buf_cur++;                                                                                                                                      \
    }                                                                                                                                                            \
    MZ_MACRO_END

#define TINFL_NEED_BITS(state_index, n)                \
    do                                                 \
    {                                                  \
        mz_uint c;                                     \
        TINFL_GET_BYTE(state_index, c);                \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); \
        num_bits += 8;                                 \
    } while (num_bits < (mz_uint)(n))
#define TINFL_SKIP_BITS(state_index, n)      \
    do                                       \
    {                                        \
        if (num_bits < (mz_uint)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    MZ_MACRO_END
#define TINFL_GET_BITS(state_index, b, n)    \
    do                                       \
    {                                        \
        if (num_bits < (mz_uint)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        b = bit_buf & ((1 << (n)) - 1);      \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    MZ_MACRO_END

/* TINFL_HUFF_BITBUF_FILL() is only used rarely, when the number of bytes remaining in the input buffer falls below 2. */
/* It reads just enough bytes from the input stream that are needed to decode the next Huffman code (and absolutely no more). It works by trying to fully decode a */
/* Huffman code by using whatever bits are currently present in the bit buffer. If this fails, it reads another byte, and tries again until it succeeds or until the */
/* bit buffer contains >=15 bits (deflate's max. Huffman code size). */
#define TINFL_HUFF_BITBUF_FILL(state_index, pHuff)                             \
    do                                                                         \
    {                                                                          \
        temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];     \
        if (temp >= 0)                                                         \
        {                                                                      \
            code_len = temp >> 9;                                              \
            if ((code_len) && (num_bits >= code_len))                          \
                break;                                                         \
        }                                                                      \
        else if (num_bits > TINFL_FAST_LOOKUP_BITS)                            \
        {                                                                      \
            code_len = TINFL_FAST_LOOKUP_BITS;                                 \
            do                                                                 \
            {                                                                  \
                temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; \
            } while ((temp < 0) && (num_bits >= (code_len + 1)));              \
            if (temp >= 0)                                                     \
                break;                                                         \
        }                                                                      \
        TINFL_GET_BYTE(state_index, c);                                        \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits);                         \
        num_bits += 8;                                                         \
    } while (num_bits < 15);

/* TINFL_HUFF_DECODE() decodes the next Huffman coded symbol. It's more complex than you would initially expect because the zlib API expects the decompressor to never read */
/* beyond the final byte of the deflate stream. (In other words, when this macro wants to read another byte from the input, it REALLY needs another byte in order to fully */
/* decode the next Huffman code.) Handling this properly is particularly important on raw deflate (non-zlib) streams, which aren't followed by a byte aligned adler-32. */
/* The slow path is only executed at the very end of the input buffer. */
/* v1.16: The original macro handled the case at the very end of the passed-in input buffer, but we also need to handle the case where the user passes in 1+zillion bytes */
/* following the deflate data and our non-conservative read-ahead path won't kick in here on this code. This is much trickier. */
#define TINFL_HUFF_DECODE(state_index, sym, pHuff)                                                                                  \
    do                                                                                                                              \
    {                                                                                                                               \
        int temp;                                                                                                                   \
        mz_uint code_len, c;                                                                                                        \
        if (num_bits < 15)                                                                                                          \
        {                                                                                                                           \
            if ((pIn_buf_end - pIn_buf_cur) < 2)                                                                                    \
            {                                                                                                                       \
                TINFL_HUFF_BITBUF_FILL(state_index, pHuff);                                                                         \
            }                                                                                                                       \
            else                                                                                                                    \
            {                                                                                                                       \
                bit_buf |= (((tinfl_bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((tinfl_bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); \
                pIn_buf_cur += 2;                                                                                                   \
                num_bits += 16;                                                                                                     \
            }                                                                                                                       \
        }                                                                                                                           \
        if ((temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)                                               \
            code_len = temp >> 9, temp &= 511;                                                                                      \
        else                                                                                                                        \
        {                                                                                                                           \
            code_len = TINFL_FAST_LOOKUP_BITS;                                                                                      \
            do                                                                                                                      \
            {                                                                                                                       \
                temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)];                                                      \
            } while (temp < 0);                                                                                                     \
        }                                                                                                                           \
        sym = temp;                                                                                                                 \
        bit_buf >>= code_len;                                                                                                       \
        num_bits -= code_len;                                                                                                       \
    }                                                                                                                               \
    MZ_MACRO_END

tinfl_status tinfl_decompress(tinfl_decompressor *r, const mz_uint8 *pIn_buf_next, size_t *pIn_buf_size, mz_uint8 *pOut_buf_start, mz_uint8 *pOut_buf_next, size_t *pOut_buf_size, const mz_uint32 decomp_flags)
{
    static const int s_length_base[31] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0 };
    static const int s_length_extra[31] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0 };
    static const int s_dist_base[32] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0 };
    static const int s_dist_extra[32] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static const mz_uint8 s_length_dezigzag[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    static const int s_min_table_sizes[3] = { 257, 1, 4 };

    tinfl_status status = TINFL_STATUS_FAILED;
    mz_uint32 num_bits, dist, counter, num_extra;
    tinfl_bit_buf_t bit_buf;
    const mz_uint8 *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size;
    mz_uint8 *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next + *pOut_buf_size;
    size_t out_buf_size_mask = (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ? (size_t)-1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1, dist_from_out_buf_start;

    /* Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter). */
    if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start))
    {
        *pIn_buf_size = *pOut_buf_size = 0;
        return TINFL_STATUS_BAD_PARAM;
    }

    num_bits = r->m_num_bits;
    bit_buf = r->m_bit_buf;
    dist = r->m_dist;
    counter = r->m_counter;
    num_extra = r->m_num_extra;
    dist_from_out_buf_start = r->m_dist_from_out_buf_start;
    TINFL_CR_BEGIN

    bit_buf = num_bits = dist = counter = num_extra = r->m_zhdr0 = r->m_zhdr1 = 0;
    r->m_z_adler32 = r->m_check_adler32 = 1;
    if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
    {
        TINFL_GET_BYTE(1, r->m_zhdr0);
        TINFL_GET_BYTE(2, r->m_zhdr1);
        counter = (((r->m_zhdr0 * 256 + r->m_zhdr1) % 31 != 0) || (r->m_zhdr1 & 32) || ((r->m_zhdr0 & 15) != 8));
        if (!(decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
            counter |= (((1U << (8U + (r->m_zhdr0 >> 4))) > 32768U) || ((out_buf_size_mask + 1) < (size_t)(1U << (8U + (r->m_zhdr0 >> 4)))));
        if (counter)
        {
            TINFL_CR_RETURN_FOREVER(36, TINFL_STATUS_FAILED);
        }
    }

    do
    {
        TINFL_GET_BITS(3, r->m_final, 3);
        r->m_type = r->m_final >> 1;
        if (r->m_type == 0)
        {
            TINFL_SKIP_BITS(5, num_bits & 7);
            for (counter = 0; counter < 4; ++counter)
            {
                if (num_bits)
                    TINFL_GET_BITS(6, r->m_raw_header[counter], 8);
                else
                    TINFL_GET_BYTE(7, r->m_raw_header[counter]);
            }
            if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (mz_uint)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8))))
            {
                TINFL_CR_RETURN_FOREVER(39, TINFL_STATUS_FAILED);
            }
            while ((counter) && (num_bits))
            {
                TINFL_GET_BITS(51, dist, 8);
                while (pOut_buf_cur >= pOut_buf_end)
                {
                    TINFL_CR_RETURN(52, TINFL_STATUS_HAS_MORE_OUTPUT);
                }
                *pOut_buf_cur++ = (mz_uint8)dist;
                counter--;
            }
            while (counter)
            {
                size_t n;
                while (pOut_buf_cur >= pOut_buf_end)
                {
                    TINFL_CR_RETURN(9, TINFL_STATUS_HAS_MORE_OUTPUT);
                }
                while (pIn_buf_cur >= pIn_buf_end)
                {
                    TINFL_CR_RETURN(38, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS);
                }
                n = MZ_MIN(MZ_MIN((size_t)(pOut_buf_end - pOut_buf_cur), (size_t)(pIn_buf_end - pIn_buf_cur)), counter);
                TINFL_MEMCPY(pOut_buf_cur, pIn_buf_cur, n);
                pIn_buf_cur += n;
                pOut_buf_cur += n;
                counter -= (mz_uint)n;
            }
        }
        else if (r->m_type == 3)
        {
            TINFL_CR_RETURN_FOREVER(10, TINFL_STATUS_FAILED);
        }
        else
        {
            if (r->m_type == 1)
            {
                mz_uint8 *p = r->m_tables[0].m_code_size;
                mz_uint i;
                r->m_table_sizes[0] = 288;
                r->m_table_sizes[1] = 32;
                TINFL_MEMSET(r->m_tables[1].m_code_size, 5, 32);
                for (i = 0; i <= 143; ++i)
                    *p++ = 8;
                for (; i <= 255; ++i)
                    *p++ = 9;
                for (; i <= 279; ++i)
                    *p++ = 7;
                for (; i <= 287; ++i)
                    *p++ = 8;
            }
            else
            {
                for (counter = 0; counter < 3; counter++)
                {
                    TINFL_GET_BITS(11, r->m_table_sizes[counter], "\05\05\04"[counter]);
                    r->m_table_sizes[counter] += s_min_table_sizes[counter];
                }
                MZ_CLEAR_ARR(r->m_tables[2].m_code_size);
                for (counter = 0; counter < r->m_table_sizes[2]; counter++)
                {
                    mz_uint s;
                    TINFL_GET_BITS(14, s, 3);
                    r->m_tables[2].m_code_size[s_length_dezigzag[counter]] = (mz_uint8)s;
                }
                r->m_table_sizes[2] = 19;
            }
            for (; (int)r->m_type >= 0; r->m_type--)
            {
                int tree_next, tree_cur;
                tinfl_huff_table *pTable;
                mz_uint i, j, used_syms, total, sym_index, next_code[17], total_syms[16];
                pTable = &r->m_tables[r->m_type];
                MZ_CLEAR_ARR(total_syms);
                MZ_CLEAR_ARR(pTable->m_look_up);
                MZ_CLEAR_ARR(pTable->m_tree);
                for (i = 0; i < r->m_table_sizes[r->m_type]; ++i)
                    total_syms[pTable->m_code_size[i]]++;
                used_syms = 0, total = 0;
                next_code[0] = next_code[1] = 0;
                for (i = 1; i <= 15; ++i)
                {
                    used_syms += total_syms[i];
                    next_code[i + 1] = (total = ((total + total_syms[i]) << 1));
                }
                if ((65536 != total) && (used_syms > 1))
                {
                    TINFL_CR_RETURN_FOREVER(35, TINFL_STATUS_FAILED);
                }
                for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index)
                {
                    mz_uint rev_code = 0, l, cur_code, code_size = pTable->m_code_size[sym_index];
                    if (!code_size)
                        continue;
                    cur_code = next_code[code_size]++;
                    for (l = code_size; l > 0; l--, cur_code >>= 1)
                        rev_code = (rev_code << 1) | (cur_code & 1);
                    if (code_size <= TINFL_FAST_LOOKUP_BITS)
                    {
                        mz_int16 k = (mz_int16)((code_size << 9) | sym_index);
                        while (rev_code < TINFL_FAST_LOOKUP_SIZE)
                        {
                            pTable->m_look_up[rev_code] = k;
                            rev_code += (1 << code_size);
                        }
                        continue;
                    }
                    if (0 == (tree_cur = pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)]))
                    {
                        pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)] = (mz_int16)tree_next;
                        tree_cur = tree_next;
                        tree_next -= 2;
                    }
                    rev_code >>= (TINFL_FAST_LOOKUP_BITS - 1);
                    for (j = code_size; j > (TINFL_FAST_LOOKUP_BITS + 1); j--)
                    {
                        tree_cur -= ((rev_code >>= 1) & 1);
                        if (!pTable->m_tree[-tree_cur - 1])
                        {
                            pTable->m_tree[-tree_cur - 1] = (mz_int16)tree_next;
                            tree_cur = tree_next;
                            tree_next -= 2;
                        }
                        else
                            tree_cur = pTable->m_tree[-tree_cur - 1];
                    }
                    tree_cur -= ((rev_code >>= 1) & 1);
                    pTable->m_tree[-tree_cur - 1] = (mz_int16)sym_index;
                }
                if (r->m_type == 2)
                {
                    for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]);)
                    {
                        mz_uint s;
                        TINFL_HUFF_DECODE(16, dist, &r->m_tables[2]);
                        if (dist < 16)
                        {
                            r->m_len_codes[counter++] = (mz_uint8)dist;
                            continue;
                        }
                        if ((dist == 16) && (!counter))
                        {
                            TINFL_CR_RETURN_FOREVER(17, TINFL_STATUS_FAILED);
                        }
                        num_extra = "\02\03\07"[dist - 16];
                        TINFL_GET_BITS(18, s, num_extra);
                        s += "\03\03\013"[dist - 16];
                        TINFL_MEMSET(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s);
                        counter += s;
                    }
                    if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter)
                    {
                        TINFL_CR_RETURN_FOREVER(21, TINFL_STATUS_FAILED);
                    }
                    TINFL_MEMCPY(r->m_tables[0].m_code_size, r->m_len_codes, r->m_table_sizes[0]);
                    TINFL_MEMCPY(r->m_tables[1].m_code_size, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
                }
            }
            for (;;)
            {
                mz_uint8 *pSrc;
                for (;;)
                {
                    if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2))
                    {
                        TINFL_HUFF_DECODE(23, counter, &r->m_tables[0]);
                        if (counter >= 256)
                            break;
                        while (pOut_buf_cur >= pOut_buf_end)
                        {
                            TINFL_CR_RETURN(24, TINFL_STATUS_HAS_MORE_OUTPUT);
                        }
                        *pOut_buf_cur++ = (mz_uint8)counter;
                    }
                    else
                    {
                        int sym2;
                        mz_uint code_len;
#if TINFL_USE_64BIT_BITBUF
                        if (num_bits < 30)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE32(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 4;
                            num_bits += 32;
                        }
#else
                        if (num_bits < 15)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 2;
                            num_bits += 16;
                        }
#endif
                        if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                            code_len = sym2 >> 9;
                        else
                        {
                            code_len = TINFL_FAST_LOOKUP_BITS;
                            do
                            {
                                sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)];
                            } while (sym2 < 0);
                        }
                        counter = sym2;
                        bit_buf >>= code_len;
                        num_bits -= code_len;
                        if (counter & 256)
                            break;

#if !TINFL_USE_64BIT_BITBUF
                        if (num_bits < 15)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)MZ_READ_LE16(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 2;
                            num_bits += 16;
                        }
#endif
                        if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                            code_len = sym2 >> 9;
                        else
                        {
                            code_len = TINFL_FAST_LOOKUP_BITS;
                            do
                            {
                                sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)];
                            } while (sym2 < 0);
                        }
                        bit_buf >>= code_len;
                        num_bits -= code_len;

                        pOut_buf_cur[0] = (mz_uint8)counter;
                        if (sym2 & 256)
                        {
                            pOut_buf_cur++;
                            counter = sym2;
                            break;
                        }
                        pOut_buf_cur[1] = (mz_uint8)sym2;
                        pOut_buf_cur += 2;
                    }
                }
                if ((counter &= 511) == 256)
                    break;

                num_extra = s_length_extra[counter - 257];
                counter = s_length_base[counter - 257];
                if (num_extra)
                {
                    mz_uint extra_bits;
                    TINFL_GET_BITS(25, extra_bits, num_extra);
                    counter += extra_bits;
                }

                TINFL_HUFF_DECODE(26, dist, &r->m_tables[1]);
                num_extra = s_dist_extra[dist];
                dist = s_dist_base[dist];
                if (num_extra)
                {
                    mz_uint extra_bits;
                    TINFL_GET_BITS(27, extra_bits, num_extra);
                    dist += extra_bits;
                }

                dist_from_out_buf_start = pOut_buf_cur - pOut_buf_start;
                if ((dist == 0 || dist > dist_from_out_buf_start || dist_from_out_buf_start == 0) && (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
                {
                    TINFL_CR_RETURN_FOREVER(37, TINFL_STATUS_FAILED);
                }

                pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

                if ((MZ_MAX(pOut_buf_cur, pSrc) + counter) > pOut_buf_end)
                {
                    while (counter--)
                    {
                        while (pOut_buf_cur >= pOut_buf_end)
                        {
                            TINFL_CR_RETURN(53, TINFL_STATUS_HAS_MORE_OUTPUT);
                        }
                        *pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
                    }
                    continue;
                }
#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
                else if ((counter >= 9) && (counter <= dist))
                {
                    const mz_uint8 *pSrc_end = pSrc + (counter & ~7);
                    do
                    {
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
						memcpy(pOut_buf_cur, pSrc, sizeof(mz_uint32)*2);
#else
                        ((mz_uint32 *)pOut_buf_cur)[0] = ((const mz_uint32 *)pSrc)[0];
                        ((mz_uint32 *)pOut_buf_cur)[1] = ((const mz_uint32 *)pSrc)[1];
#endif
                        pOut_buf_cur += 8;
                    } while ((pSrc += 8) < pSrc_end);
                    if ((counter &= 7) < 3)
                    {
                        if (counter)
                        {
                            pOut_buf_cur[0] = pSrc[0];
                            if (counter > 1)
                                pOut_buf_cur[1] = pSrc[1];
                            pOut_buf_cur += counter;
                        }
                        continue;
                    }
                }
#endif
                while(counter>2)
                {
                    pOut_buf_cur[0] = pSrc[0];
                    pOut_buf_cur[1] = pSrc[1];
                    pOut_buf_cur[2] = pSrc[2];
                    pOut_buf_cur += 3;
                    pSrc += 3;
					counter -= 3;
                }
                if (counter > 0)
                {
                    pOut_buf_cur[0] = pSrc[0];
                    if (counter > 1)
                        pOut_buf_cur[1] = pSrc[1];
                    pOut_buf_cur += counter;
                }
            }
        }
    } while (!(r->m_final & 1));

    /* Ensure byte alignment and put back any bytes from the bitbuf if we've looked ahead too far on gzip, or other Deflate streams followed by arbitrary data. */
    /* I'm being super conservative here. A number of simplifications can be made to the byte alignment part, and the Adler32 check shouldn't ever need to worry about reading from the bitbuf now. */
    TINFL_SKIP_BITS(32, num_bits & 7);
    while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
    {
        --pIn_buf_cur;
        num_bits -= 8;
    }
    bit_buf &= (tinfl_bit_buf_t)((((mz_uint64)1) << num_bits) - (mz_uint64)1);
    MZ_ASSERT(!num_bits); /* if this assert fires then we've read beyond the end of non-deflate/zlib streams with following data (such as gzip streams). */

    if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
    {
        for (counter = 0; counter < 4; ++counter)
        {
            mz_uint s;
            if (num_bits)
                TINFL_GET_BITS(41, s, 8);
            else
                TINFL_GET_BYTE(42, s);
            r->m_z_adler32 = (r->m_z_adler32 << 8) | s;
        }
    }
    TINFL_CR_RETURN_FOREVER(34, TINFL_STATUS_DONE);

    TINFL_CR_FINISH

common_exit:
    /* As long as we aren't telling the caller that we NEED more input to make forward progress: */
    /* Put back any bytes from the bitbuf in case we've looked ahead too far on gzip, or other Deflate streams followed by arbitrary data. */
    /* We need to be very careful here to NOT push back any bytes we definitely know we need to make forward progress, though, or we'll lock the caller up into an inf loop. */
    if ((status != TINFL_STATUS_NEEDS_MORE_INPUT) && (status != TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS))
    {
        while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
        {
            --pIn_buf_cur;
            num_bits -= 8;
        }
    }
    r->m_num_bits = num_bits;
    r->m_bit_buf = bit_buf & (tinfl_bit_buf_t)((((mz_uint64)1) << num_bits) - (mz_uint64)1);
    r->m_dist = dist;
    r->m_counter = counter;
    r->m_num_extra = num_extra;
    r->m_dist_from_out_buf_start = dist_from_out_buf_start;
    *pIn_buf_size = pIn_buf_cur - pIn_buf_next;
    *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
    if ((decomp_flags & (TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32)) && (status >= 0))
    {
        const mz_uint8 *ptr = pOut_buf_next;
        size_t buf_len = *pOut_buf_size;
        mz_uint32 i, s1 = r->m_check_adler32 & 0xffff, s2 = r->m_check_adler32 >> 16;
        size_t block_len = buf_len % 5552;
        while (buf_len)
        {
            for (i = 0; i + 7 < block_len; i += 8, ptr += 8)
            {
                s1 += ptr[0], s2 += s1;
                s1 += ptr[1], s2 += s1;
                s1 += ptr[2], s2 += s1;
                s1 += ptr[3], s2 += s1;
                s1 += ptr[4], s2 += s1;
                s1 += ptr[5], s2 += s1;
                s1 += ptr[6], s2 += s1;
                s1 += ptr[7], s2 += s1;
            }
            for (; i < block_len; ++i)
                s1 += *ptr++, s2 += s1;
            s1 %= 65521U, s2 %= 65521U;
            buf_len -= block_len;
            block_len = 5552;
        }
        r->m_check_adler32 = (s2 << 16) + s1;
        if ((status == TINFL_STATUS_DONE) && (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER) && (r->m_check_adler32 != r->m_z_adler32))
            status = TINFL_STATUS_ADLER32_MISMATCH;
    }
    return status;
}

#ifdef __cplusplus
}
#endif

#endif /*#ifndef MINIZ_NO_INFLATE_APIS*/
 /**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
 * Copyright 2016 Martin Raiber
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/


#ifndef MINIZ_NO_ARCHIVE_APIS

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------- .ZIP archive reading */

#define MZ_TOLOWER(c) ((((c) >= 'A') && ((c) <= 'Z')) ? ((c) - 'A' + 'a') : (c))

/* Various ZIP archive enums. To completely avoid cross platform compiler alignment and platform endian issues, miniz.c doesn't use structs for any of this stuff. */
enum
{
    /* ZIP archive identifiers and record sizes */
    MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG = 0x06054b50,
    MZ_ZIP_CENTRAL_DIR_HEADER_SIG = 0x02014b50,
    MZ_ZIP_LOCAL_DIR_HEADER_SIG = 0x04034b50,
    MZ_ZIP_LOCAL_DIR_HEADER_SIZE = 30,
    MZ_ZIP_CENTRAL_DIR_HEADER_SIZE = 46,
    MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE = 22,

    /* ZIP64 archive identifier and record sizes */
    MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIG = 0x06064b50,
    MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIG = 0x07064b50,
    MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE = 56,
    MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE = 20,
    MZ_ZIP64_EXTENDED_INFORMATION_FIELD_HEADER_ID = 0x0001,
    MZ_ZIP_DATA_DESCRIPTOR_ID = 0x08074b50,
    MZ_ZIP_DATA_DESCRIPTER_SIZE64 = 24,
    MZ_ZIP_DATA_DESCRIPTER_SIZE32 = 16,

    /* Central directory header record offsets */
    MZ_ZIP_CDH_SIG_OFS = 0,
    MZ_ZIP_CDH_VERSION_MADE_BY_OFS = 4,
    MZ_ZIP_CDH_VERSION_NEEDED_OFS = 6,
    MZ_ZIP_CDH_BIT_FLAG_OFS = 8,
    MZ_ZIP_CDH_METHOD_OFS = 10,
    MZ_ZIP_CDH_FILE_TIME_OFS = 12,
    MZ_ZIP_CDH_FILE_DATE_OFS = 14,
    MZ_ZIP_CDH_CRC32_OFS = 16,
    MZ_ZIP_CDH_COMPRESSED_SIZE_OFS = 20,
    MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS = 24,
    MZ_ZIP_CDH_FILENAME_LEN_OFS = 28,
    MZ_ZIP_CDH_EXTRA_LEN_OFS = 30,
    MZ_ZIP_CDH_COMMENT_LEN_OFS = 32,
    MZ_ZIP_CDH_DISK_START_OFS = 34,
    MZ_ZIP_CDH_INTERNAL_ATTR_OFS = 36,
    MZ_ZIP_CDH_EXTERNAL_ATTR_OFS = 38,
    MZ_ZIP_CDH_LOCAL_HEADER_OFS = 42,

    /* Local directory header offsets */
    MZ_ZIP_LDH_SIG_OFS = 0,
    MZ_ZIP_LDH_VERSION_NEEDED_OFS = 4,
    MZ_ZIP_LDH_BIT_FLAG_OFS = 6,
    MZ_ZIP_LDH_METHOD_OFS = 8,
    MZ_ZIP_LDH_FILE_TIME_OFS = 10,
    MZ_ZIP_LDH_FILE_DATE_OFS = 12,
    MZ_ZIP_LDH_CRC32_OFS = 14,
    MZ_ZIP_LDH_COMPRESSED_SIZE_OFS = 18,
    MZ_ZIP_LDH_DECOMPRESSED_SIZE_OFS = 22,
    MZ_ZIP_LDH_FILENAME_LEN_OFS = 26,
    MZ_ZIP_LDH_EXTRA_LEN_OFS = 28,
    MZ_ZIP_LDH_BIT_FLAG_HAS_LOCATOR = 1 << 3,

    /* End of central directory offsets */
    MZ_ZIP_ECDH_SIG_OFS = 0,
    MZ_ZIP_ECDH_NUM_THIS_DISK_OFS = 4,
    MZ_ZIP_ECDH_NUM_DISK_CDIR_OFS = 6,
    MZ_ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS = 8,
    MZ_ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS = 10,
    MZ_ZIP_ECDH_CDIR_SIZE_OFS = 12,
    MZ_ZIP_ECDH_CDIR_OFS_OFS = 16,
    MZ_ZIP_ECDH_COMMENT_SIZE_OFS = 20,

    /* ZIP64 End of central directory locator offsets */
    MZ_ZIP64_ECDL_SIG_OFS = 0,                    /* 4 bytes */
    MZ_ZIP64_ECDL_NUM_DISK_CDIR_OFS = 4,          /* 4 bytes */
    MZ_ZIP64_ECDL_REL_OFS_TO_ZIP64_ECDR_OFS = 8,  /* 8 bytes */
    MZ_ZIP64_ECDL_TOTAL_NUMBER_OF_DISKS_OFS = 16, /* 4 bytes */

    /* ZIP64 End of central directory header offsets */
    MZ_ZIP64_ECDH_SIG_OFS = 0,                       /* 4 bytes */
    MZ_ZIP64_ECDH_SIZE_OF_RECORD_OFS = 4,            /* 8 bytes */
    MZ_ZIP64_ECDH_VERSION_MADE_BY_OFS = 12,          /* 2 bytes */
    MZ_ZIP64_ECDH_VERSION_NEEDED_OFS = 14,           /* 2 bytes */
    MZ_ZIP64_ECDH_NUM_THIS_DISK_OFS = 16,            /* 4 bytes */
    MZ_ZIP64_ECDH_NUM_DISK_CDIR_OFS = 20,            /* 4 bytes */
    MZ_ZIP64_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS = 24, /* 8 bytes */
    MZ_ZIP64_ECDH_CDIR_TOTAL_ENTRIES_OFS = 32,       /* 8 bytes */
    MZ_ZIP64_ECDH_CDIR_SIZE_OFS = 40,                /* 8 bytes */
    MZ_ZIP64_ECDH_CDIR_OFS_OFS = 48,                 /* 8 bytes */
    MZ_ZIP_VERSION_MADE_BY_DOS_FILESYSTEM_ID = 0,
    MZ_ZIP_DOS_DIR_ATTRIBUTE_BITFLAG = 0x10,
    MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_IS_ENCRYPTED = 1,
    MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_COMPRESSED_PATCH_FLAG = 32,
    MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_USES_STRONG_ENCRYPTION = 64,
    MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_LOCAL_DIR_IS_MASKED = 8192,
    MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_UTF8 = 1 << 11
};

typedef struct
{
    void *m_p;
    size_t m_size, m_capacity;
    mz_uint m_element_size;
} mz_zip_array;

struct mz_zip_internal_state_tag
{
    mz_zip_array m_central_dir;
    mz_zip_array m_central_dir_offsets;
    mz_zip_array m_sorted_central_dir_offsets;

    /* The flags passed in when the archive is initially opened. */
    uint32_t m_init_flags;

    /* MZ_TRUE if the archive has a zip64 end of central directory headers, etc. */
    mz_bool m_zip64;

    /* MZ_TRUE if we found zip64 extended info in the central directory (m_zip64 will also be slammed to true too, even if we didn't find a zip64 end of central dir header, etc.) */
    mz_bool m_zip64_has_extended_info_fields;

    void *m_pMem;
};

#define MZ_ZIP_ARRAY_SET_ELEMENT_SIZE(array_ptr, element_size) (array_ptr)->m_element_size = element_size

#if defined(DEBUG) || defined(_DEBUG)
static MZ_FORCEINLINE mz_uint mz_zip_array_range_check(const mz_zip_array *pArray, mz_uint index)
{
    MZ_ASSERT(index < pArray->m_size);
    return index;
}
#define MZ_ZIP_ARRAY_ELEMENT(array_ptr, element_type, index) ((element_type *)((array_ptr)->m_p))[mz_zip_array_range_check(array_ptr, index)]
#else
#define MZ_ZIP_ARRAY_ELEMENT(array_ptr, element_type, index) ((element_type *)((array_ptr)->m_p))[index]
#endif

static MZ_FORCEINLINE void mz_zip_array_clear(mz_zip_archive *pZip, mz_zip_array *pArray)
{
    pZip->m_pFree(pZip->m_pAlloc_opaque, pArray->m_p);
    memset(pArray, 0, sizeof(mz_zip_array));
}

static mz_bool mz_zip_array_ensure_capacity(mz_zip_archive *pZip, mz_zip_array *pArray, size_t min_new_capacity, mz_uint growing)
{
    void *pNew_p;
    size_t new_capacity = min_new_capacity;
    MZ_ASSERT(pArray->m_element_size);
    if (pArray->m_capacity >= min_new_capacity)
        return MZ_TRUE;
    if (growing)
    {
        new_capacity = MZ_MAX(1, pArray->m_capacity);
        while (new_capacity < min_new_capacity)
            new_capacity *= 2;
    }
    if (NULL == (pNew_p = pZip->m_pRealloc(pZip->m_pAlloc_opaque, pArray->m_p, pArray->m_element_size, new_capacity)))
        return MZ_FALSE;
    pArray->m_p = pNew_p;
    pArray->m_capacity = new_capacity;
    return MZ_TRUE;
}

static MZ_FORCEINLINE mz_bool mz_zip_array_resize(mz_zip_archive *pZip, mz_zip_array *pArray, size_t new_size, mz_uint growing)
{
    if (new_size > pArray->m_capacity)
    {
        if (!mz_zip_array_ensure_capacity(pZip, pArray, new_size, growing))
            return MZ_FALSE;
    }
    pArray->m_size = new_size;
    return MZ_TRUE;
}

static MZ_FORCEINLINE mz_bool mz_zip_set_error(mz_zip_archive *pZip, mz_zip_error err_num)
{
    if (pZip)
        pZip->m_last_error = err_num;
    return MZ_FALSE;
}

static mz_bool mz_zip_reader_init_internal(mz_zip_archive *pZip, mz_uint flags)
{
    (void)flags;
    if ((!pZip) || (pZip->m_pState) || (pZip->m_zip_mode != MZ_ZIP_MODE_INVALID))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    if (!pZip->m_pAlloc)
        pZip->m_pAlloc = miniz_def_alloc_func;
    if (!pZip->m_pFree)
        pZip->m_pFree = miniz_def_free_func;
    if (!pZip->m_pRealloc)
        pZip->m_pRealloc = miniz_def_realloc_func;

    pZip->m_archive_size = 0;
    pZip->m_central_directory_file_ofs = 0;
    pZip->m_total_files = 0;
    pZip->m_last_error = MZ_ZIP_NO_ERROR;

    if (NULL == (pZip->m_pState = (mz_zip_internal_state *)pZip->m_pAlloc(pZip->m_pAlloc_opaque, 1, sizeof(mz_zip_internal_state))))
        return mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);

    memset(pZip->m_pState, 0, sizeof(mz_zip_internal_state));
    MZ_ZIP_ARRAY_SET_ELEMENT_SIZE(&pZip->m_pState->m_central_dir, sizeof(mz_uint8));
    MZ_ZIP_ARRAY_SET_ELEMENT_SIZE(&pZip->m_pState->m_central_dir_offsets, sizeof(mz_uint32));
    MZ_ZIP_ARRAY_SET_ELEMENT_SIZE(&pZip->m_pState->m_sorted_central_dir_offsets, sizeof(mz_uint32));
    pZip->m_pState->m_init_flags = flags;
    pZip->m_pState->m_zip64 = MZ_FALSE;
    pZip->m_pState->m_zip64_has_extended_info_fields = MZ_FALSE;

    pZip->m_zip_mode = MZ_ZIP_MODE_READING;

    return MZ_TRUE;
}

static MZ_FORCEINLINE mz_bool mz_zip_reader_filename_less(const mz_zip_array *pCentral_dir_array, const mz_zip_array *pCentral_dir_offsets, mz_uint l_index, mz_uint r_index)
{
    const mz_uint8 *pL = &MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_array, mz_uint8, MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_offsets, mz_uint32, l_index)), *pE;
    const mz_uint8 *pR = &MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_array, mz_uint8, MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_offsets, mz_uint32, r_index));
    mz_uint l_len = MZ_READ_LE16(pL + MZ_ZIP_CDH_FILENAME_LEN_OFS), r_len = MZ_READ_LE16(pR + MZ_ZIP_CDH_FILENAME_LEN_OFS);
    mz_uint8 l = 0, r = 0;
    pL += MZ_ZIP_CENTRAL_DIR_HEADER_SIZE;
    pR += MZ_ZIP_CENTRAL_DIR_HEADER_SIZE;
    pE = pL + MZ_MIN(l_len, r_len);
    while (pL < pE)
    {
        if ((l = MZ_TOLOWER(*pL)) != (r = MZ_TOLOWER(*pR)))
            break;
        pL++;
        pR++;
    }
    return (pL == pE) ? (l_len < r_len) : (l < r);
}

#define MZ_SWAP_UINT32(a, b) \
    do                       \
    {                        \
        mz_uint32 t = a;     \
        a = b;               \
        b = t;               \
    }                        \
    MZ_MACRO_END

/* Heap sort of lowercased filenames, used to help accelerate plain central directory searches by mz_zip_reader_locate_file(). (Could also use qsort(), but it could allocate memory.) */
static void mz_zip_reader_sort_central_dir_offsets_by_filename(mz_zip_archive *pZip)
{
    mz_zip_internal_state *pState = pZip->m_pState;
    const mz_zip_array *pCentral_dir_offsets = &pState->m_central_dir_offsets;
    const mz_zip_array *pCentral_dir = &pState->m_central_dir;
    mz_uint32 *pIndices;
    mz_uint32 start, end;
    const mz_uint32 size = pZip->m_total_files;

    if (size <= 1U)
        return;

    pIndices = &MZ_ZIP_ARRAY_ELEMENT(&pState->m_sorted_central_dir_offsets, mz_uint32, 0);

    start = (size - 2U) >> 1U;
    for (;;)
    {
        mz_uint64 child, root = start;
        for (;;)
        {
            if ((child = (root << 1U) + 1U) >= size)
                break;
            child += (((child + 1U) < size) && (mz_zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[child], pIndices[child + 1U])));
            if (!mz_zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[root], pIndices[child]))
                break;
            MZ_SWAP_UINT32(pIndices[root], pIndices[child]);
            root = child;
        }
        if (!start)
            break;
        start--;
    }

    end = size - 1;
    while (end > 0)
    {
        mz_uint64 child, root = 0;
        MZ_SWAP_UINT32(pIndices[end], pIndices[0]);
        for (;;)
        {
            if ((child = (root << 1U) + 1U) >= end)
                break;
            child += (((child + 1U) < end) && mz_zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[child], pIndices[child + 1U]));
            if (!mz_zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[root], pIndices[child]))
                break;
            MZ_SWAP_UINT32(pIndices[root], pIndices[child]);
            root = child;
        }
        end--;
    }
}

static mz_bool mz_zip_reader_locate_header_sig(mz_zip_archive *pZip, mz_uint32 record_sig, mz_uint32 record_size, mz_int64 *pOfs)
{
    mz_int64 cur_file_ofs;
    mz_uint32 buf_u32[4096 / sizeof(mz_uint32)];
    mz_uint8 *pBuf = (mz_uint8 *)buf_u32;

    /* Basic sanity checks - reject files which are too small */
    if (pZip->m_archive_size < record_size)
        return MZ_FALSE;

    /* Find the record by scanning the file from the end towards the beginning. */
    cur_file_ofs = MZ_MAX((mz_int64)pZip->m_archive_size - (mz_int64)sizeof(buf_u32), 0);
    for (;;)
    {
        int i, n = (int)MZ_MIN(sizeof(buf_u32), pZip->m_archive_size - cur_file_ofs);

        if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pBuf, n) != (mz_uint)n)
            return MZ_FALSE;

        for (i = n - 4; i >= 0; --i)
        {
            mz_uint s = MZ_READ_LE32(pBuf + i);
            if (s == record_sig)
            {
                if ((pZip->m_archive_size - (cur_file_ofs + i)) >= record_size)
                    break;
            }
        }

        if (i >= 0)
        {
            cur_file_ofs += i;
            break;
        }

        /* Give up if we've searched the entire file, or we've gone back "too far" (~64kb) */
        if ((!cur_file_ofs) || ((pZip->m_archive_size - cur_file_ofs) >= (MZ_UINT16_MAX + record_size)))
            return MZ_FALSE;

        cur_file_ofs = MZ_MAX(cur_file_ofs - (sizeof(buf_u32) - 3), 0);
    }

    *pOfs = cur_file_ofs;
    return MZ_TRUE;
}

static mz_bool mz_zip_reader_read_central_dir(mz_zip_archive *pZip, mz_uint flags)
{
    mz_uint cdir_size = 0, cdir_entries_on_this_disk = 0, num_this_disk = 0, cdir_disk_index = 0;
    mz_uint64 cdir_ofs = 0;
    mz_int64 cur_file_ofs = 0;
    const mz_uint8 *p;

    mz_uint32 buf_u32[4096 / sizeof(mz_uint32)];
    mz_uint8 *pBuf = (mz_uint8 *)buf_u32;
    mz_bool sort_central_dir = ((flags & MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY) == 0);
    mz_uint32 zip64_end_of_central_dir_locator_u32[(MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE + sizeof(mz_uint32) - 1) / sizeof(mz_uint32)];
    mz_uint8 *pZip64_locator = (mz_uint8 *)zip64_end_of_central_dir_locator_u32;

    mz_uint32 zip64_end_of_central_dir_header_u32[(MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE + sizeof(mz_uint32) - 1) / sizeof(mz_uint32)];
    mz_uint8 *pZip64_end_of_central_dir = (mz_uint8 *)zip64_end_of_central_dir_header_u32;

    mz_uint64 zip64_end_of_central_dir_ofs = 0;

    /* Basic sanity checks - reject files which are too small, and check the first 4 bytes of the file to make sure a local header is there. */
    if (pZip->m_archive_size < MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
        return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);

    if (!mz_zip_reader_locate_header_sig(pZip, MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG, MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE, &cur_file_ofs))
        return mz_zip_set_error(pZip, MZ_ZIP_FAILED_FINDING_CENTRAL_DIR);

    /* Read and verify the end of central directory record. */
    if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pBuf, MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) != MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
        return mz_zip_set_error(pZip, MZ_ZIP_FILE_READ_FAILED);

    if (MZ_READ_LE32(pBuf + MZ_ZIP_ECDH_SIG_OFS) != MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG)
        return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);

    if (cur_file_ofs >= (MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE + MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE))
    {
        if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs - MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE, pZip64_locator, MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE) == MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIZE)
        {
            if (MZ_READ_LE32(pZip64_locator + MZ_ZIP64_ECDL_SIG_OFS) == MZ_ZIP64_END_OF_CENTRAL_DIR_LOCATOR_SIG)
            {
                zip64_end_of_central_dir_ofs = MZ_READ_LE64(pZip64_locator + MZ_ZIP64_ECDL_REL_OFS_TO_ZIP64_ECDR_OFS);
                if (zip64_end_of_central_dir_ofs > (pZip->m_archive_size - MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE))
                    return mz_zip_set_error(pZip, MZ_ZIP_NOT_AN_ARCHIVE);

                if (pZip->m_pRead(pZip->m_pIO_opaque, zip64_end_of_central_dir_ofs, pZip64_end_of_central_dir, MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE) == MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE)
                {
                    if (MZ_READ_LE32(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_SIG_OFS) == MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIG)
                    {
                        pZip->m_pState->m_zip64 = MZ_TRUE;
                    }
                }
            }
        }
    }

    pZip->m_total_files = MZ_READ_LE16(pBuf + MZ_ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS);
    cdir_entries_on_this_disk = MZ_READ_LE16(pBuf + MZ_ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS);
    num_this_disk = MZ_READ_LE16(pBuf + MZ_ZIP_ECDH_NUM_THIS_DISK_OFS);
    cdir_disk_index = MZ_READ_LE16(pBuf + MZ_ZIP_ECDH_NUM_DISK_CDIR_OFS);
    cdir_size = MZ_READ_LE32(pBuf + MZ_ZIP_ECDH_CDIR_SIZE_OFS);
    cdir_ofs = MZ_READ_LE32(pBuf + MZ_ZIP_ECDH_CDIR_OFS_OFS);

    if (pZip->m_pState->m_zip64)
    {
        mz_uint32 zip64_total_num_of_disks = MZ_READ_LE32(pZip64_locator + MZ_ZIP64_ECDL_TOTAL_NUMBER_OF_DISKS_OFS);
        mz_uint64 zip64_cdir_total_entries = MZ_READ_LE64(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_CDIR_TOTAL_ENTRIES_OFS);
        mz_uint64 zip64_cdir_total_entries_on_this_disk = MZ_READ_LE64(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS);
        mz_uint64 zip64_size_of_end_of_central_dir_record = MZ_READ_LE64(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_SIZE_OF_RECORD_OFS);
        mz_uint64 zip64_size_of_central_directory = MZ_READ_LE64(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_CDIR_SIZE_OFS);

        if (zip64_size_of_end_of_central_dir_record < (MZ_ZIP64_END_OF_CENTRAL_DIR_HEADER_SIZE - 12))
            return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

        if (zip64_total_num_of_disks != 1U)
            return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_MULTIDISK);

        /* Check for miniz's practical limits */
        if (zip64_cdir_total_entries > MZ_UINT32_MAX)
            return mz_zip_set_error(pZip, MZ_ZIP_TOO_MANY_FILES);

        pZip->m_total_files = (mz_uint32)zip64_cdir_total_entries;

        if (zip64_cdir_total_entries_on_this_disk > MZ_UINT32_MAX)
            return mz_zip_set_error(pZip, MZ_ZIP_TOO_MANY_FILES);

        cdir_entries_on_this_disk = (mz_uint32)zip64_cdir_total_entries_on_this_disk;

        /* Check for miniz's current practical limits (sorry, this should be enough for millions of files) */
        if (zip64_size_of_central_directory > MZ_UINT32_MAX)
            return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_CDIR_SIZE);

        cdir_size = (mz_uint32)zip64_size_of_central_directory;

        num_this_disk = MZ_READ_LE32(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_NUM_THIS_DISK_OFS);

        cdir_disk_index = MZ_READ_LE32(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_NUM_DISK_CDIR_OFS);

        cdir_ofs = MZ_READ_LE64(pZip64_end_of_central_dir + MZ_ZIP64_ECDH_CDIR_OFS_OFS);
    }

    if (pZip->m_total_files != cdir_entries_on_this_disk)
        return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_MULTIDISK);

    if (((num_this_disk | cdir_disk_index) != 0) && ((num_this_disk != 1) || (cdir_disk_index != 1)))
        return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_MULTIDISK);

    if (cdir_size < pZip->m_total_files * MZ_ZIP_CENTRAL_DIR_HEADER_SIZE)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

    if ((cdir_ofs + (mz_uint64)cdir_size) > pZip->m_archive_size)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

    pZip->m_central_directory_file_ofs = cdir_ofs;

    if (pZip->m_total_files)
    {
        mz_uint i, n;
        /* Read the entire central directory into a heap block, and allocate another heap block to hold the unsorted central dir file record offsets, and possibly another to hold the sorted indices. */
        if ((!mz_zip_array_resize(pZip, &pZip->m_pState->m_central_dir, cdir_size, MZ_FALSE)) ||
            (!mz_zip_array_resize(pZip, &pZip->m_pState->m_central_dir_offsets, pZip->m_total_files, MZ_FALSE)))
            return mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);

        if (sort_central_dir)
        {
            if (!mz_zip_array_resize(pZip, &pZip->m_pState->m_sorted_central_dir_offsets, pZip->m_total_files, MZ_FALSE))
                return mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);
        }

        if (pZip->m_pRead(pZip->m_pIO_opaque, cdir_ofs, pZip->m_pState->m_central_dir.m_p, cdir_size) != cdir_size)
            return mz_zip_set_error(pZip, MZ_ZIP_FILE_READ_FAILED);

        /* Now create an index into the central directory file records, do some basic sanity checking on each record */
        p = (const mz_uint8 *)pZip->m_pState->m_central_dir.m_p;
        for (n = cdir_size, i = 0; i < pZip->m_total_files; ++i)
        {
            mz_uint total_header_size, disk_index, bit_flags, filename_size, ext_data_size;
            mz_uint64 comp_size, decomp_size, local_header_ofs;

            if ((n < MZ_ZIP_CENTRAL_DIR_HEADER_SIZE) || (MZ_READ_LE32(p) != MZ_ZIP_CENTRAL_DIR_HEADER_SIG))
                return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

            MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir_offsets, mz_uint32, i) = (mz_uint32)(p - (const mz_uint8 *)pZip->m_pState->m_central_dir.m_p);

            if (sort_central_dir)
                MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_sorted_central_dir_offsets, mz_uint32, i) = i;

            comp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_COMPRESSED_SIZE_OFS);
            decomp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS);
            local_header_ofs = MZ_READ_LE32(p + MZ_ZIP_CDH_LOCAL_HEADER_OFS);
            filename_size = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
            ext_data_size = MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS);

            if ((!pZip->m_pState->m_zip64_has_extended_info_fields) &&
                (ext_data_size) &&
                (MZ_MAX(MZ_MAX(comp_size, decomp_size), local_header_ofs) == MZ_UINT32_MAX))
            {
                /* Attempt to find zip64 extended information field in the entry's extra data */
                mz_uint32 extra_size_remaining = ext_data_size;

                if (extra_size_remaining)
                {
					const mz_uint8 *pExtra_data;
					void* buf = NULL;

					if (MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_size + ext_data_size > n)
					{
						buf = MZ_MALLOC(ext_data_size);
						if(buf==NULL)
							return mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);

						if (pZip->m_pRead(pZip->m_pIO_opaque, cdir_ofs + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_size, buf, ext_data_size) != ext_data_size)
						{
							MZ_FREE(buf);
							return mz_zip_set_error(pZip, MZ_ZIP_FILE_READ_FAILED);
						}

						pExtra_data = (mz_uint8*)buf;
					}
					else
					{
						pExtra_data = p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_size;
					}

                    do
                    {
                        mz_uint32 field_id;
                        mz_uint32 field_data_size;

						if (extra_size_remaining < (sizeof(mz_uint16) * 2))
						{
							MZ_FREE(buf);
							return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);
						}

                        field_id = MZ_READ_LE16(pExtra_data);
                        field_data_size = MZ_READ_LE16(pExtra_data + sizeof(mz_uint16));

						if ((field_data_size + sizeof(mz_uint16) * 2) > extra_size_remaining)
						{
							MZ_FREE(buf);
							return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);
						}

                        if (field_id == MZ_ZIP64_EXTENDED_INFORMATION_FIELD_HEADER_ID)
                        {
                            /* Ok, the archive didn't have any zip64 headers but it uses a zip64 extended information field so mark it as zip64 anyway (this can occur with infozip's zip util when it reads compresses files from stdin). */
                            pZip->m_pState->m_zip64 = MZ_TRUE;
                            pZip->m_pState->m_zip64_has_extended_info_fields = MZ_TRUE;
                            break;
                        }

                        pExtra_data += sizeof(mz_uint16) * 2 + field_data_size;
                        extra_size_remaining = extra_size_remaining - sizeof(mz_uint16) * 2 - field_data_size;
                    } while (extra_size_remaining);

					MZ_FREE(buf);
                }
            }

            /* I've seen archives that aren't marked as zip64 that uses zip64 ext data, argh */
            if ((comp_size != MZ_UINT32_MAX) && (decomp_size != MZ_UINT32_MAX))
            {
                if (((!MZ_READ_LE32(p + MZ_ZIP_CDH_METHOD_OFS)) && (decomp_size != comp_size)) || (decomp_size && !comp_size))
                    return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);
            }

            disk_index = MZ_READ_LE16(p + MZ_ZIP_CDH_DISK_START_OFS);
            if ((disk_index == MZ_UINT16_MAX) || ((disk_index != num_this_disk) && (disk_index != 1)))
                return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_MULTIDISK);

            if (comp_size != MZ_UINT32_MAX)
            {
                if (((mz_uint64)MZ_READ_LE32(p + MZ_ZIP_CDH_LOCAL_HEADER_OFS) + MZ_ZIP_LOCAL_DIR_HEADER_SIZE + comp_size) > pZip->m_archive_size)
                    return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);
            }

            bit_flags = MZ_READ_LE16(p + MZ_ZIP_CDH_BIT_FLAG_OFS);
            if (bit_flags & MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_LOCAL_DIR_IS_MASKED)
                return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_ENCRYPTION);

            if ((total_header_size = MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS) + MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS) + MZ_READ_LE16(p + MZ_ZIP_CDH_COMMENT_LEN_OFS)) > n)
                return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

            n -= total_header_size;
            p += total_header_size;
        }
    }

    if (sort_central_dir)
        mz_zip_reader_sort_central_dir_offsets_by_filename(pZip);

    return MZ_TRUE;
}

static mz_bool mz_zip_reader_end_internal(mz_zip_archive *pZip, mz_bool set_last_error)
{
    mz_bool status = MZ_TRUE;

    if (!pZip)
        return MZ_FALSE;

    if ((!pZip->m_pState) || (!pZip->m_pAlloc) || (!pZip->m_pFree) || (pZip->m_zip_mode != MZ_ZIP_MODE_READING))
    {
        if (set_last_error)
            pZip->m_last_error = MZ_ZIP_INVALID_PARAMETER;

        return MZ_FALSE;
    }

    if (pZip->m_pState)
    {
        mz_zip_internal_state *pState = pZip->m_pState;
        pZip->m_pState = NULL;

        mz_zip_array_clear(pZip, &pState->m_central_dir);
        mz_zip_array_clear(pZip, &pState->m_central_dir_offsets);
        mz_zip_array_clear(pZip, &pState->m_sorted_central_dir_offsets);

        pZip->m_pFree(pZip->m_pAlloc_opaque, pState);
    }
    pZip->m_zip_mode = MZ_ZIP_MODE_INVALID;

    return status;
}

mz_bool mz_zip_reader_end(mz_zip_archive *pZip)
{
    return mz_zip_reader_end_internal(pZip, MZ_TRUE);
}
mz_bool mz_zip_reader_init(mz_zip_archive *pZip, mz_uint64 size, mz_uint flags)
{
    if ((!pZip) || (!pZip->m_pRead))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    if (!mz_zip_reader_init_internal(pZip, flags))
        return MZ_FALSE;

    pZip->m_zip_type = MZ_ZIP_TYPE_USER;
    pZip->m_archive_size = size;

    if (!mz_zip_reader_read_central_dir(pZip, flags))
    {
        mz_zip_reader_end_internal(pZip, MZ_FALSE);
        return MZ_FALSE;
    }

    return MZ_TRUE;
}

static MZ_FORCEINLINE const mz_uint8 *mz_zip_get_cdh(mz_zip_archive *pZip, mz_uint file_index)
{
    if ((!pZip) || (!pZip->m_pState) || (file_index >= pZip->m_total_files))
        return NULL;
    return &MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir, mz_uint8, MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir_offsets, mz_uint32, file_index));
}

mz_bool mz_zip_reader_is_file_encrypted(mz_zip_archive *pZip, mz_uint file_index)
{
    mz_uint m_bit_flag;
    const mz_uint8 *p = mz_zip_get_cdh(pZip, file_index);
    if (!p)
    {
        mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);
        return MZ_FALSE;
    }

    m_bit_flag = MZ_READ_LE16(p + MZ_ZIP_CDH_BIT_FLAG_OFS);
    return (m_bit_flag & (MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_IS_ENCRYPTED | MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_USES_STRONG_ENCRYPTION)) != 0;
}

mz_bool mz_zip_reader_is_file_supported(mz_zip_archive *pZip, mz_uint file_index)
{
    mz_uint bit_flag;
    mz_uint method;

    const mz_uint8 *p = mz_zip_get_cdh(pZip, file_index);
    if (!p)
    {
        mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);
        return MZ_FALSE;
    }

    method = MZ_READ_LE16(p + MZ_ZIP_CDH_METHOD_OFS);
    bit_flag = MZ_READ_LE16(p + MZ_ZIP_CDH_BIT_FLAG_OFS);

    if ((method != 0) && (method != MZ_DEFLATED))
    {
        mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_METHOD);
        return MZ_FALSE;
    }

    if (bit_flag & (MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_IS_ENCRYPTED | MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_USES_STRONG_ENCRYPTION))
    {
        mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_ENCRYPTION);
        return MZ_FALSE;
    }

    if (bit_flag & MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_COMPRESSED_PATCH_FLAG)
    {
        mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_FEATURE);
        return MZ_FALSE;
    }

    return MZ_TRUE;
}

mz_bool mz_zip_reader_is_file_a_directory(mz_zip_archive *pZip, mz_uint file_index)
{
    mz_uint filename_len, attribute_mapping_id, external_attr;
    const mz_uint8 *p = mz_zip_get_cdh(pZip, file_index);
    if (!p)
    {
        mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);
        return MZ_FALSE;
    }

    filename_len = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
    if (filename_len)
    {
        if (*(p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + filename_len - 1) == '/')
            return MZ_TRUE;
    }

    /* Bugfix: This code was also checking if the internal attribute was non-zero, which wasn't correct. */
    /* Most/all zip writers (hopefully) set DOS file/directory attributes in the low 16-bits, so check for the DOS directory flag and ignore the source OS ID in the created by field. */
    /* FIXME: Remove this check? Is it necessary - we already check the filename. */
    attribute_mapping_id = MZ_READ_LE16(p + MZ_ZIP_CDH_VERSION_MADE_BY_OFS) >> 8;
    (void)attribute_mapping_id;

    external_attr = MZ_READ_LE32(p + MZ_ZIP_CDH_EXTERNAL_ATTR_OFS);
    if ((external_attr & MZ_ZIP_DOS_DIR_ATTRIBUTE_BITFLAG) != 0)
    {
        return MZ_TRUE;
    }

    return MZ_FALSE;
}

static mz_bool mz_zip_file_stat_internal(mz_zip_archive *pZip, mz_uint file_index, const mz_uint8 *pCentral_dir_header, mz_zip_archive_file_stat *pStat, mz_bool *pFound_zip64_extra_data)
{
    mz_uint n;
    const mz_uint8 *p = pCentral_dir_header;

    if (pFound_zip64_extra_data)
        *pFound_zip64_extra_data = MZ_FALSE;

    if ((!p) || (!pStat))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    /* Extract fields from the central directory record. */
    pStat->m_file_index = file_index;
    pStat->m_central_dir_ofs = MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir_offsets, mz_uint32, file_index);
    pStat->m_version_made_by = MZ_READ_LE16(p + MZ_ZIP_CDH_VERSION_MADE_BY_OFS);
    pStat->m_version_needed = MZ_READ_LE16(p + MZ_ZIP_CDH_VERSION_NEEDED_OFS);
    pStat->m_bit_flag = MZ_READ_LE16(p + MZ_ZIP_CDH_BIT_FLAG_OFS);
    pStat->m_method = MZ_READ_LE16(p + MZ_ZIP_CDH_METHOD_OFS);
    pStat->m_crc32 = MZ_READ_LE32(p + MZ_ZIP_CDH_CRC32_OFS);
    pStat->m_comp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_COMPRESSED_SIZE_OFS);
    pStat->m_uncomp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS);
    pStat->m_internal_attr = MZ_READ_LE16(p + MZ_ZIP_CDH_INTERNAL_ATTR_OFS);
    pStat->m_external_attr = MZ_READ_LE32(p + MZ_ZIP_CDH_EXTERNAL_ATTR_OFS);
    pStat->m_local_header_ofs = MZ_READ_LE32(p + MZ_ZIP_CDH_LOCAL_HEADER_OFS);

    /* Copy as much of the filename and comment as possible. */
    n = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
    n = MZ_MIN(n, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1);
    memcpy(pStat->m_filename, p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE, n);
    pStat->m_filename[n] = '\0';

    n = MZ_READ_LE16(p + MZ_ZIP_CDH_COMMENT_LEN_OFS);
    n = MZ_MIN(n, MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE - 1);
    pStat->m_comment_size = n;
    memcpy(pStat->m_comment, p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS) + MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS), n);
    pStat->m_comment[n] = '\0';

    /* Set some flags for convienance */
    pStat->m_is_directory = mz_zip_reader_is_file_a_directory(pZip, file_index);
    pStat->m_is_encrypted = mz_zip_reader_is_file_encrypted(pZip, file_index);
    pStat->m_is_supported = mz_zip_reader_is_file_supported(pZip, file_index);

    /* See if we need to read any zip64 extended information fields. */
    /* Confusingly, these zip64 fields can be present even on non-zip64 archives (Debian zip on a huge files from stdin piped to stdout creates them). */
    if (MZ_MAX(MZ_MAX(pStat->m_comp_size, pStat->m_uncomp_size), pStat->m_local_header_ofs) == MZ_UINT32_MAX)
    {
        /* Attempt to find zip64 extended information field in the entry's extra data */
        mz_uint32 extra_size_remaining = MZ_READ_LE16(p + MZ_ZIP_CDH_EXTRA_LEN_OFS);

        if (extra_size_remaining)
        {
            const mz_uint8 *pExtra_data = p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE + MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);

            do
            {
                mz_uint32 field_id;
                mz_uint32 field_data_size;

                if (extra_size_remaining < (sizeof(mz_uint16) * 2))
                    return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

                field_id = MZ_READ_LE16(pExtra_data);
                field_data_size = MZ_READ_LE16(pExtra_data + sizeof(mz_uint16));

                if ((field_data_size + sizeof(mz_uint16) * 2) > extra_size_remaining)
                    return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

                if (field_id == MZ_ZIP64_EXTENDED_INFORMATION_FIELD_HEADER_ID)
                {
                    const mz_uint8 *pField_data = pExtra_data + sizeof(mz_uint16) * 2;
                    mz_uint32 field_data_remaining = field_data_size;

                    if (pFound_zip64_extra_data)
                        *pFound_zip64_extra_data = MZ_TRUE;

                    if (pStat->m_uncomp_size == MZ_UINT32_MAX)
                    {
                        if (field_data_remaining < sizeof(mz_uint64))
                            return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

                        pStat->m_uncomp_size = MZ_READ_LE64(pField_data);
                        pField_data += sizeof(mz_uint64);
                        field_data_remaining -= sizeof(mz_uint64);
                    }

                    if (pStat->m_comp_size == MZ_UINT32_MAX)
                    {
                        if (field_data_remaining < sizeof(mz_uint64))
                            return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

                        pStat->m_comp_size = MZ_READ_LE64(pField_data);
                        pField_data += sizeof(mz_uint64);
                        field_data_remaining -= sizeof(mz_uint64);
                    }

                    if (pStat->m_local_header_ofs == MZ_UINT32_MAX)
                    {
                        if (field_data_remaining < sizeof(mz_uint64))
                            return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

                        pStat->m_local_header_ofs = MZ_READ_LE64(pField_data);
                        pField_data += sizeof(mz_uint64);
                        field_data_remaining -= sizeof(mz_uint64);
                    }

                    break;
                }

                pExtra_data += sizeof(mz_uint16) * 2 + field_data_size;
                extra_size_remaining = extra_size_remaining - sizeof(mz_uint16) * 2 - field_data_size;
            } while (extra_size_remaining);
        }
    }

    return MZ_TRUE;
}

static MZ_FORCEINLINE mz_bool mz_zip_string_equal(const char *pA, const char *pB, mz_uint len, mz_uint flags)
{
    mz_uint i;
    if (flags & MZ_ZIP_FLAG_CASE_SENSITIVE)
        return 0 == memcmp(pA, pB, len);
    for (i = 0; i < len; ++i)
        if (MZ_TOLOWER(pA[i]) != MZ_TOLOWER(pB[i]))
            return MZ_FALSE;
    return MZ_TRUE;
}

static MZ_FORCEINLINE int mz_zip_filename_compare(const mz_zip_array *pCentral_dir_array, const mz_zip_array *pCentral_dir_offsets, mz_uint l_index, const char *pR, mz_uint r_len)
{
    const mz_uint8 *pL = &MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_array, mz_uint8, MZ_ZIP_ARRAY_ELEMENT(pCentral_dir_offsets, mz_uint32, l_index)), *pE;
    mz_uint l_len = MZ_READ_LE16(pL + MZ_ZIP_CDH_FILENAME_LEN_OFS);
    mz_uint8 l = 0, r = 0;
    pL += MZ_ZIP_CENTRAL_DIR_HEADER_SIZE;
    pE = pL + MZ_MIN(l_len, r_len);
    while (pL < pE)
    {
        if ((l = MZ_TOLOWER(*pL)) != (r = MZ_TOLOWER(*pR)))
            break;
        pL++;
        pR++;
    }
    return (pL == pE) ? (int)(l_len - r_len) : (l - r);
}

static mz_bool mz_zip_locate_file_binary_search(mz_zip_archive *pZip, const char *pFilename, mz_uint32 *pIndex)
{
    mz_zip_internal_state *pState = pZip->m_pState;
    const mz_zip_array *pCentral_dir_offsets = &pState->m_central_dir_offsets;
    const mz_zip_array *pCentral_dir = &pState->m_central_dir;
    mz_uint32 *pIndices = &MZ_ZIP_ARRAY_ELEMENT(&pState->m_sorted_central_dir_offsets, mz_uint32, 0);
    const uint32_t size = pZip->m_total_files;
    const mz_uint filename_len = (mz_uint)strlen(pFilename);

    if (pIndex)
        *pIndex = 0;

    if (size)
    {
        /* yes I could use uint32_t's, but then we would have to add some special case checks in the loop, argh, and */
        /* honestly the major expense here on 32-bit CPU's will still be the filename compare */
        mz_int64 l = 0, h = (mz_int64)size - 1;

        while (l <= h)
        {
            mz_int64 m = l + ((h - l) >> 1);
            uint32_t file_index = pIndices[(uint32_t)m];

            int comp = mz_zip_filename_compare(pCentral_dir, pCentral_dir_offsets, file_index, pFilename, filename_len);
            if (!comp)
            {
                if (pIndex)
                    *pIndex = file_index;
                return MZ_TRUE;
            }
            else if (comp < 0)
                l = m + 1;
            else
                h = m - 1;
        }
    }

    return mz_zip_set_error(pZip, MZ_ZIP_FILE_NOT_FOUND);
}

int mz_zip_reader_locate_file(mz_zip_archive *pZip, const char *pName, const char *pComment, mz_uint flags)
{
    mz_uint32 index;
    if (!mz_zip_reader_locate_file_v2(pZip, pName, pComment, flags, &index))
        return -1;
    else
        return (int)index;
}

mz_bool mz_zip_reader_locate_file_v2(mz_zip_archive *pZip, const char *pName, const char *pComment, mz_uint flags, mz_uint32 *pIndex)
{
    mz_uint file_index;
    size_t name_len, comment_len;

    if (pIndex)
        *pIndex = 0;

    if ((!pZip) || (!pZip->m_pState) || (!pName))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    /* See if we can use a binary search */
    if (((pZip->m_pState->m_init_flags & MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY) == 0) &&
        (pZip->m_zip_mode == MZ_ZIP_MODE_READING) &&
        ((flags & (MZ_ZIP_FLAG_IGNORE_PATH | MZ_ZIP_FLAG_CASE_SENSITIVE)) == 0) && (!pComment) && (pZip->m_pState->m_sorted_central_dir_offsets.m_size))
    {
        return mz_zip_locate_file_binary_search(pZip, pName, pIndex);
    }

    /* Locate the entry by scanning the entire central directory */
    name_len = strlen(pName);
    if (name_len > MZ_UINT16_MAX)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    comment_len = pComment ? strlen(pComment) : 0;
    if (comment_len > MZ_UINT16_MAX)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    for (file_index = 0; file_index < pZip->m_total_files; file_index++)
    {
        const mz_uint8 *pHeader = &MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir, mz_uint8, MZ_ZIP_ARRAY_ELEMENT(&pZip->m_pState->m_central_dir_offsets, mz_uint32, file_index));
        mz_uint filename_len = MZ_READ_LE16(pHeader + MZ_ZIP_CDH_FILENAME_LEN_OFS);
        const char *pFilename = (const char *)pHeader + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE;
        if (filename_len < name_len)
            continue;
        if (comment_len)
        {
            mz_uint file_extra_len = MZ_READ_LE16(pHeader + MZ_ZIP_CDH_EXTRA_LEN_OFS), file_comment_len = MZ_READ_LE16(pHeader + MZ_ZIP_CDH_COMMENT_LEN_OFS);
            const char *pFile_comment = pFilename + filename_len + file_extra_len;
            if ((file_comment_len != comment_len) || (!mz_zip_string_equal(pComment, pFile_comment, file_comment_len, flags)))
                continue;
        }
        if ((flags & MZ_ZIP_FLAG_IGNORE_PATH) && (filename_len))
        {
            int ofs = filename_len - 1;
            do
            {
                if ((pFilename[ofs] == '/') || (pFilename[ofs] == '\\') || (pFilename[ofs] == ':'))
                    break;
            } while (--ofs >= 0);
            ofs++;
            pFilename += ofs;
            filename_len -= ofs;
        }
        if ((filename_len == name_len) && (mz_zip_string_equal(pName, pFilename, filename_len, flags)))
        {
            if (pIndex)
                *pIndex = file_index;
            return MZ_TRUE;
        }
    }

    return mz_zip_set_error(pZip, MZ_ZIP_FILE_NOT_FOUND);
}

mz_bool mz_zip_reader_extract_to_mem_no_alloc(mz_zip_archive *pZip, mz_uint file_index, void *pBuf, size_t buf_size, mz_uint flags, void *pUser_read_buf, size_t user_read_buf_size)
{
    int status = TINFL_STATUS_DONE;
    mz_uint64 needed_size, cur_file_ofs, comp_remaining, out_buf_ofs = 0, read_buf_size, read_buf_ofs = 0, read_buf_avail;
    mz_zip_archive_file_stat file_stat;
    void *pRead_buf;
    mz_uint32 local_header_u32[(MZ_ZIP_LOCAL_DIR_HEADER_SIZE + sizeof(mz_uint32) - 1) / sizeof(mz_uint32)];
    mz_uint8 *pLocal_header = (mz_uint8 *)local_header_u32;
    tinfl_decompressor inflator;

    if ((!pZip) || (!pZip->m_pState) || ((buf_size) && (!pBuf)) || ((user_read_buf_size) && (!pUser_read_buf)) || (!pZip->m_pRead))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    if (!mz_zip_reader_file_stat(pZip, file_index, &file_stat))
        return MZ_FALSE;

    /* A directory or zero length file */
    if ((file_stat.m_is_directory) || (!file_stat.m_comp_size))
        return MZ_TRUE;

    /* Encryption and patch files are not supported. */
    if (file_stat.m_bit_flag & (MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_IS_ENCRYPTED | MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_USES_STRONG_ENCRYPTION | MZ_ZIP_GENERAL_PURPOSE_BIT_FLAG_COMPRESSED_PATCH_FLAG))
        return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_ENCRYPTION);

    /* This function only supports decompressing stored and deflate. */
    if ((!(flags & MZ_ZIP_FLAG_COMPRESSED_DATA)) && (file_stat.m_method != 0) && (file_stat.m_method != MZ_DEFLATED))
        return mz_zip_set_error(pZip, MZ_ZIP_UNSUPPORTED_METHOD);

    /* Ensure supplied output buffer is large enough. */
    needed_size = (flags & MZ_ZIP_FLAG_COMPRESSED_DATA) ? file_stat.m_comp_size : file_stat.m_uncomp_size;
    if (buf_size < needed_size)
        return mz_zip_set_error(pZip, MZ_ZIP_BUF_TOO_SMALL);

    /* Read and parse the local directory entry. */
    cur_file_ofs = file_stat.m_local_header_ofs;
    if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pLocal_header, MZ_ZIP_LOCAL_DIR_HEADER_SIZE) != MZ_ZIP_LOCAL_DIR_HEADER_SIZE)
        return mz_zip_set_error(pZip, MZ_ZIP_FILE_READ_FAILED);

    if (MZ_READ_LE32(pLocal_header) != MZ_ZIP_LOCAL_DIR_HEADER_SIG)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

    cur_file_ofs += MZ_ZIP_LOCAL_DIR_HEADER_SIZE + MZ_READ_LE16(pLocal_header + MZ_ZIP_LDH_FILENAME_LEN_OFS) + MZ_READ_LE16(pLocal_header + MZ_ZIP_LDH_EXTRA_LEN_OFS);
    if ((cur_file_ofs + file_stat.m_comp_size) > pZip->m_archive_size)
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_HEADER_OR_CORRUPTED);

    if ((flags & MZ_ZIP_FLAG_COMPRESSED_DATA) || (!file_stat.m_method))
    {
        /* The file is stored or the caller has requested the compressed data. */
        if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pBuf, (size_t)needed_size) != needed_size)
            return mz_zip_set_error(pZip, MZ_ZIP_FILE_READ_FAILED);

#ifndef MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS
        if ((flags & MZ_ZIP_FLAG_COMPRESSED_DATA) == 0)
        {
            if (mz_crc32(MZ_CRC32_INIT, (const mz_uint8 *)pBuf, (size_t)file_stat.m_uncomp_size) != file_stat.m_crc32)
                return mz_zip_set_error(pZip, MZ_ZIP_CRC_CHECK_FAILED);
        }
#endif

        return MZ_TRUE;
    }

    /* Decompress the file either directly from memory or from a file input buffer. */
    tinfl_init(&inflator);

    if (pZip->m_pState->m_pMem)
    {
        /* Read directly from the archive in memory. */
        pRead_buf = (mz_uint8 *)pZip->m_pState->m_pMem + cur_file_ofs;
        read_buf_size = read_buf_avail = file_stat.m_comp_size;
        comp_remaining = 0;
    }
    else if (pUser_read_buf)
    {
        /* Use a user provided read buffer. */
        if (!user_read_buf_size)
            return MZ_FALSE;
        pRead_buf = (mz_uint8 *)pUser_read_buf;
        read_buf_size = user_read_buf_size;
        read_buf_avail = 0;
        comp_remaining = file_stat.m_comp_size;
    }
    else
    {
        /* Temporarily allocate a read buffer. */
        read_buf_size = MZ_MIN(file_stat.m_comp_size, (mz_uint64)MZ_ZIP_MAX_IO_BUF_SIZE);
        if (((sizeof(size_t) == sizeof(mz_uint32))) && (read_buf_size > 0x7FFFFFFF))
            return mz_zip_set_error(pZip, MZ_ZIP_INTERNAL_ERROR);

        if (NULL == (pRead_buf = pZip->m_pAlloc(pZip->m_pAlloc_opaque, 1, (size_t)read_buf_size)))
            return mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);

        read_buf_avail = 0;
        comp_remaining = file_stat.m_comp_size;
    }

    do
    {
        /* The size_t cast here should be OK because we've verified that the output buffer is >= file_stat.m_uncomp_size above */
        size_t in_buf_size, out_buf_size = (size_t)(file_stat.m_uncomp_size - out_buf_ofs);
        if ((!read_buf_avail) && (!pZip->m_pState->m_pMem))
        {
            read_buf_avail = MZ_MIN(read_buf_size, comp_remaining);
            if (pZip->m_pRead(pZip->m_pIO_opaque, cur_file_ofs, pRead_buf, (size_t)read_buf_avail) != read_buf_avail)
            {
                status = TINFL_STATUS_FAILED;
                mz_zip_set_error(pZip, MZ_ZIP_DECOMPRESSION_FAILED);
                break;
            }
            cur_file_ofs += read_buf_avail;
            comp_remaining -= read_buf_avail;
            read_buf_ofs = 0;
        }
        in_buf_size = (size_t)read_buf_avail;
        status = tinfl_decompress(&inflator, (mz_uint8 *)pRead_buf + read_buf_ofs, &in_buf_size, (mz_uint8 *)pBuf, (mz_uint8 *)pBuf + out_buf_ofs, &out_buf_size, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF | (comp_remaining ? TINFL_FLAG_HAS_MORE_INPUT : 0));
        read_buf_avail -= in_buf_size;
        read_buf_ofs += in_buf_size;
        out_buf_ofs += out_buf_size;
    } while (status == TINFL_STATUS_NEEDS_MORE_INPUT);

    if (status == TINFL_STATUS_DONE)
    {
        /* Make sure the entire file was decompressed, and check its CRC. */
        if (out_buf_ofs != file_stat.m_uncomp_size)
        {
            mz_zip_set_error(pZip, MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE);
            status = TINFL_STATUS_FAILED;
        }
#ifndef MINIZ_DISABLE_ZIP_READER_CRC32_CHECKS
        else if (mz_crc32(MZ_CRC32_INIT, (const mz_uint8 *)pBuf, (size_t)file_stat.m_uncomp_size) != file_stat.m_crc32)
        {
            mz_zip_set_error(pZip, MZ_ZIP_CRC_CHECK_FAILED);
            status = TINFL_STATUS_FAILED;
        }
#endif
    }

    if ((!pZip->m_pState->m_pMem) && (!pUser_read_buf))
        pZip->m_pFree(pZip->m_pAlloc_opaque, pRead_buf);

    return status == TINFL_STATUS_DONE;
}

mz_bool mz_zip_reader_extract_to_mem(mz_zip_archive *pZip, mz_uint file_index, void *pBuf, size_t buf_size, mz_uint flags)
{
    return mz_zip_reader_extract_to_mem_no_alloc(pZip, file_index, pBuf, buf_size, flags, NULL, 0);
}

void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index, size_t *pSize, mz_uint flags)
{
    mz_uint64 comp_size, uncomp_size, alloc_size;
    const mz_uint8 *p = mz_zip_get_cdh(pZip, file_index);
    void *pBuf;

    if (pSize)
        *pSize = 0;

    if (!p)
    {
        mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);
        return NULL;
    }

    comp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_COMPRESSED_SIZE_OFS);
    uncomp_size = MZ_READ_LE32(p + MZ_ZIP_CDH_DECOMPRESSED_SIZE_OFS);

    alloc_size = (flags & MZ_ZIP_FLAG_COMPRESSED_DATA) ? comp_size : uncomp_size;
    if (((sizeof(size_t) == sizeof(mz_uint32))) && (alloc_size > 0x7FFFFFFF))
    {
        mz_zip_set_error(pZip, MZ_ZIP_INTERNAL_ERROR);
        return NULL;
    }

    if (NULL == (pBuf = pZip->m_pAlloc(pZip->m_pAlloc_opaque, 1, (size_t)alloc_size)))
    {
        mz_zip_set_error(pZip, MZ_ZIP_ALLOC_FAILED);
        return NULL;
    }

    if (!mz_zip_reader_extract_to_mem(pZip, file_index, pBuf, (size_t)alloc_size, flags))
    {
        pZip->m_pFree(pZip->m_pAlloc_opaque, pBuf);
        return NULL;
    }

    if (pSize)
        *pSize = (size_t)alloc_size;
    return pBuf;
}

void *mz_zip_reader_extract_file_to_heap(mz_zip_archive *pZip, const char *pFilename, size_t *pSize, mz_uint flags)
{
    mz_uint32 file_index;
    if (!mz_zip_reader_locate_file_v2(pZip, pFilename, NULL, flags, &file_index))
    {
        if (pSize)
            *pSize = 0;
        return MZ_FALSE;
    }
    return mz_zip_reader_extract_to_heap(pZip, file_index, pSize, flags);
}

/* ------------------- Misc utils */

#if 0 /* unused for now */
mz_zip_mode mz_zip_get_mode(mz_zip_archive *pZip)
{
    return pZip ? pZip->m_zip_mode : MZ_ZIP_MODE_INVALID;
}

mz_zip_type mz_zip_get_type(mz_zip_archive *pZip)
{
    return pZip ? pZip->m_zip_type : MZ_ZIP_TYPE_INVALID;
}

mz_zip_error mz_zip_set_last_error(mz_zip_archive *pZip, mz_zip_error err_num)
{
    mz_zip_error prev_err;

    if (!pZip)
        return MZ_ZIP_INVALID_PARAMETER;

    prev_err = pZip->m_last_error;

    pZip->m_last_error = err_num;
    return prev_err;
}

mz_zip_error mz_zip_peek_last_error(mz_zip_archive *pZip)
{
    if (!pZip)
        return MZ_ZIP_INVALID_PARAMETER;

    return pZip->m_last_error;
}

mz_zip_error mz_zip_clear_last_error(mz_zip_archive *pZip)
{
    return mz_zip_set_last_error(pZip, MZ_ZIP_NO_ERROR);
}

mz_zip_error mz_zip_get_last_error(mz_zip_archive *pZip)
{
    mz_zip_error prev_err;

    if (!pZip)
        return MZ_ZIP_INVALID_PARAMETER;

    prev_err = pZip->m_last_error;

    pZip->m_last_error = MZ_ZIP_NO_ERROR;
    return prev_err;
}

const char *mz_zip_get_error_string(mz_zip_error mz_err)
{
    switch (mz_err)
    {
        case MZ_ZIP_NO_ERROR:
            return "no error";
        case MZ_ZIP_UNDEFINED_ERROR:
            return "undefined error";
        case MZ_ZIP_TOO_MANY_FILES:
            return "too many files";
        case MZ_ZIP_FILE_TOO_LARGE:
            return "file too large";
        case MZ_ZIP_UNSUPPORTED_METHOD:
            return "unsupported method";
        case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
            return "unsupported encryption";
        case MZ_ZIP_UNSUPPORTED_FEATURE:
            return "unsupported feature";
        case MZ_ZIP_FAILED_FINDING_CENTRAL_DIR:
            return "failed finding central directory";
        case MZ_ZIP_NOT_AN_ARCHIVE:
            return "not a ZIP archive";
        case MZ_ZIP_INVALID_HEADER_OR_CORRUPTED:
            return "invalid header or archive is corrupted";
        case MZ_ZIP_UNSUPPORTED_MULTIDISK:
            return "unsupported multidisk archive";
        case MZ_ZIP_DECOMPRESSION_FAILED:
            return "decompression failed or archive is corrupted";
        case MZ_ZIP_COMPRESSION_FAILED:
            return "compression failed";
        case MZ_ZIP_UNEXPECTED_DECOMPRESSED_SIZE:
            return "unexpected decompressed size";
        case MZ_ZIP_CRC_CHECK_FAILED:
            return "CRC-32 check failed";
        case MZ_ZIP_UNSUPPORTED_CDIR_SIZE:
            return "unsupported central directory size";
        case MZ_ZIP_ALLOC_FAILED:
            return "allocation failed";
        case MZ_ZIP_FILE_OPEN_FAILED:
            return "file open failed";
        case MZ_ZIP_FILE_CREATE_FAILED:
            return "file create failed";
        case MZ_ZIP_FILE_WRITE_FAILED:
            return "file write failed";
        case MZ_ZIP_FILE_READ_FAILED:
            return "file read failed";
        case MZ_ZIP_FILE_CLOSE_FAILED:
            return "file close failed";
        case MZ_ZIP_FILE_SEEK_FAILED:
            return "file seek failed";
        case MZ_ZIP_FILE_STAT_FAILED:
            return "file stat failed";
        case MZ_ZIP_INVALID_PARAMETER:
            return "invalid parameter";
        case MZ_ZIP_INVALID_FILENAME:
            return "invalid filename";
        case MZ_ZIP_BUF_TOO_SMALL:
            return "buffer too small";
        case MZ_ZIP_INTERNAL_ERROR:
            return "internal error";
        case MZ_ZIP_FILE_NOT_FOUND:
            return "file not found";
        case MZ_ZIP_ARCHIVE_TOO_LARGE:
            return "archive is too large";
        case MZ_ZIP_VALIDATION_FAILED:
            return "validation failed";
        case MZ_ZIP_WRITE_CALLBACK_FAILED:
            return "write calledback failed";
        default:
            break;
    }

    return "unknown error";
}

/* Note: Just because the archive is not zip64 doesn't necessarily mean it doesn't have Zip64 extended information extra field, argh. */
mz_bool mz_zip_is_zip64(mz_zip_archive *pZip)
{
    if ((!pZip) || (!pZip->m_pState))
        return MZ_FALSE;

    return pZip->m_pState->m_zip64;
}

size_t mz_zip_get_central_dir_size(mz_zip_archive *pZip)
{
    if ((!pZip) || (!pZip->m_pState))
        return 0;

    return pZip->m_pState->m_central_dir.m_size;
}

mz_uint mz_zip_reader_get_num_files(mz_zip_archive *pZip)
{
    return pZip ? pZip->m_total_files : 0;
}

mz_uint64 mz_zip_get_archive_size(mz_zip_archive *pZip)
{
    if (!pZip)
        return 0;
    return pZip->m_archive_size;
}

MZ_FILE *mz_zip_get_cfile(mz_zip_archive *pZip)
{
    if ((!pZip) || (!pZip->m_pState))
        return 0;
    return pZip->m_pState->m_pFile;
}

size_t mz_zip_read_archive_data(mz_zip_archive *pZip, mz_uint64 file_ofs, void *pBuf, size_t n)
{
    if ((!pZip) || (!pZip->m_pState) || (!pBuf) || (!pZip->m_pRead))
        return mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);

    return pZip->m_pRead(pZip->m_pIO_opaque, file_ofs, pBuf, n);
}

mz_bool mz_zip_end(mz_zip_archive *pZip)
{
    if (!pZip)
        return MZ_FALSE;

    if (pZip->m_zip_mode == MZ_ZIP_MODE_READING)
        return mz_zip_reader_end(pZip);

    return MZ_FALSE;
}

mz_uint mz_zip_reader_get_filename(mz_zip_archive *pZip, mz_uint file_index, char *pFilename, mz_uint filename_buf_size)
{
    mz_uint n;
    const mz_uint8 *p = mz_zip_get_cdh(pZip, file_index);
    if (!p)
    {
        if (filename_buf_size)
            pFilename[0] = '\0';
        mz_zip_set_error(pZip, MZ_ZIP_INVALID_PARAMETER);
        return 0;
    }
    n = MZ_READ_LE16(p + MZ_ZIP_CDH_FILENAME_LEN_OFS);
    if (filename_buf_size)
    {
        n = MZ_MIN(n, filename_buf_size - 1);
        memcpy(pFilename, p + MZ_ZIP_CENTRAL_DIR_HEADER_SIZE, n);
        pFilename[n] = '\0';
    }
    return n + 1;
}
#endif

mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, mz_uint file_index, mz_zip_archive_file_stat *pStat)
{
    return mz_zip_file_stat_internal(pZip, file_index, mz_zip_get_cdh(pZip, file_index), pStat, NULL);
}

#ifdef __cplusplus
}
#endif

#endif /*#ifndef MINIZ_NO_ARCHIVE_APIS*/
