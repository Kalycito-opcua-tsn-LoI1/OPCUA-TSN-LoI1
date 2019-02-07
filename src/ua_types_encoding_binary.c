/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. 
 *
 *    Copyright 2014-2018 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2014-2017 (c) Florian Palm
 *    Copyright 2014-2016 (c) Sten Grüner
 *    Copyright 2014 (c) Leon Urbas
 *    Copyright 2015 (c) LEvertz
 *    Copyright 2015 (c) Chris Iatrou
 *    Copyright 2015-2016 (c) Oleksiy Vasylyev
 *    Copyright 2016-2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2016 (c) Lorenz Haas
 *    Copyright 2017 (c) Mark Giraud, Fraunhofer IOSB
 *    Copyright 2017 (c) Henrik Norrman
 */

#include "ua_util.h"
#include "ua_types_encoding_binary.h"
#include "ua_types_generated.h"
#include "ua_types_generated_handling.h"

/**
 * Type Encoding and Decoding
 * --------------------------
 * The following methods contain encoding and decoding functions for the builtin
 * data types and generic functions that operate on all types and arrays. This
 * requires the type description from a UA_DataType structure.
 *
 * Encoding Context
 * ^^^^^^^^^^^^^^^^
 * If possible, the encoding context is stored in a thread-local variable to
 * speed up encoding. If thread-local variables are not supported, the context
 * is "looped through" every method call. The ``_``-macro accesses either the
 * thread-local or the "looped through" context . */

#define UA_ENCODING_MAX_RECURSION 20

typedef struct {
    /* Pointers to the current position and the last position in the buffer */
    u8 *pos;
    const u8 *end;

    u16 depth; /* How often did we en-/decoding recurse? */

    size_t customTypesArraySize;
    const UA_DataType *customTypesArray;

    UA_exchangeEncodeBuffer exchangeBufferCallback;
    void *exchangeBufferCallbackHandle;
} Ctx;

typedef status (*encodeBinarySignature)(const void *UA_RESTRICT src, const UA_DataType *type,
                                        Ctx *UA_RESTRICT ctx);
typedef status (*decodeBinarySignature)(void *UA_RESTRICT dst, const UA_DataType *type,
                                        Ctx *UA_RESTRICT ctx);
typedef size_t (*calcSizeBinarySignature)(const void *UA_RESTRICT p, const UA_DataType *contenttype);

#define ENCODE_BINARY(TYPE) static status \
    TYPE##_encodeBinary(const UA_##TYPE *UA_RESTRICT src, const UA_DataType *type, Ctx *UA_RESTRICT ctx)
#define DECODE_BINARY(TYPE) static status \
    TYPE##_decodeBinary(UA_##TYPE *UA_RESTRICT dst, const UA_DataType *type, Ctx *UA_RESTRICT ctx)
#define CALCSIZE_BINARY(TYPE) static size_t \
    TYPE##_calcSizeBinary(const UA_##TYPE *UA_RESTRICT src, const UA_DataType *_)
#define ENCODE_DIRECT(SRC, TYPE) TYPE##_encodeBinary((const UA_##TYPE*)SRC, NULL, ctx)
#define DECODE_DIRECT(DST, TYPE) TYPE##_decodeBinary((UA_##TYPE*)DST, NULL, ctx)

/* Jumptables for de-/encoding and computing the buffer length. The methods in
 * the decoding jumptable do not all clean up their allocated memory when an
 * error occurs. So a final _deleteMembers needs to be called before returning
 * to the user. */
extern const encodeBinarySignature encodeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1];
extern const decodeBinarySignature decodeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1];
extern const calcSizeBinarySignature calcSizeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1];
static status encodeBinaryInternal(const void *src, const UA_DataType *type, Ctx *ctx);
static status decodeBinaryInternal(void *dst, const UA_DataType *type, Ctx *ctx);

/**
 * Chunking
 * ^^^^^^^^
 * Breaking a message into chunks is integrated with the encoding. When the end
 * of a buffer is reached, a callback is executed that sends the current buffer
 * as a chunk and exchanges the encoding buffer "underneath" the ongoing
 * encoding. This reduces the RAM requirements and unnecessary copying.
 *
 * In encodeBinaryInternal and Array_encodeBinary, we store a pointer to the
 * last "good position" in the buffer. If we reach the end of the buffer, the
 * encoding until that point is sent out. Afterwards the "good position" pointer
 * is no longer valid. In order to prevent reuse, no method must return
 * UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED after having called exchangeBuffer().
 * This needs to be ensured for the following methods:
 *
 * encodeBinaryInternal
 * Array_encodeBinary
 * NodeId_encodeBinary
 * ExpandedNodeId_encodeBinary
 * LocalizedText_encodeBinary
 * ExtensionObject_encodeBinary
 * Variant_encodeBinary
 * DataValue_encodeBinary
 * DiagnosticInfo_encodeBinary */

/* Send the current chunk and replace the buffer */
static status exchangeBuffer(Ctx *ctx) {
    if(!ctx->exchangeBufferCallback)
        return UA_STATUSCODE_BADENCODINGERROR;
    return ctx->exchangeBufferCallback(ctx->exchangeBufferCallbackHandle, &ctx->pos, &ctx->end);
}

/* If encoding fails, exchange the buffer and try again. It is assumed that the
 * following encoding never fails on a fresh buffer. This is true for numerical
 * types. */
static status
encodeWithExchangeBuffer(const void *ptr, encodeBinarySignature encodeFunc, Ctx *ctx) {
    status ret = encodeFunc(ptr, NULL, ctx);
    if(ret == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED) {
        ret = exchangeBuffer(ctx);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
        encodeFunc(ptr, NULL, ctx);
    }
    return UA_STATUSCODE_GOOD;
}

