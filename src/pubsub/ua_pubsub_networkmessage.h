/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2017 - 2018 Fraunhofer IOSB (Author: Tino Bischoff)
 */

#ifndef UA_NETWORKMESSAGE_H_
#define UA_NETWORKMESSAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ua_types.h"
#include "ua_types_generated.h"

/* DataSet Payload Header */
typedef struct {
    UA_Byte count;
    UA_UInt16* dataSetWriterIds;
} UA_DataSetPayloadHeader;

/* FieldEncoding Enum  */
typedef enum {
    UA_FIELDENCODING_VARIANT = 0, 
    UA_FIELDENCODING_RAWDATA = 1,
    UA_FIELDENCODING_DATAVALUE = 2
} UA_FieldEncoding;

/* DataSetMessage Type */
typedef enum {
    UA_DATASETMESSAGE_DATAKEYFRAME = 0,
    UA_DATASETMESSAGE_DATADELTAFRAME = 1,
    UA_DATASETMESSAGE_EVENT = 2, 
    UA_DATASETMESSAGE_KEEPALIVE = 3
} UA_DataSetMessageType;

/* DataSetMessage Header */
typedef struct {
    UA_Boolean dataSetMessageValid;
    UA_FieldEncoding fieldEncoding;
    UA_Boolean dataSetMessageSequenceNrEnabled;
    UA_Boolean timestampEnabled;
    UA_Boolean statusEnabled;
    UA_Boolean configVersionMajorVersionEnabled;
    UA_Boolean configVersionMinorVersionEnabled;
    UA_DataSetMessageType dataSetMessageType;
    UA_Boolean picoSecondsIncluded;
    UA_UInt16 dataSetMessageSequenceNr;
    UA_UtcTime timestamp;
    UA_UInt16 picoSeconds;
    UA_UInt16 status;
    UA_UInt32 configVersionMajorVersion;
    UA_UInt32 configVersionMinorVersion;
} UA_DataSetMessageHeader;

UA_StatusCode
UA_DataSetMessageHeader_encodeBinary(const UA_DataSetMessageHeader* src,
                                     UA_Byte **bufPos, const UA_Byte *bufEnd);

UA_StatusCode
UA_DataSetMessageHeader_decodeBinary(const UA_ByteString *src, size_t *offset,
                                     UA_DataSetMessageHeader* dst);

size_t
UA_DataSetMessageHeader_calcSizeBinary(const UA_DataSetMessageHeader* p);

/**
 * DataSetMessage
 * ^^^^^^^^^^^^^^ */

typedef struct {
    UA_UInt16 fieldCount;
    UA_DataValue* dataSetFields;
} UA_DataSetMessage_DataKeyFrameData;

typedef struct {
    UA_UInt16 fieldIndex;
    UA_DataValue fieldValue;
} UA_DataSetMessage_DeltaFrameField;

typedef struct {
    UA_UInt16 fieldCount;
    UA_DataSetMessage_DeltaFrameField* deltaFrameFields;
} UA_DataSetMessage_DataDeltaFrameData;

typedef struct {
    UA_DataSetMessageHeader header;
    union {
        UA_DataSetMessage_DataKeyFrameData keyFrameData;
        UA_DataSetMessage_DataDeltaFrameData deltaFrameData;
    } data;
} UA_DataSetMessage;

UA_StatusCode
UA_DataSetMessage_encodeBinary(const UA_DataSetMessage* src, UA_Byte **bufPos,
                               const UA_Byte *bufEnd);

UA_StatusCode
UA_DataSetMessage_decodeBinary(const UA_ByteString *src, size_t *offset,
                               UA_DataSetMessage* dst);

size_t
UA_DataSetMessage_calcSizeBinary(const UA_DataSetMessage* p);

void UA_DataSetMessage_free(const UA_DataSetMessage* p);

typedef struct {
    UA_UInt16* sizes;
    UA_DataSetMessage* dataSetMessages;
} UA_DataSetPayload;