#define ENCODE_WITHEXCHANGE(VAR, TYPE) \
    encodeWithExchangeBuffer((const void*)VAR, (encodeBinarySignature)TYPE##_encodeBinary, ctx)

/*****************/
/* Integer Types */
/*****************/

#if !UA_BINARY_OVERLAYABLE_INTEGER

#pragma message "Integer endianness could not be detected to be little endian. Use slow generic encoding."

/* These en/decoding functions are only used when the architecture isn't little-endian. */
static void
UA_encode16(const u16 v, u8 buf[2]) {
    buf[0] = (u8)v;
    buf[1] = (u8)(v >> 8);
}

static void
UA_decode16(const u8 buf[2], u16 *v) {
    *v = (u16)((u16)buf[0] + (((u16)buf[1]) << 8));
}

static void
UA_encode32(const u32 v, u8 buf[4]) {
    buf[0] = (u8)v;
    buf[1] = (u8)(v >> 8);
    buf[2] = (u8)(v >> 16);
    buf[3] = (u8)(v >> 24);
}

static void
UA_decode32(const u8 buf[4], u32 *v) {
    *v = (u32)((u32)buf[0] + (((u32)buf[1]) << 8) +
             (((u32)buf[2]) << 16) + (((u32)buf[3]) << 24));
}

static void
UA_encode64(const u64 v, u8 buf[8]) {
    buf[0] = (u8)v;
    buf[1] = (u8)(v >> 8);
    buf[2] = (u8)(v >> 16);
    buf[3] = (u8)(v >> 24);
    buf[4] = (u8)(v >> 32);
    buf[5] = (u8)(v >> 40);
    buf[6] = (u8)(v >> 48);
    buf[7] = (u8)(v >> 56);
}

static void
UA_decode64(const u8 buf[8], u64 *v) {
    *v = (u64)((u64)buf[0] + (((u64)buf[1]) << 8) +
             (((u64)buf[2]) << 16) + (((u64)buf[3]) << 24) +
             (((u64)buf[4]) << 32) + (((u64)buf[5]) << 40) +
             (((u64)buf[6]) << 48) + (((u64)buf[7]) << 56));
}

#endif /* !UA_BINARY_OVERLAYABLE_INTEGER */

/* Boolean */
ENCODE_BINARY(Boolean) {
    if(ctx->pos + sizeof(bool) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
    *ctx->pos = *(const u8*)src;
    ++ctx->pos;
    return UA_STATUSCODE_GOOD;
}

DECODE_BINARY(Boolean) {
    if(ctx->pos + sizeof(bool) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
    *dst = (*ctx->pos > 0) ? true : false;
    ++ctx->pos;
    return UA_STATUSCODE_GOOD;
}

/* Byte */
ENCODE_BINARY(Byte) {
    if(ctx->pos + sizeof(u8) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
    *ctx->pos = *(const u8*)src;
    ++ctx->pos;
    return UA_STATUSCODE_GOOD;
}

DECODE_BINARY(Byte) {
    if(ctx->pos + sizeof(u8) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
    *dst = *ctx->pos;
    ++ctx->pos;
    return UA_STATUSCODE_GOOD;
}

/* UInt16 */
ENCODE_BINARY(UInt16) {
    if(ctx->pos + sizeof(u16) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(ctx->pos, src, sizeof(u16));
#else
    UA_encode16(*src, ctx->pos);
#endif
    ctx->pos += 2;
    return UA_STATUSCODE_GOOD;
}

DECODE_BINARY(UInt16) {
    if(ctx->pos + sizeof(u16) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(dst, ctx->pos, sizeof(u16));
#else
    UA_decode16(ctx->pos, dst);
#endif
    ctx->pos += 2;
    return UA_STATUSCODE_GOOD;
}

/* UInt32 */
ENCODE_BINARY(UInt32) {
    if(ctx->pos + sizeof(u32) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(ctx->pos, src, sizeof(u32));
#else
    UA_encode32(*src, ctx->pos);
#endif
    ctx->pos += 4;
    return UA_STATUSCODE_GOOD;
}

DECODE_BINARY(UInt32) {
    if(ctx->pos + sizeof(u32) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(dst, ctx->pos, sizeof(u32));
#else
    UA_decode32(ctx->pos, dst);
#endif
    ctx->pos += 4;
    return UA_STATUSCODE_GOOD;
}

/* UInt64 */
ENCODE_BINARY(UInt64) {
    if(ctx->pos + sizeof(u64) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(ctx->pos, src, sizeof(u64));
#else
    UA_encode64(*src, ctx->pos);
#endif
    ctx->pos += 8;
    return UA_STATUSCODE_GOOD;
}

DECODE_BINARY(UInt64) {
    if(ctx->pos + sizeof(u64) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
#if UA_BINARY_OVERLAYABLE_INTEGER
    memcpy(dst, ctx->pos, sizeof(u64));
#else
    UA_decode64(ctx->pos, dst);
#endif
    ctx->pos += 8;
    return UA_STATUSCODE_GOOD;
}

/************************/
/* Floating Point Types */
/************************/

#if UA_BINARY_OVERLAYABLE_FLOAT
# define Float_encodeBinary UInt32_encodeBinary
# define Float_decodeBinary UInt32_decodeBinary
# define Double_encodeBinary UInt64_encodeBinary
# define Double_decodeBinary UInt64_decodeBinary
#else

#include <math.h>

#pragma message "No native IEEE 754 format detected. Use slow generic encoding."

/* Handling of IEEE754 floating point values was taken from Beej's Guide to
 * Network Programming (http://beej.us/guide/bgnet/) and enhanced to cover the
 * edge cases +/-0, +/-inf and nan. */
static uint64_t
pack754(long double f, unsigned bits, unsigned expbits) {
    unsigned significandbits = bits - expbits - 1;
    long double fnorm;
    long long sign;
    if(f < 0) { sign = 1; fnorm = -f; }
    else { sign = 0; fnorm = f; }
    int shift = 0;
    while(fnorm >= 2.0) { fnorm /= 2.0; ++shift; }
    while(fnorm < 1.0) { fnorm *= 2.0; --shift; }
    fnorm = fnorm - 1.0;
    long long significand = (long long)(fnorm * ((float)(1LL<<significandbits) + 0.5f));
    long long exponent = shift + ((1<<(expbits-1)) - 1);
    return (uint64_t)((sign<<(bits-1)) | (exponent<<(bits-expbits-1)) | significand);
}

static long double
unpack754(uint64_t i, unsigned bits, unsigned expbits) {
    unsigned significandbits = bits - expbits - 1;
    long double result = (long double)(i&(uint64_t)((1LL<<significandbits)-1));
    result /= (1LL<<significandbits);
    result += 1.0f;
    unsigned bias = (unsigned)(1<<(expbits-1)) - 1;
    long long shift = (long long)((i>>significandbits) & (uint64_t)((1LL<<expbits)-1)) - bias;
    while(shift > 0) { result *= 2.0; --shift; }
    while(shift < 0) { result /= 2.0; ++shift; }
    result *= ((i>>(bits-1))&1)? -1.0: 1.0;
    return result;
}

/* Float */
#define FLOAT_NAN 0xffc00000
#define FLOAT_INF 0x7f800000
#define FLOAT_NEG_INF 0xff800000
#define FLOAT_NEG_ZERO 0x80000000

ENCODE_BINARY(Float) {
    UA_Float f = *src;
    u32 encoded;
    /* cppcheck-suppress duplicateExpression */
    if(f != f) encoded = FLOAT_NAN;
    else if(f == 0.0f) encoded = signbit(f) ? FLOAT_NEG_ZERO : 0;
    else if(f/f != f/f) encoded = f > 0 ? FLOAT_INF : FLOAT_NEG_INF;
    else encoded = (u32)pack754(f, 32, 8);
    return ENCODE_DIRECT(&encoded, UInt32);
}

DECODE_BINARY(Float) {
    u32 decoded;
    status ret = DECODE_DIRECT(&decoded, UInt32);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;
    if(decoded == 0) *dst = 0.0f;
    else if(decoded == FLOAT_NEG_ZERO) *dst = -0.0f;
    else if(decoded == FLOAT_INF) *dst = INFINITY;
    else if(decoded == FLOAT_NEG_INF) *dst = -INFINITY;
    else if((decoded >= 0x7f800001 && decoded <= 0x7fffffff) ||
       (decoded >= 0xff800001)) *dst = NAN;
    else *dst = (UA_Float)unpack754(decoded, 32, 8);
    return UA_STATUSCODE_GOOD;
}

/* Double */
#define DOUBLE_NAN 0xfff8000000000000L
#define DOUBLE_INF 0x7ff0000000000000L
#define DOUBLE_NEG_INF 0xfff0000000000000L
#define DOUBLE_NEG_ZERO 0x8000000000000000L

ENCODE_BINARY(Double) {
    UA_Double d = *src;
    u64 encoded;
    /* cppcheck-suppress duplicateExpression */
    if(d != d) encoded = DOUBLE_NAN;
    else if(d == 0.0) encoded = signbit(d) ? DOUBLE_NEG_ZERO : 0;
    else if(d/d != d/d) encoded = d > 0 ? DOUBLE_INF : DOUBLE_NEG_INF;
    else encoded = pack754(d, 64, 11);
    return ENCODE_DIRECT(&encoded, UInt64);
}

DECODE_BINARY(Double) {
    u64 decoded;
    status ret = DECODE_DIRECT(&decoded, UInt64);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;
    if(decoded == 0) *dst = 0.0;
    else if(decoded == DOUBLE_NEG_ZERO) *dst = -0.0;
    else if(decoded == DOUBLE_INF) *dst = INFINITY;
    else if(decoded == DOUBLE_NEG_INF) *dst = -INFINITY;
    else if((decoded >= 0x7ff0000000000001L && decoded <= 0x7fffffffffffffffL) ||
       (decoded >= 0xfff0000000000001L)) *dst = NAN;
    else *dst = (UA_Double)unpack754(decoded, 64, 11);
    return UA_STATUSCODE_GOOD;
}

#endif

/******************/
/* Array Handling */
/******************/

static status
Array_encodeBinaryOverlayable(uintptr_t ptr, size_t length, size_t elementMemSize, Ctx *ctx) {
    /* Store the number of already encoded elements */
    size_t finished = 0;

    /* Loop as long as more elements remain than fit into the chunk */
    while(ctx->end < ctx->pos + (elementMemSize * (length-finished))) {
        size_t possible = ((uintptr_t)ctx->end - (uintptr_t)ctx->pos) / (sizeof(u8) * elementMemSize);
        size_t possibleMem = possible * elementMemSize;
        memcpy(ctx->pos, (void*)ptr, possibleMem);
        ctx->pos += possibleMem;
        ptr += possibleMem;
        finished += possible;
        status ret = exchangeBuffer(ctx);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    /* Encode the remaining elements */
    memcpy(ctx->pos, (void*)ptr, elementMemSize * (length-finished));
    ctx->pos += elementMemSize * (length-finished);
    return UA_STATUSCODE_GOOD;
}

static status
Array_encodeBinaryComplex(uintptr_t ptr, size_t length, const UA_DataType *type, Ctx *ctx) {
    /* Get the encoding function for the data type. The jumptable at
     * UA_BUILTIN_TYPES_COUNT points to the generic UA_encodeBinary method */
    size_t encode_index = type->builtin ? type->typeIndex : UA_BUILTIN_TYPES_COUNT;
    encodeBinarySignature encodeType = encodeBinaryJumpTable[encode_index];

    /* Encode every element */
    for(size_t i = 0; i < length; ++i) {
        u8 *oldpos = ctx->pos;
        status ret = encodeType((const void*)ptr, type, ctx);
        ptr += type->memSize;
        /* Encoding failed, switch to the next chunk when possible */
        if(ret != UA_STATUSCODE_GOOD) {
            if(ret == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED) {
                ctx->pos = oldpos; /* Set buffer position to the end of the last encoded element */
                ret = exchangeBuffer(ctx);
                ptr -= type->memSize; /* Undo to retry encoding the ith element */
                --i;
            }
            UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
            if(ret != UA_STATUSCODE_GOOD)
                return ret; /* Unrecoverable fail */
        }
    }
    return UA_STATUSCODE_GOOD;
}

static status
Array_encodeBinary(const void *src, size_t length, const UA_DataType *type, Ctx *ctx) {
    /* Check and convert the array length to int32 */
    i32 signed_length = -1;
    if(length > UA_INT32_MAX)
        return UA_STATUSCODE_BADINTERNALERROR;
    if(length > 0)
        signed_length = (i32)length;
    else if(src == UA_EMPTY_ARRAY_SENTINEL)
        signed_length = 0;

    /* Encode the array length */
    status ret = ENCODE_WITHEXCHANGE(&signed_length, UInt32);

    /* Quit early? */
    if(ret != UA_STATUSCODE_GOOD || length == 0)
        return ret;

    /* Encode the content */
    if(!type->overlayable)
        return Array_encodeBinaryComplex((uintptr_t)src, length, type, ctx);
    return Array_encodeBinaryOverlayable((uintptr_t)src, length, type->memSize, ctx);
}

static status
Array_decodeBinary(void *UA_RESTRICT *UA_RESTRICT dst, size_t *out_length,
                   const UA_DataType *type, Ctx *ctx) {
    /* Decode the length */
    i32 signed_length;
    status ret = DECODE_DIRECT(&signed_length, UInt32); /* Int32 */
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Return early for empty arrays */
    if(signed_length <= 0) {
        *out_length = 0;
        if(signed_length < 0)
            *dst = NULL;
        else
            *dst = UA_EMPTY_ARRAY_SENTINEL;
        return UA_STATUSCODE_GOOD;
    }

    /* Filter out arrays that can obviously not be decoded, because the message
     * is too small for the array length. This prevents the allocation of very
     * long arrays for bogus messages.*/
    size_t length = (size_t)signed_length;
    if(ctx->pos + ((type->memSize * length) / 32) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;

    /* Allocate memory */
    *dst = UA_calloc(length, type->memSize);
    if(!*dst)
        return UA_STATUSCODE_BADOUTOFMEMORY;

    if(type->overlayable) {
        /* memcpy overlayable array */
        if(ctx->end < ctx->pos + (type->memSize * length)) {
            UA_free(*dst);
            *dst = NULL;
            return UA_STATUSCODE_BADDECODINGERROR;
        }
        memcpy(*dst, ctx->pos, type->memSize * length);
        ctx->pos += type->memSize * length;
    } else {
        /* Decode array members */
        uintptr_t ptr = (uintptr_t)*dst;
        size_t decode_index = type->builtin ? type->typeIndex : UA_BUILTIN_TYPES_COUNT;
        for(size_t i = 0; i < length; ++i) {
            ret = decodeBinaryJumpTable[decode_index]((void*)ptr, type, ctx);
            if(ret != UA_STATUSCODE_GOOD) {
                /* +1 because last element is also already initialized */
                UA_Array_delete(*dst, i+1, type);
                *dst = NULL;
                return ret;
            }
            ptr += type->memSize;
        }
    }
    *out_length = length;
    return UA_STATUSCODE_GOOD;
}

/*****************/
/* Builtin Types */
/*****************/

ENCODE_BINARY(String) {
    return Array_encodeBinary(src->data, src->length, &UA_TYPES[UA_TYPES_BYTE], ctx);
}

DECODE_BINARY(String) {
    return Array_decodeBinary((void**)&dst->data, &dst->length, &UA_TYPES[UA_TYPES_BYTE], ctx);
}

/* Guid */
ENCODE_BINARY(Guid) {
    status ret = UA_STATUSCODE_GOOD;
    ret |= ENCODE_DIRECT(&src->data1, UInt32);
    ret |= ENCODE_DIRECT(&src->data2, UInt16);
    ret |= ENCODE_DIRECT(&src->data3, UInt16);
    if(ctx->pos + (8*sizeof(u8)) > ctx->end)
        return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED;
    memcpy(ctx->pos, src->data4, 8*sizeof(u8));
    ctx->pos += 8;
    return ret;
}

DECODE_BINARY(Guid) {
    status ret = UA_STATUSCODE_GOOD;
    ret |= DECODE_DIRECT(&dst->data1, UInt32);
    ret |= DECODE_DIRECT(&dst->data2, UInt16);
    ret |= DECODE_DIRECT(&dst->data3, UInt16);
    if(ctx->pos + (8*sizeof(u8)) > ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
    memcpy(dst->data4, ctx->pos, 8*sizeof(u8));
    ctx->pos += 8;
    return ret;
}

/* NodeId */
#define UA_NODEIDTYPE_NUMERIC_TWOBYTE 0
#define UA_NODEIDTYPE_NUMERIC_FOURBYTE 1
#define UA_NODEIDTYPE_NUMERIC_COMPLETE 2

#define UA_EXPANDEDNODEID_SERVERINDEX_FLAG 0x40
#define UA_EXPANDEDNODEID_NAMESPACEURI_FLAG 0x80

/* For ExpandedNodeId, we prefill the encoding mask. We can return
 * UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED before encoding the string, as the
 * buffer is not replaced. */
static status
NodeId_encodeBinaryWithEncodingMask(UA_NodeId const *src, u8 encoding, Ctx *ctx) {
    status ret = UA_STATUSCODE_GOOD;
    switch(src->identifierType) {
    case UA_NODEIDTYPE_NUMERIC:
        if(src->identifier.numeric > UA_UINT16_MAX || src->namespaceIndex > UA_BYTE_MAX) {
            encoding |= UA_NODEIDTYPE_NUMERIC_COMPLETE;
            ret |= ENCODE_DIRECT(&encoding, Byte);
            ret |= ENCODE_DIRECT(&src->namespaceIndex, UInt16);
            ret |= ENCODE_DIRECT(&src->identifier.numeric, UInt32);
        } else if(src->identifier.numeric > UA_BYTE_MAX || src->namespaceIndex > 0) {
            encoding |= UA_NODEIDTYPE_NUMERIC_FOURBYTE;
            ret |= ENCODE_DIRECT(&encoding, Byte);
            u8 nsindex = (u8)src->namespaceIndex;
            ret |= ENCODE_DIRECT(&nsindex, Byte);
            u16 identifier16 = (u16)src->identifier.numeric;
            ret |= ENCODE_DIRECT(&identifier16, UInt16);
        } else {
            encoding |= UA_NODEIDTYPE_NUMERIC_TWOBYTE;
            ret |= ENCODE_DIRECT(&encoding, Byte);
            u8 identifier8 = (u8)src->identifier.numeric;
            ret |= ENCODE_DIRECT(&identifier8, Byte);
        }
        break;
    case UA_NODEIDTYPE_STRING:
        encoding |= UA_NODEIDTYPE_STRING;
        ret |= ENCODE_DIRECT(&encoding, Byte);
        ret |= ENCODE_DIRECT(&src->namespaceIndex, UInt16);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
        ret = ENCODE_DIRECT(&src->identifier.string, String);
        break;
    case UA_NODEIDTYPE_GUID:
        encoding |= UA_NODEIDTYPE_GUID;
        ret |= ENCODE_DIRECT(&encoding, Byte);
        ret |= ENCODE_DIRECT(&src->namespaceIndex, UInt16);
        ret |= ENCODE_DIRECT(&src->identifier.guid, Guid);
        break;
    case UA_NODEIDTYPE_BYTESTRING:
        encoding |= UA_NODEIDTYPE_BYTESTRING;
        ret |= ENCODE_DIRECT(&encoding, Byte);
        ret |= ENCODE_DIRECT(&src->namespaceIndex, UInt16);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
        ret = ENCODE_DIRECT(&src->identifier.byteString, String); /* ByteString */
        break;
    default:
        return UA_STATUSCODE_BADINTERNALERROR;
    }
    return ret;
}

ENCODE_BINARY(NodeId) {
    return NodeId_encodeBinaryWithEncodingMask(src, 0, ctx);
}

DECODE_BINARY(NodeId) {
    u8 dstByte = 0, encodingByte = 0;
    u16 dstUInt16 = 0;

    /* Decode the encoding bitfield */
    status ret = DECODE_DIRECT(&encodingByte, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Filter out the bits used only for ExpandedNodeIds */
    encodingByte &= (u8)~(UA_EXPANDEDNODEID_SERVERINDEX_FLAG |
                          UA_EXPANDEDNODEID_NAMESPACEURI_FLAG);

    /* Decode the namespace and identifier */
    switch(encodingByte) {
    case UA_NODEIDTYPE_NUMERIC_TWOBYTE:
        dst->identifierType = UA_NODEIDTYPE_NUMERIC;
        ret = DECODE_DIRECT(&dstByte, Byte);
        dst->identifier.numeric = dstByte;
        dst->namespaceIndex = 0;
        break;
    case UA_NODEIDTYPE_NUMERIC_FOURBYTE:
        dst->identifierType = UA_NODEIDTYPE_NUMERIC;
        ret |= DECODE_DIRECT(&dstByte, Byte);
        dst->namespaceIndex = dstByte;
        ret |= DECODE_DIRECT(&dstUInt16, UInt16);
        dst->identifier.numeric = dstUInt16;
        break;
    case UA_NODEIDTYPE_NUMERIC_COMPLETE:
        dst->identifierType = UA_NODEIDTYPE_NUMERIC;
        ret |= DECODE_DIRECT(&dst->namespaceIndex, UInt16);
        ret |= DECODE_DIRECT(&dst->identifier.numeric, UInt32);
        break;
    case UA_NODEIDTYPE_STRING:
        dst->identifierType = UA_NODEIDTYPE_STRING;
        ret |= DECODE_DIRECT(&dst->namespaceIndex, UInt16);
        ret |= DECODE_DIRECT(&dst->identifier.string, String);
        break;
    case UA_NODEIDTYPE_GUID:
        dst->identifierType = UA_NODEIDTYPE_GUID;
        ret |= DECODE_DIRECT(&dst->namespaceIndex, UInt16);
        ret |= DECODE_DIRECT(&dst->identifier.guid, Guid);
        break;
    case UA_NODEIDTYPE_BYTESTRING:
        dst->identifierType = UA_NODEIDTYPE_BYTESTRING;
        ret |= DECODE_DIRECT(&dst->namespaceIndex, UInt16);
        ret |= DECODE_DIRECT(&dst->identifier.byteString, String); /* ByteString */
        break;
    default:
        ret |= UA_STATUSCODE_BADINTERNALERROR;
        break;
    }
    return ret;
}

/* ExpandedNodeId */
ENCODE_BINARY(ExpandedNodeId) {
    /* Set up the encoding mask */
    u8 encoding = 0;
    if((void*)src->namespaceUri.data > UA_EMPTY_ARRAY_SENTINEL)
        encoding |= UA_EXPANDEDNODEID_NAMESPACEURI_FLAG;
    if(src->serverIndex > 0)
        encoding |= UA_EXPANDEDNODEID_SERVERINDEX_FLAG;

    /* Encode the NodeId */
    status ret = NodeId_encodeBinaryWithEncodingMask(&src->nodeId, encoding, ctx);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the namespace. Do not return
     * UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED afterwards. */
    if((void*)src->namespaceUri.data > UA_EMPTY_ARRAY_SENTINEL) {
        ret = ENCODE_DIRECT(&src->namespaceUri, String);
        UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    /* Encode the serverIndex */
    if(src->serverIndex > 0)
        ret = ENCODE_WITHEXCHANGE(&src->serverIndex, UInt32);
    UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
    return ret;
}

DECODE_BINARY(ExpandedNodeId) {
    /* Decode the encoding mask */
    if(ctx->pos >= ctx->end)
        return UA_STATUSCODE_BADDECODINGERROR;
    u8 encoding = *ctx->pos;

    /* Decode the NodeId */
    status ret = DECODE_DIRECT(&dst->nodeId, NodeId);

    /* Decode the NamespaceUri */
    if(encoding & UA_EXPANDEDNODEID_NAMESPACEURI_FLAG) {
        dst->nodeId.namespaceIndex = 0;
        ret |= DECODE_DIRECT(&dst->namespaceUri, String);
    }

    /* Decode the ServerIndex */
    if(encoding & UA_EXPANDEDNODEID_SERVERINDEX_FLAG)
        ret |= DECODE_DIRECT(&dst->serverIndex, UInt32);
    return ret;
}

/* QualifiedName */

ENCODE_BINARY(QualifiedName) {
    return ENCODE_DIRECT(&src->namespaceIndex, UInt16) |
           ENCODE_DIRECT(&src->name, String);
}

DECODE_BINARY(QualifiedName) {
    return DECODE_DIRECT(&dst->namespaceIndex, UInt16) |
        DECODE_DIRECT(&dst->name, String);
}

/* LocalizedText */
#define UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_LOCALE 0x01
#define UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_TEXT 0x02

ENCODE_BINARY(LocalizedText) {
    /* Set up the encoding mask */
    u8 encoding = 0;
    if(src->locale.data)
        encoding |= UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_LOCALE;
    if(src->text.data)
        encoding |= UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_TEXT;

    /* Encode the encoding byte */
    status ret = ENCODE_DIRECT(&encoding, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the strings */
    if(encoding & UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_LOCALE)
        ret |= ENCODE_DIRECT(&src->locale, String);
    if(encoding & UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_TEXT)
        ret |= ENCODE_DIRECT(&src->text, String);
    UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
    return ret;
}

DECODE_BINARY(LocalizedText) {
    /* Decode the encoding mask */
    u8 encoding = 0;
    status ret = DECODE_DIRECT(&encoding, Byte);

    /* Decode the content */
    if(encoding & UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_LOCALE)
        ret |= DECODE_DIRECT(&dst->locale, String);
    if(encoding & UA_LOCALIZEDTEXT_ENCODINGMASKTYPE_TEXT)
        ret |= DECODE_DIRECT(&dst->text, String);
    return ret;
}

/* The binary encoding has a different nodeid from the data type. So it is not
 * possible to reuse UA_findDataType */
static const UA_DataType *
UA_findDataTypeByBinaryInternal(const UA_NodeId *typeId, Ctx *ctx) {
    /* We only store a numeric identifier for the encoding nodeid of data types */
    if(typeId->identifierType != UA_NODEIDTYPE_NUMERIC)
        return NULL;

    /* Always look in built-in types first
     * (may contain data types from all namespaces) */
    for(size_t i = 0; i < UA_TYPES_COUNT; ++i) {
        if(UA_TYPES[i].binaryEncodingId == typeId->identifier.numeric &&
           UA_TYPES[i].typeId.namespaceIndex == typeId->namespaceIndex)
            return &UA_TYPES[i];
    }

    /* When other namespace look in custom types, too */
    if(typeId->namespaceIndex != 0) {
        for(size_t i = 0; i < ctx->customTypesArraySize; ++i) {
            if(ctx->customTypesArray[i].binaryEncodingId == typeId->identifier.numeric &&
               ctx->customTypesArray[i].typeId.namespaceIndex == typeId->namespaceIndex)
                return &ctx->customTypesArray[i];
        }
    }

    return NULL;
}

const UA_DataType *
UA_findDataTypeByBinary(const UA_NodeId *typeId) {
    Ctx ctx;
    ctx.customTypesArraySize = 0;
    ctx.customTypesArray = NULL;
    return UA_findDataTypeByBinaryInternal(typeId, &ctx);
}

/* ExtensionObject */
ENCODE_BINARY(ExtensionObject) {
    u8 encoding = (u8)src->encoding;

    /* No content or already encoded content. Do not return
     * UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED after encoding the NodeId. */
    if(encoding <= UA_EXTENSIONOBJECT_ENCODED_XML) {
        status ret = ENCODE_DIRECT(&src->content.encoded.typeId, NodeId);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
        ret = ENCODE_WITHEXCHANGE(&encoding, Byte);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
        switch(src->encoding) {
        case UA_EXTENSIONOBJECT_ENCODED_NOBODY:
            break;
        case UA_EXTENSIONOBJECT_ENCODED_BYTESTRING:
        case UA_EXTENSIONOBJECT_ENCODED_XML:
            ret = ENCODE_DIRECT(&src->content.encoded.body, String); /* ByteString */
            break;
        default:
            ret = UA_STATUSCODE_BADINTERNALERROR;
        }
        return ret;
    }

    /* Cannot encode with no data or no type description */
    if(!src->content.decoded.type || !src->content.decoded.data)
        return UA_STATUSCODE_BADENCODINGERROR;

    /* Write the NodeId for the binary encoded type. The NodeId is always
     * numeric, so no buffer replacement is taking place. */
    UA_NodeId typeId = src->content.decoded.type->typeId;
    if(typeId.identifierType != UA_NODEIDTYPE_NUMERIC)
        return UA_STATUSCODE_BADENCODINGERROR;
    typeId.identifier.numeric = src->content.decoded.type->binaryEncodingId;
    status ret = ENCODE_DIRECT(&typeId, NodeId);

    /* Write the encoding byte */
    encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
    ret |= ENCODE_DIRECT(&encoding, Byte);

    /* Compute the content length */
    const UA_DataType *contentType = src->content.decoded.type;
    size_t len = UA_calcSizeBinary(src->content.decoded.data, contentType);

    /* Encode the content length */
    if(len > UA_INT32_MAX)
        return UA_STATUSCODE_BADENCODINGERROR;
    i32 signed_len = (i32)len;
    ret |= ENCODE_DIRECT(&signed_len, UInt32); /* Int32 */

    /* Return early upon failures (no buffer exchange until here) */
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the content */
    return encodeBinaryInternal(src->content.decoded.data, contentType, ctx);
}

static status
ExtensionObject_decodeBinaryContent(UA_ExtensionObject *dst, const UA_NodeId *typeId, Ctx *ctx) {
    /* Lookup the datatype */
    const UA_DataType *type = UA_findDataTypeByBinaryInternal(typeId, ctx);

    /* Unknown type, just take the binary content */
    if(!type) {
        dst->encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
        UA_NodeId_copy(typeId, &dst->content.encoded.typeId);
        return DECODE_DIRECT(&dst->content.encoded.body, String); /* ByteString */
    }

    /* Allocate memory */
    dst->content.decoded.data = UA_new(type);
    if(!dst->content.decoded.data)
        return UA_STATUSCODE_BADOUTOFMEMORY;

    /* Jump over the length field (TODO: check if the decoded length matches) */
    ctx->pos += 4;

    /* Decode */
    dst->encoding = UA_EXTENSIONOBJECT_DECODED;
    dst->content.decoded.type = type;
    size_t decode_index = type->builtin ? type->typeIndex : UA_BUILTIN_TYPES_COUNT;
    return decodeBinaryJumpTable[decode_index](dst->content.decoded.data, type, ctx);
}

DECODE_BINARY(ExtensionObject) {
    u8 encoding = 0;
    UA_NodeId binTypeId; /* Can contain a string nodeid. But no corresponding
                          * type is then found in open62541. We only store
                          * numerical nodeids of the binary encoding identifier.
                          * The extenionobject will be decoded to contain a
                          * binary blob. */
    UA_NodeId_init(&binTypeId);
    status ret = UA_STATUSCODE_GOOD;
    ret |= DECODE_DIRECT(&binTypeId, NodeId);
    ret |= DECODE_DIRECT(&encoding, Byte);
    if(ret != UA_STATUSCODE_GOOD) {
        UA_NodeId_deleteMembers(&binTypeId);
        return ret;
    }

    if(encoding == UA_EXTENSIONOBJECT_ENCODED_BYTESTRING) {
        ret = ExtensionObject_decodeBinaryContent(dst, &binTypeId, ctx);
        UA_NodeId_deleteMembers(&binTypeId);
    } else if(encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY) {
        dst->encoding = (UA_ExtensionObjectEncoding)encoding;
        dst->content.encoded.typeId = binTypeId; /* move to dst */
        dst->content.encoded.body = UA_BYTESTRING_NULL;
    } else if(encoding == UA_EXTENSIONOBJECT_ENCODED_XML) {
        dst->encoding = (UA_ExtensionObjectEncoding)encoding;
        dst->content.encoded.typeId = binTypeId; /* move to dst */
        ret = DECODE_DIRECT(&dst->content.encoded.body, String); /* ByteString */
        if(ret != UA_STATUSCODE_GOOD)
            UA_NodeId_deleteMembers(&dst->content.encoded.typeId);
    } else {
        UA_NodeId_deleteMembers(&binTypeId);
        ret = UA_STATUSCODE_BADDECODINGERROR;
    }

    return ret;
}

/* Variant */

/* Never returns UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED */
static status
Variant_encodeBinaryWrapExtensionObject(const UA_Variant *src, const bool isArray, Ctx *ctx) {
    /* Default to 1 for a scalar. */
    size_t length = 1;

    /* Encode the array length if required */
    status ret = UA_STATUSCODE_GOOD;
    if(isArray) {
        if(src->arrayLength > UA_INT32_MAX)
            return UA_STATUSCODE_BADENCODINGERROR;
        length = src->arrayLength;
        i32 encodedLength = (i32)src->arrayLength;
        ret = ENCODE_DIRECT(&encodedLength, UInt32); /* Int32 */
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    /* Set up the ExtensionObject */
    UA_ExtensionObject eo;
    UA_ExtensionObject_init(&eo);
    eo.encoding = UA_EXTENSIONOBJECT_DECODED;
    eo.content.decoded.type = src->type;
    const u16 memSize = src->type->memSize;
    uintptr_t ptr = (uintptr_t)src->data;

    /* Iterate over the array */
    for(size_t i = 0; i < length && ret == UA_STATUSCODE_GOOD; ++i) {
        eo.content.decoded.data = (void*)ptr;
        ret = encodeBinaryInternal(&eo, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT], ctx);
        ptr += memSize;
    }
    return ret;
}

enum UA_VARIANT_ENCODINGMASKTYPE {
    UA_VARIANT_ENCODINGMASKTYPE_TYPEID_MASK = 0x3F,        /* bits 0:5 */
    UA_VARIANT_ENCODINGMASKTYPE_DIMENSIONS  = (0x01 << 6), /* bit 6 */
    UA_VARIANT_ENCODINGMASKTYPE_ARRAY       = (0x01 << 7)  /* bit 7 */
};



ENCODE_BINARY(Variant) {
    /* Quit early for the empty variant */
    u8 encoding = 0;
    if(!src->type)
        return ENCODE_DIRECT(&encoding, Byte);

    /* Set the content type in the encoding mask */
    const bool isBuiltin = src->type->builtin;
    const bool isAlias = src->type->membersSize == 1
                         && UA_TYPES[src->type->members[0].memberTypeIndex].builtin;
    if(isBuiltin)
        encoding |= UA_VARIANT_ENCODINGMASKTYPE_TYPEID_MASK & (u8)(src->type->typeIndex + 1);
    else if(isAlias)
        encoding |= UA_VARIANT_ENCODINGMASKTYPE_TYPEID_MASK & (u8)(src->type->members[0].memberTypeIndex + 1);
    else
        encoding |= UA_VARIANT_ENCODINGMASKTYPE_TYPEID_MASK & (u8)(UA_TYPES_EXTENSIONOBJECT + 1);

    /* Set the array type in the encoding mask */
    const bool isArray = src->arrayLength > 0 || src->data <= UA_EMPTY_ARRAY_SENTINEL;
    const bool hasDimensions = isArray && src->arrayDimensionsSize > 0;
    if(isArray) {
        encoding |= UA_VARIANT_ENCODINGMASKTYPE_ARRAY;
        if(hasDimensions)
            encoding |= UA_VARIANT_ENCODINGMASKTYPE_DIMENSIONS;
    }

    /* Encode the encoding byte */
    status ret = ENCODE_DIRECT(&encoding, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the content */
    if(!isBuiltin && !isAlias)
        ret = Variant_encodeBinaryWrapExtensionObject(src, isArray, ctx);
    else if(!isArray)
        ret = encodeBinaryInternal(src->data, src->type, ctx);
    else
        ret = Array_encodeBinary(src->data, src->arrayLength, src->type, ctx);

    /* Encode the array dimensions */
    if(hasDimensions && ret == UA_STATUSCODE_GOOD)
        ret = Array_encodeBinary(src->arrayDimensions, src->arrayDimensionsSize,
                                 &UA_TYPES[UA_TYPES_INT32], ctx);
    return ret;
}

static status
Variant_decodeBinaryUnwrapExtensionObject(UA_Variant *dst, Ctx *ctx) {
    /* Save the position in the ByteString. If unwrapping is not possible, start
     * from here to decode a normal ExtensionObject. */
    u8 *old_pos = ctx->pos;

    /* Decode the DataType */
    UA_NodeId typeId;
    UA_NodeId_init(&typeId);
    status ret = DECODE_DIRECT(&typeId, NodeId);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Decode the EncodingByte */
    u8 encoding;
    ret = DECODE_DIRECT(&encoding, Byte);
    if(ret != UA_STATUSCODE_GOOD) {
        UA_NodeId_deleteMembers(&typeId);
        return ret;
    }

    /* Search for the datatype. Default to ExtensionObject. */
    if(encoding == UA_EXTENSIONOBJECT_ENCODED_BYTESTRING &&
       (dst->type = UA_findDataTypeByBinaryInternal(&typeId, ctx)) != NULL) {
        /* Jump over the length field (TODO: check if length matches) */
        ctx->pos += 4;
    } else {
        /* Reset and decode as ExtensionObject */
        dst->type = &UA_TYPES[UA_TYPES_EXTENSIONOBJECT];
        ctx->pos = old_pos;
        UA_NodeId_deleteMembers(&typeId);
    }

    /* Allocate memory */
    dst->data = UA_new(dst->type);
    if(!dst->data)
        return UA_STATUSCODE_BADOUTOFMEMORY;

    /* Decode the content */
    size_t decode_index = dst->type->builtin ? dst->type->typeIndex : UA_BUILTIN_TYPES_COUNT;
    return decodeBinaryJumpTable[decode_index](dst->data, dst->type, ctx);
}

/* The resulting variant always has the storagetype UA_VARIANT_DATA. */
DECODE_BINARY(Variant) {
    /* Decode the encoding byte */
    u8 encodingByte;
    status ret = DECODE_DIRECT(&encodingByte, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Return early for an empty variant (was already _inited) */
    if(encodingByte == 0)
        return UA_STATUSCODE_GOOD;

    /* Does the variant contain an array? */
    const bool isArray = (encodingByte & UA_VARIANT_ENCODINGMASKTYPE_ARRAY) > 0;

    /* Get the datatype of the content. The type must be a builtin data type.
     * All not-builtin types are wrapped in an ExtensionObject. */
    size_t typeIndex = (size_t)((encodingByte & UA_VARIANT_ENCODINGMASKTYPE_TYPEID_MASK) - 1);
    if(typeIndex > UA_TYPES_DIAGNOSTICINFO)
        return UA_STATUSCODE_BADDECODINGERROR;

    /* A variant cannot contain a variant. But it can contain an array of
     * variants */
    if(typeIndex == UA_TYPES_VARIANT && !isArray)
        return UA_STATUSCODE_BADDECODINGERROR;

    /* Check the recursion limit */
    if(ctx->depth > UA_ENCODING_MAX_RECURSION)
        return UA_STATUSCODE_BADENCODINGERROR;
    ctx->depth++;

    /* Decode the content */
    dst->type = &UA_TYPES[typeIndex];
    if(isArray) {
        ret = Array_decodeBinary(&dst->data, &dst->arrayLength, dst->type, ctx);
    } else if(typeIndex != UA_TYPES_EXTENSIONOBJECT) {
        dst->data = UA_new(dst->type);
        if(!dst->data)
            return UA_STATUSCODE_BADOUTOFMEMORY;
        ret = decodeBinaryJumpTable[typeIndex](dst->data, dst->type, ctx);
    } else {
        ret = Variant_decodeBinaryUnwrapExtensionObject(dst, ctx);
    }

    /* Decode array dimensions */
    if(isArray && (encodingByte & UA_VARIANT_ENCODINGMASKTYPE_DIMENSIONS) > 0)
        ret |= Array_decodeBinary((void**)&dst->arrayDimensions, &dst->arrayDimensionsSize,
                                  &UA_TYPES[UA_TYPES_INT32], ctx);

    ctx->depth--;
    return ret;
}

/* DataValue */
ENCODE_BINARY(DataValue) {
    /* Set up the encoding mask */
    u8 encodingMask = (u8)
        (((u8)src->hasValue) |
         ((u8)src->hasStatus << 1) |
         ((u8)src->hasSourceTimestamp << 2) |
         ((u8)src->hasServerTimestamp << 3) |
         ((u8)src->hasSourcePicoseconds << 4) |
         ((u8)src->hasServerPicoseconds << 5));

    /* Encode the encoding byte */
    status ret = ENCODE_DIRECT(&encodingMask, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the variant. Afterwards, do not return
     * UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED, as the buffer might have been
     * exchanged during encoding of the variant. */
    if(src->hasValue) {
        ret = ENCODE_DIRECT(&src->value, Variant);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    if(src->hasStatus)
        ret |= ENCODE_WITHEXCHANGE(&src->status, UInt32);
    if(src->hasSourceTimestamp)
        ret |= ENCODE_WITHEXCHANGE(&src->sourceTimestamp, UInt64);
    if(src->hasSourcePicoseconds)
        ret |= ENCODE_WITHEXCHANGE(&src->sourcePicoseconds, UInt16);
    if(src->hasServerTimestamp)
        ret |= ENCODE_WITHEXCHANGE(&src->serverTimestamp, UInt64);
    if(src->hasServerPicoseconds)
        ret |= ENCODE_WITHEXCHANGE(&src->serverPicoseconds, UInt16);
    UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
    return ret;
}

#define MAX_PICO_SECONDS 9999

DECODE_BINARY(DataValue) {
    /* Decode the encoding mask */
    u8 encodingMask;
    status ret = DECODE_DIRECT(&encodingMask, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Check the recursion limit */
    if(ctx->depth > UA_ENCODING_MAX_RECURSION)
        return UA_STATUSCODE_BADENCODINGERROR;
    ctx->depth++;


    /* Decode the content */
    if(encodingMask & 0x01) {
        dst->hasValue = true;
        ret |= DECODE_DIRECT(&dst->value, Variant);
    }
    if(encodingMask & 0x02) {
        dst->hasStatus = true;
        ret |= DECODE_DIRECT(&dst->status, UInt32); /* StatusCode */
    }
    if(encodingMask & 0x04) {
        dst->hasSourceTimestamp = true;
        ret |= DECODE_DIRECT(&dst->sourceTimestamp, UInt64); /* DateTime */
    }
    if(encodingMask & 0x10) {
        dst->hasSourcePicoseconds = true;
        ret |= DECODE_DIRECT(&dst->sourcePicoseconds, UInt16);
        if(dst->sourcePicoseconds > MAX_PICO_SECONDS)
            dst->sourcePicoseconds = MAX_PICO_SECONDS;
    }
    if(encodingMask & 0x08) {
        dst->hasServerTimestamp = true;
        ret |= DECODE_DIRECT(&dst->serverTimestamp, UInt64); /* DateTime */
    }
    if(encodingMask & 0x20) {
        dst->hasServerPicoseconds = true;
        ret |= DECODE_DIRECT(&dst->serverPicoseconds, UInt16);
        if(dst->serverPicoseconds > MAX_PICO_SECONDS)
            dst->serverPicoseconds = MAX_PICO_SECONDS;
    }

    ctx->depth--;

    return ret;
}

/* DiagnosticInfo */
ENCODE_BINARY(DiagnosticInfo) {
    /* Set up the encoding mask */
    u8 encodingMask = (u8)
        ((u8)src->hasSymbolicId | ((u8)src->hasNamespaceUri << 1) |
        ((u8)src->hasLocalizedText << 2) | ((u8)src->hasLocale << 3) |
        ((u8)src->hasAdditionalInfo << 4) | ((u8)src->hasInnerDiagnosticInfo << 5));

    /* Encode the numeric content */
    status ret = ENCODE_DIRECT(&encodingMask, Byte);
    if(src->hasSymbolicId)
        ret |= ENCODE_DIRECT(&src->symbolicId, UInt32); /* Int32 */
    if(src->hasNamespaceUri)
        ret |= ENCODE_DIRECT(&src->namespaceUri, UInt32); /* Int32 */
    if(src->hasLocalizedText)
        ret |= ENCODE_DIRECT(&src->localizedText, UInt32); /* Int32 */
    if(src->hasLocale)
        ret |= ENCODE_DIRECT(&src->locale, UInt32); /* Int32 */
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Encode the additional info */
    if(src->hasAdditionalInfo) {
        ret = ENCODE_DIRECT(&src->additionalInfo, String);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    /* From here on, do not return UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED, as
     * the buffer might have been exchanged during encoding of the string. */

    /* Encode the inner status code */
    if(src->hasInnerStatusCode) {
        ret = ENCODE_WITHEXCHANGE(&src->innerStatusCode, UInt32);
        UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
        if(ret != UA_STATUSCODE_GOOD)
            return ret;
    }

    /* Encode the inner diagnostic info */
    if(src->hasInnerDiagnosticInfo)
        ret = encodeBinaryInternal(src->innerDiagnosticInfo, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO], ctx);

    UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
    return ret;
}

DECODE_BINARY(DiagnosticInfo) {
    /* Decode the encoding mask */
    u8 encodingMask;
    status ret = DECODE_DIRECT(&encodingMask, Byte);
    if(ret != UA_STATUSCODE_GOOD)
        return ret;

    /* Decode the content */
    if(encodingMask & 0x01) {
        dst->hasSymbolicId = true;
        ret |= DECODE_DIRECT(&dst->symbolicId, UInt32); /* Int32 */
    }
    if(encodingMask & 0x02) {
        dst->hasNamespaceUri = true;
        ret |= DECODE_DIRECT(&dst->namespaceUri, UInt32); /* Int32 */
    }
    if(encodingMask & 0x04) {
        dst->hasLocalizedText = true;
        ret |= DECODE_DIRECT(&dst->localizedText, UInt32); /* Int32 */
    }
    if(encodingMask & 0x08) {
        dst->hasLocale = true;
        ret |= DECODE_DIRECT(&dst->locale, UInt32); /* Int32 */
    }
    if(encodingMask & 0x10) {
        dst->hasAdditionalInfo = true;
        ret |= DECODE_DIRECT(&dst->additionalInfo, String);
    }
    if(encodingMask & 0x20) {
        dst->hasInnerStatusCode = true;
        ret |= DECODE_DIRECT(&dst->innerStatusCode, UInt32); /* StatusCode */
    }
    if(encodingMask & 0x40) {
        /* innerDiagnosticInfo is allocated on the heap */
        dst->innerDiagnosticInfo = (UA_DiagnosticInfo*)
            UA_calloc(1, sizeof(UA_DiagnosticInfo));
        if(!dst->innerDiagnosticInfo)
            return UA_STATUSCODE_BADOUTOFMEMORY;
        dst->hasInnerDiagnosticInfo = true;

        /* Check the recursion limit */
        if(ctx->depth > UA_ENCODING_MAX_RECURSION)
            return UA_STATUSCODE_BADENCODINGERROR;

        ctx->depth++;
        ret |= DECODE_DIRECT(dst->innerDiagnosticInfo, DiagnosticInfo);
        ctx->depth--;
    }
    return ret;
}

/********************/
/* Structured Types */
/********************/

const encodeBinarySignature encodeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1] = {
    (encodeBinarySignature)Boolean_encodeBinary,
    (encodeBinarySignature)Byte_encodeBinary, /* SByte */
    (encodeBinarySignature)Byte_encodeBinary,
    (encodeBinarySignature)UInt16_encodeBinary, /* Int16 */
    (encodeBinarySignature)UInt16_encodeBinary,
    (encodeBinarySignature)UInt32_encodeBinary, /* Int32 */
    (encodeBinarySignature)UInt32_encodeBinary,
    (encodeBinarySignature)UInt64_encodeBinary, /* Int64 */
    (encodeBinarySignature)UInt64_encodeBinary,
    (encodeBinarySignature)Float_encodeBinary,
    (encodeBinarySignature)Double_encodeBinary,
    (encodeBinarySignature)String_encodeBinary,
    (encodeBinarySignature)UInt64_encodeBinary, /* DateTime */
    (encodeBinarySignature)Guid_encodeBinary,
    (encodeBinarySignature)String_encodeBinary, /* ByteString */
    (encodeBinarySignature)String_encodeBinary, /* XmlElement */
    (encodeBinarySignature)NodeId_encodeBinary,
    (encodeBinarySignature)ExpandedNodeId_encodeBinary,
    (encodeBinarySignature)UInt32_encodeBinary, /* StatusCode */
    (encodeBinarySignature)QualifiedName_encodeBinary,
    (encodeBinarySignature)LocalizedText_encodeBinary,
    (encodeBinarySignature)ExtensionObject_encodeBinary,
    (encodeBinarySignature)DataValue_encodeBinary,
    (encodeBinarySignature)Variant_encodeBinary,
    (encodeBinarySignature)DiagnosticInfo_encodeBinary,
    (encodeBinarySignature)encodeBinaryInternal,
};

static status
encodeBinaryInternal(const void *src, const UA_DataType *type, Ctx *ctx) {
    /* Check the recursion limit */
    if(ctx->depth > UA_ENCODING_MAX_RECURSION)
        return UA_STATUSCODE_BADENCODINGERROR;
    ctx->depth++;

    uintptr_t ptr = (uintptr_t)src;
    status ret = UA_STATUSCODE_GOOD;
    u8 membersSize = type->membersSize;
    const UA_DataType *typelists[2] = { UA_TYPES, &type[-type->typeIndex] };
    for(size_t i = 0; i < membersSize && ret == UA_STATUSCODE_GOOD; ++i) {
        const UA_DataTypeMember *member = &type->members[i];
        const UA_DataType *membertype = &typelists[!member->namespaceZero][member->memberTypeIndex];
        if(!member->isArray) {
            ptr += member->padding;
            size_t encode_index = membertype->builtin ? membertype->typeIndex : UA_BUILTIN_TYPES_COUNT;
            size_t memSize = membertype->memSize;
            u8 *oldpos = ctx->pos;
            ret = encodeBinaryJumpTable[encode_index]((const void*)ptr, membertype, ctx);
            ptr += memSize;
            if(ret == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED) {
                ctx->pos = oldpos; /* exchange/send the buffer */
                ret = exchangeBuffer(ctx);
                ptr -= member->padding + memSize; /* encode the same member in the next iteration */
                if(ret == UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED || ctx->pos + memSize > ctx->end) {
                    /* the send buffer is too small to encode the member, even after exchangeBuffer */
                    ret = UA_STATUSCODE_BADRESPONSETOOLARGE;
                    break;
                }
                --i;
            }
        } else {
            ptr += member->padding;
            const size_t length = *((const size_t*)ptr);
            ptr += sizeof(size_t);
            ret = Array_encodeBinary(*(void *UA_RESTRICT const *)ptr, length, membertype, ctx);
            ptr += sizeof(void*);
        }
    }

    UA_assert(ret != UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED);
    ctx->depth--;
    return ret;
}

status
UA_encodeBinary(const void *src, const UA_DataType *type,
                u8 **bufPos, const u8 **bufEnd,
                UA_exchangeEncodeBuffer exchangeCallback, void *exchangeHandle) {
    /* Set up the context */
    Ctx ctx;
    ctx.pos = *bufPos;
    ctx.end = *bufEnd;
    ctx.depth = 0;
    ctx.exchangeBufferCallback = exchangeCallback;
    ctx.exchangeBufferCallbackHandle = exchangeHandle;

    /* Encode */
    status ret = encodeBinaryInternal(src, type, &ctx);

    /* Set the new buffer position for the output. Beware that the buffer might
     * have been exchanged internally. */
    *bufPos = ctx.pos;
    *bufEnd = ctx.end;
    return ret;
}

const decodeBinarySignature decodeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1] = {
    (decodeBinarySignature)Boolean_decodeBinary,
    (decodeBinarySignature)Byte_decodeBinary, /* SByte */
    (decodeBinarySignature)Byte_decodeBinary,
    (decodeBinarySignature)UInt16_decodeBinary, /* Int16 */
    (decodeBinarySignature)UInt16_decodeBinary,
    (decodeBinarySignature)UInt32_decodeBinary, /* Int32 */
    (decodeBinarySignature)UInt32_decodeBinary,
    (decodeBinarySignature)UInt64_decodeBinary, /* Int64 */
    (decodeBinarySignature)UInt64_decodeBinary,
    (decodeBinarySignature)Float_decodeBinary,
    (decodeBinarySignature)Double_decodeBinary,
    (decodeBinarySignature)String_decodeBinary,
    (decodeBinarySignature)UInt64_decodeBinary, /* DateTime */
    (decodeBinarySignature)Guid_decodeBinary,
    (decodeBinarySignature)String_decodeBinary, /* ByteString */
    (decodeBinarySignature)String_decodeBinary, /* XmlElement */
    (decodeBinarySignature)NodeId_decodeBinary,
    (decodeBinarySignature)ExpandedNodeId_decodeBinary,
    (decodeBinarySignature)UInt32_decodeBinary, /* StatusCode */
    (decodeBinarySignature)QualifiedName_decodeBinary,
    (decodeBinarySignature)LocalizedText_decodeBinary,
    (decodeBinarySignature)ExtensionObject_decodeBinary,
    (decodeBinarySignature)DataValue_decodeBinary,
    (decodeBinarySignature)Variant_decodeBinary,
    (decodeBinarySignature)DiagnosticInfo_decodeBinary,
    (decodeBinarySignature)decodeBinaryInternal
};

static status
decodeBinaryInternal(void *dst, const UA_DataType *type, Ctx *ctx) {
    /* Check the recursion limit */
    if(ctx->depth > UA_ENCODING_MAX_RECURSION)
        return UA_STATUSCODE_BADENCODINGERROR;
    ctx->depth++;

    uintptr_t ptr = (uintptr_t)dst;
    status ret = UA_STATUSCODE_GOOD;
    u8 membersSize = type->membersSize;
    const UA_DataType *typelists[2] = { UA_TYPES, &type[-type->typeIndex] };
    for(size_t i = 0; i < membersSize && ret == UA_STATUSCODE_GOOD; ++i) {
        const UA_DataTypeMember *member = &type->members[i];
        const UA_DataType *membertype = &typelists[!member->namespaceZero][member->memberTypeIndex];
        if(!member->isArray) {
            ptr += member->padding;
            size_t fi = membertype->builtin ? membertype->typeIndex : UA_BUILTIN_TYPES_COUNT;
            size_t memSize = membertype->memSize;
            ret |= decodeBinaryJumpTable[fi]((void *UA_RESTRICT)ptr, membertype, ctx);
            ptr += memSize;
        } else {
            ptr += member->padding;
            size_t *length = (size_t*)ptr;
            ptr += sizeof(size_t);
            ret |= Array_decodeBinary((void *UA_RESTRICT *UA_RESTRICT)ptr, length, membertype, ctx);
            ptr += sizeof(void*);
        }
    }

    ctx->depth--;
    return ret;
}

status
UA_decodeBinary(const UA_ByteString *src, size_t *offset, void *dst,
                const UA_DataType *type, size_t customTypesSize,
                const UA_DataType *customTypes) {
    /* Set up the context */
    Ctx ctx;
    ctx.pos = &src->data[*offset];
    ctx.end = &src->data[src->length];
    ctx.depth = 0;
    ctx.customTypesArraySize = customTypesSize;
    ctx.customTypesArray = customTypes;

    /* Decode */
    memset(dst, 0, type->memSize); /* Initialize the value */
    status ret = decodeBinaryInternal(dst, type, &ctx);

    if(ret == UA_STATUSCODE_GOOD) {
        /* Set the new offset */
        *offset = (size_t)(ctx.pos - src->data) / sizeof(u8);
    } else {
        /* Clean up */
        UA_deleteMembers(dst, type);
        memset(dst, 0, type->memSize);
    }
    return ret;
}

/**
 * Compute the Message Size
 * ------------------------
 * The following methods are used to compute the length of a datum in binary
 * encoding. */

static size_t
Array_calcSizeBinary(const void *src, size_t length, const UA_DataType *type) {
    size_t s = 4; /* length */
    if(type->overlayable) {
        s += type->memSize * length;
        return s;
    }
    uintptr_t ptr = (uintptr_t)src;
    size_t encode_index = type->builtin ? type->typeIndex : UA_BUILTIN_TYPES_COUNT;
    for(size_t i = 0; i < length; ++i) {
        s += calcSizeBinaryJumpTable[encode_index]((const void*)ptr, type);
        ptr += type->memSize;
    }
    return s;
}

static size_t
calcSizeBinaryMemSize(const void *UA_RESTRICT p, const UA_DataType *type) {
    return type->memSize;
}

CALCSIZE_BINARY(String) {
    return 4 + src->length;
}

CALCSIZE_BINARY(Guid) {
    return 16;
}

CALCSIZE_BINARY(NodeId) {
    size_t s = 1; /* encoding byte */
    switch(src->identifierType) {
    case UA_NODEIDTYPE_NUMERIC:
        if(src->identifier.numeric > UA_UINT16_MAX || src->namespaceIndex > UA_BYTE_MAX) {
            s += 6;
        } else if(src->identifier.numeric > UA_BYTE_MAX || src->namespaceIndex > 0) {
            s += 3;
        } else {
            s += 1;
        }
        break;
    case UA_NODEIDTYPE_BYTESTRING:
    case UA_NODEIDTYPE_STRING:
        s += 2;
        s += String_calcSizeBinary(&src->identifier.string, NULL);
        break;
    case UA_NODEIDTYPE_GUID:
        s += 18;
        break;
    default:
        return 0;
    }
    return s;
}

CALCSIZE_BINARY(ExpandedNodeId) {
    size_t s = NodeId_calcSizeBinary(&src->nodeId, NULL);
    if(src->namespaceUri.length > 0)
        s += String_calcSizeBinary(&src->namespaceUri, NULL);
    if(src->serverIndex > 0)
        s += 4;
    return s;
}

CALCSIZE_BINARY(QualifiedName) {
    return 2 + String_calcSizeBinary(&src->name, NULL);
}

CALCSIZE_BINARY(LocalizedText) {
    size_t s = 1; /* encoding byte */
    if(src->locale.data)
        s += String_calcSizeBinary(&src->locale, NULL);
    if(src->text.data)
        s += String_calcSizeBinary(&src->text, NULL);
    return s;
}

CALCSIZE_BINARY(ExtensionObject) {
    size_t s = 1; /* encoding byte */
    if(src->encoding > UA_EXTENSIONOBJECT_ENCODED_XML) {
        if(!src->content.decoded.type || !src->content.decoded.data)
            return 0;
        if(src->content.decoded.type->typeId.identifierType != UA_NODEIDTYPE_NUMERIC)
            return 0;
        s += NodeId_calcSizeBinary(&src->content.decoded.type->typeId, NULL);
        s += 4; /* length */
        const UA_DataType *type = src->content.decoded.type;
        size_t encode_index = type->builtin ? type->typeIndex : UA_BUILTIN_TYPES_COUNT;
        s += calcSizeBinaryJumpTable[encode_index](src->content.decoded.data, type);
    } else {
        s += NodeId_calcSizeBinary(&src->content.encoded.typeId, NULL);
        switch(src->encoding) {
        case UA_EXTENSIONOBJECT_ENCODED_NOBODY:
            break;
        case UA_EXTENSIONOBJECT_ENCODED_BYTESTRING:
        case UA_EXTENSIONOBJECT_ENCODED_XML:
            s += String_calcSizeBinary(&src->content.encoded.body, NULL);
            break;
        default:
            return 0;
        }
    }
    return s;
}

CALCSIZE_BINARY(Variant) {
    size_t s = 1; /* encoding byte */
    if(!src->type)
        return s;

    bool isArray = src->arrayLength > 0 || src->data <= UA_EMPTY_ARRAY_SENTINEL;
    bool hasDimensions = isArray && src->arrayDimensionsSize > 0;
    bool isBuiltin = src->type->builtin;


    size_t encode_index = src->type->typeIndex;
    if(!isBuiltin) {
        encode_index = UA_BUILTIN_TYPES_COUNT;
        if(src->type->typeId.identifierType != UA_NODEIDTYPE_NUMERIC)
            return 0;
    }

    uintptr_t ptr = (uintptr_t)src->data;
    size_t length = isArray ? src->arrayLength : 1;
    if (isArray)
        s += Array_calcSizeBinary((const void*)ptr, length, src->type);
    else
        s += calcSizeBinaryJumpTable[encode_index]((const void*)ptr, src->type);

    if (!isBuiltin) {
        /* The type is wrapped inside an extensionobject */
        /* (NodeId + encoding byte + extension object length) * array length */
        s += (NodeId_calcSizeBinary(&src->type->typeId, NULL) + 1 + 4) * length;
    }

    if(hasDimensions)
        s += Array_calcSizeBinary(src->arrayDimensions, src->arrayDimensionsSize,
                                  &UA_TYPES[UA_TYPES_INT32]);
    return s;
}

CALCSIZE_BINARY(DataValue) {
    size_t s = 1; /* encoding byte */
    if(src->hasValue)
        s += Variant_calcSizeBinary(&src->value, NULL);
    if(src->hasStatus)
        s += 4;
    if(src->hasSourceTimestamp)
        s += 8;
    if(src->hasSourcePicoseconds)
        s += 2;
    if(src->hasServerTimestamp)
        s += 8;
    if(src->hasServerPicoseconds)
        s += 2;
    return s;
}

CALCSIZE_BINARY(DiagnosticInfo) {
    size_t s = 1; /* encoding byte */
    if(src->hasSymbolicId)
        s += 4;
    if(src->hasNamespaceUri)
        s += 4;
    if(src->hasLocalizedText)
        s += 4;
    if(src->hasLocale)
        s += 4;
    if(src->hasAdditionalInfo)
        s += String_calcSizeBinary(&src->additionalInfo, NULL);
    if(src->hasInnerStatusCode)
        s += 4;
    if(src->hasInnerDiagnosticInfo)
        s += DiagnosticInfo_calcSizeBinary(src->innerDiagnosticInfo, NULL);
    return s;
}

const calcSizeBinarySignature calcSizeBinaryJumpTable[UA_BUILTIN_TYPES_COUNT + 1] = {
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Boolean */
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Byte */
    (calcSizeBinarySignature)calcSizeBinaryMemSize,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Int16 */
    (calcSizeBinarySignature)calcSizeBinaryMemSize,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Int32 */
    (calcSizeBinarySignature)calcSizeBinaryMemSize,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Int64 */
    (calcSizeBinarySignature)calcSizeBinaryMemSize,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Float */
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* Double */
    (calcSizeBinarySignature)String_calcSizeBinary,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* DateTime */
    (calcSizeBinarySignature)Guid_calcSizeBinary,
    (calcSizeBinarySignature)String_calcSizeBinary, /* ByteString */
    (calcSizeBinarySignature)String_calcSizeBinary, /* XmlElement */
    (calcSizeBinarySignature)NodeId_calcSizeBinary,
    (calcSizeBinarySignature)ExpandedNodeId_calcSizeBinary,
    (calcSizeBinarySignature)calcSizeBinaryMemSize, /* StatusCode */
    (calcSizeBinarySignature)QualifiedName_calcSizeBinary,
    (calcSizeBinarySignature)LocalizedText_calcSizeBinary,
    (calcSizeBinarySignature)ExtensionObject_calcSizeBinary,
    (calcSizeBinarySignature)DataValue_calcSizeBinary,
    (calcSizeBinarySignature)Variant_calcSizeBinary,
    (calcSizeBinarySignature)DiagnosticInfo_calcSizeBinary,
    (calcSizeBinarySignature)UA_calcSizeBinary
};

size_t
UA_calcSizeBinary(const void *p, const UA_DataType *type) {
    size_t s = 0;
    uintptr_t ptr = (uintptr_t)p;
    u8 membersSize = type->membersSize;
    const UA_DataType *typelists[2] = { UA_TYPES, &type[-type->typeIndex] };
    for(size_t i = 0; i < membersSize; ++i) {
        const UA_DataTypeMember *member = &type->members[i];
        const UA_DataType *membertype = &typelists[!member->namespaceZero][member->memberTypeIndex];
        if(!member->isArray) {
            ptr += member->padding;
            size_t encode_index = membertype->builtin ? membertype->typeIndex : UA_BUILTIN_TYPES_COUNT;
            s += calcSizeBinaryJumpTable[encode_index]((const void*)ptr, membertype);
            ptr += membertype->memSize;
        } else {
            ptr += member->padding;
            const size_t length = *((const size_t*)ptr);
            ptr += sizeof(size_t);
            s += Array_calcSizeBinary(*(void *UA_RESTRICT const *)ptr, length, membertype);
            ptr += sizeof(void*);
        }
    }
    return s;
}