typedef enum {
    UA_PUBLISHERDATATYPE_BYTE = 0,
    UA_PUBLISHERDATATYPE_UINT16 = 1,
    UA_PUBLISHERDATATYPE_UINT32 = 2,
    UA_PUBLISHERDATATYPE_UINT64 = 3,
    UA_PUBLISHERDATATYPE_STRING = 4
} UA_PublisherIdDatatype;

typedef enum {
    UA_NETWORKMESSAGE_DATASET = 0,
    UA_NETWORKMESSAGE_DISCOVERY_REQUEST = 1,
    UA_NETWORKMESSAGE_DISCOVERY_RESPONSE = 2
} UA_NetworkMessageType;

/**
 * UA_NetworkMessageGroupHeader
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
typedef struct {
    UA_Boolean writerGroupIdEnabled;
    UA_Boolean groupVersionEnabled;
    UA_Boolean networkMessageNumberEnabled;
    UA_Boolean sequenceNumberEnabled;
    UA_UInt16 writerGroupId;
    UA_UInt32 groupVersion; // spec: type "VersionTime"
    UA_UInt16 networkMessageNumber;
    UA_UInt16 sequenceNumber;
} UA_NetworkMessageGroupHeader;

/**
 * UA_NetworkMessageSecurityHeader
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
typedef struct {
    UA_Boolean networkMessageSigned;
    UA_Boolean networkMessageEncrypted;
    UA_Boolean securityFooterEnabled;
    UA_Boolean forceKeyReset;
    UA_UInt32 securityTokenId;      // spec: IntegerId
    UA_Byte nonceLength;
    UA_ByteString messageNonce;
    UA_UInt16 securityFooterSize;
} UA_NetworkMessageSecurityHeader;

/**
 * UA_NetworkMessage
 * ^^^^^^^^^^^^^^^^^ */
typedef struct {
    UA_Byte version;
    UA_Boolean publisherIdEnabled;
    UA_Boolean groupHeaderEnabled;
    UA_Boolean payloadHeaderEnabled;
    UA_PublisherIdDatatype publisherIdType;
    UA_Boolean dataSetClassIdEnabled;
    UA_Boolean securityEnabled;
    UA_Boolean timestampEnabled;
    UA_Boolean picosecondsEnabled;
    UA_Boolean chunkMessage;
    UA_Boolean promotedFieldsEnabled;
    UA_NetworkMessageType networkMessageType;
    union {
        UA_Byte publisherIdByte;
        UA_UInt16 publisherIdUInt16;
        UA_UInt32 publisherIdUInt32;
        UA_UInt64 publisherIdUInt64;
        UA_Guid publisherIdGuid;
        UA_String publisherIdString;
    } publisherId;
    UA_Guid dataSetClassId;

    UA_NetworkMessageGroupHeader groupHeader;

    union {
        UA_DataSetPayloadHeader dataSetPayloadHeader;
    } payloadHeader;
    
    UA_DateTime timestamp;
    UA_UInt16 picoseconds;
    UA_UInt16 promotedFieldsSize;
    UA_Variant* promotedFields;	/* BaseDataType */
    
    UA_NetworkMessageSecurityHeader securityHeader;

    union {
        UA_DataSetPayload dataSetPayload;
    } payload;
    
    UA_ByteString securityFooter;
    UA_ByteString signature;
} UA_NetworkMessage;

UA_StatusCode
UA_NetworkMessage_encodeBinary(const UA_NetworkMessage* src,
                               UA_Byte **bufPos, const UA_Byte *bufEnd);

UA_StatusCode
UA_NetworkMessage_decodeBinary(const UA_ByteString *src, size_t *offset,
                               UA_NetworkMessage* dst);

size_t
UA_NetworkMessage_calcSizeBinary(const UA_NetworkMessage* p);

void
UA_NetworkMessage_deleteMembers(UA_NetworkMessage* p);

void
UA_NetworkMessage_delete(UA_NetworkMessage* p);


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UA_NETWORKMESSAGE_H_ */
