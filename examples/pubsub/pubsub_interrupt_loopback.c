/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information. */

/**
 * **Trace point setup**
 *
 *             +--------------+                       +----------------+
 *         T1 | OPCUA PubSub |  T8                 T5 | OPCUA loopback |  T4
 *         |  |  Application |  ^                  |  |  Application   |  ^
 *         |  +--------------+  |                  |  +----------------+  |
 *  User   |  |              |  |                  |  |                |  |
 *  Space  |  |              |  |                  |  |                |  |
 *         |  |              |  |                  |  |                |  |
 *------------|--------------|------------------------|----------------|--------
 *         |  |              |  |                  |  |                |  |
 *  Kernel |  |              |  |                  |  |                |  |
 *  Space  |  |              |  |                  |  |                |  |
 *         |  |              |  |                  |  |                |  |
 *         v  +--------------+  |                  v  +----------------+  |
 *         T2 |  TX tcpdump  |  T7<----------------T6 |   RX tcpdump   |  T3
 *         |  +--------------+                        +----------------+  ^
 *         |                                                              |
 *         ----------------------------------------------------------------
 */

#include <signal.h>
#include <stdio.h>
#include <time.h>
/* For thread operations */
#include <pthread.h>

#include "ua_config_default.h"
#include "ua_network_pubsub_udp.h"
#include "ua_server.h"
#include "ua_server_internal.h"

/* These defines enables the publisher and subscriber of the OPCUA stack */
/* To run only publisher, enable PUBLISHER define alone (comment SUBSCRIBER) */
#define                      PUBLISHER
/* To run only subscriber, enable SUBSCRIBER define alone
 * (comment PUBLISHER) */
#define                      SUBSCRIBER
/* Use server interrupt or system interrupt? */
#define                      PUB_SYSTEM_INTERRUPT
/* Publish interval in milliseconds */
#define                      PUB_INTERVAL             250
/* Cycle time in ns. Eg: For 100us: 100*1000 */
#define                      CYCLE_TIME               100 * 1000
#define                      CYCLE_TIME_NINTY_PERCENT 0.9
#define                      SECONDS_SLEEP            2
#define                      NANO_SECONDS_SLEEP       999000000
#define                      NEXT_CYCLE_START_TIME    3
#define                      FIVE_MICRO_SECOND        5
#define                      MICRO_SECONDS            1000
#define                      MILLI_SECONDS            1000 * 1000
#define                      SECONDS                  1000 * 1000 * 1000
#define                      SECONDS_FIELD            1
#define                      TX_TIME_ONE              1
#define                      TX_TIME_ZERO             0
#define                      COUNTER_ZERO             0
/* Assign core affinity for threads */
#define                      CORE_TWO                 2
#define                      CORE_THREE               3
#define                      BUFFER_LENGTH            512
#define                      NETWORK_MSG_COUNT        1
#define                      MAX_MEASUREMENTS         10000000
#define                      FAILURE_EXIT             -1
#define                      CLOCKID                  CLOCK_REALTIME
#define                      SIG                      SIGUSR1
/* This is a hardcoded publisher IP address. If the IP address need to
 * be changed, change it in the below line.
 * If UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_INTERRUPT only is enabled,
 * Change in line number 323 in plugins/ua_network_pubsub_udp.c
 *
 * If UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_INTERRUPT and
 * UA_ENABLE_PUBSUB_CUSTOM_PUBLISH_INTERRUPT_TSN is enabled,
 * change multicast address as 224.0.0.22 in line number 75 and ip address in
 * line number 76 in plugins/ua_network_pubsub_udp_custom_interrupt.c
 */
#define                      PUBLISHER_IP_ADDRESS     "192.168.1.11"
#define                      DATA_SET_WRITER_ID       62541
#define                      KEY_FRAME_COUNT          10
/* Variable for next cycle start time */
struct timespec              nextCycleStartTime;
/* When the timer was created */
struct timespec              pubStartTime;
/* Set server running as true */
UA_Boolean                   running                = UA_TRUE;
/* Interval in ns */
UA_Int64                     pubIntervalNs;
/* Variables corresponding to PubSub connection creation,
 * published data set and writer group */
UA_PubSubConnection*         connection;
UA_NodeId                    connectionIdent;
UA_NodeId                    publishedDataSetIdent;
UA_NodeId                    writerGroupIdent;
UA_NodeId                    counterNodePublisher;
UA_NodeId                    counterNodeSubscriber;
/* Variable for PubSub callback */
UA_ServerCallback            pubCallback;
/* Variables for counter data handling in address space */
UA_UInt64                    counterDataPublisher   = 0;
UA_UInt64                    counterDataSubscriber  = 0;
UA_Variant                   countPointerPublisher;
UA_Variant                   countPointerSubscriber;
/* Variable for thread lock */
pthread_mutex_t              threadLock;

/* For adding nodes in the server information model */
static void addServerNodes(UA_Server* server);

#if defined(PUBLISHER)
/* File to store the data and timestamps for different traffic */
FILE*                        fpPublisher;
char*                        filePublishedData      = "publisher_T5.csv";

/* Thread for publisher */
pthread_t                    tidPublisher;

/* Array to store published counter data */
UA_UInt64                    publishCounterValue[MAX_MEASUREMENTS];
size_t                       measurementsPublisher  = 0;

/* Process scheduling parameter for publisher */
struct sched_param           schedParamPublisher;

/* Array to store timestamp */
struct timespec              publishTimestamp[MAX_MEASUREMENTS];
struct timespec              dataModificationTime;

/* Publisher thread routine for TBS */
void*                        publisherTBS(void* arg);
#endif

#if defined(SUBSCRIBER)
/* Variable for PubSub connection creation */
UA_NodeId                    connectionIdentSubscriber;

/* File to store the data and timestamps for different traffic */
FILE*                        fpSubscriber;
char*                        fileSubscribedData     = "subscriber_T4.csv";

/* Thread for subscriber */
pthread_t                    tidSubscriber;

/* Array to store subscribed counter data */
UA_UInt64                    subscribeCounterValue[MAX_MEASUREMENTS];
size_t                       measurementsSubscriber = 0;

/* Process scheduling parameter for subscriber */
struct sched_param           schedParamSubscriber;

/* Array to store timestamp */
struct timespec              subscribeTimestamp[MAX_MEASUREMENTS];
struct timespec              dataReceiveTime;

/* Subscriber thread routine */
void*                        subscriber(void* arg);

/* OPCUA Subscribe API */
void                         subscribe(void);
#endif

/* Stop signal */
static void stopHandler(int sign)
{
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = UA_FALSE;
}

#ifndef PUB_SYSTEM_INTERRUPT
/*****************************/
/* Server Event Loop Publish */
/*****************************/
/* Add a publishing callback function */
static void publishCallback(UA_Server* server, void* data)
{
    struct timespec start_time;
    struct timespec end_time;

    clock_gettime(CLOCKID, &start_time);
    pubCallback(server, data);
    clock_gettime(CLOCKID, &end_time);

    updateMeasurementsPublisher(start_time, end_time);
}

/* Add a callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_addRepeatedCallback(UA_Server* server,
                                     UA_ServerCallback callback,
                                     void* data, UA_UInt32 interval,
                                     UA_UInt64* callbackId)
{
    pubIntervalNs = interval * MILLI_SECONDS;
    pubCallback   = callback;
    clock_gettime(CLOCKID, &pubStartTime);
    return UA_Timer_addRepeatedCallback(&server->timer,
                                        (UA_TimerCallback)publishCallback,
                                        data, interval, callbackId);
}

/* Modify the interval of the callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_changeRepeatedCallbackInterval(UA_Server* server,
                                                UA_UInt64 callbackId,
                                                UA_UInt32 interval)
{
    pubIntervalNs = interval * MILLI_SECONDS;
    return UA_Timer_changeRepeatedCallbackInterval(&server->timer,
                                                   callbackId, interval);
}

/* Remove the callback added for cyclic repetition */
UA_StatusCode
UA_PubSubManager_removeRepeatedPubSubCallback(UA_Server* server,
                                              UA_UInt64 callbackId)
{
    return UA_Timer_removeRepeatedCallback(&server->timer, callbackId);
}
#else
/*****************************/
/* System Interrupt Callback */
/*****************************/

/* For one publish callback only... */
UA_Server*      pubServer;
void*           pubData;
struct sigevent pubEvent;
timer_t         pubEventTimer;

/* Singal handler */
static void handler(int sig, siginfo_t* si, void* uc)
{
    if (si->si_value.sival_ptr != &pubEventTimer)
    {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "stray signal");
        return;
    }

}

/* Add a callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_addRepeatedCallback(UA_Server* server,
                                     UA_ServerCallback callback,
                                     void* data, UA_UInt32 interval,
                                     UA_UInt64* callbackId)
{
    struct sigaction  signalAction;
    /* Arm the timer */
    struct itimerspec timerspec;
    int               resultTimerCreate = 0;
    pubServer                           = server;
    pubCallback                         = callback;
    pubData                             = data;
    pubIntervalNs                       = interval * MICRO_SECONDS;
    //pubIntervalNs                     = interval * MILLI_SECONDS;

    /* Handle the signal */
    signalAction.sa_flags               = SA_SIGINFO;
    signalAction.sa_sigaction           = handler;
    sigemptyset(&signalAction.sa_mask);
    sigaction(SIG, &signalAction, NULL);

    /* Create the event */
    pubEvent.sigev_notify               = SIGEV_NONE;
    pubEvent.sigev_signo                = SIG;
    pubEvent.sigev_value.sival_ptr      = &pubEventTimer;
    resultTimerCreate                   = timer_create(CLOCKID, &pubEvent,
                                                       &pubEventTimer);
    if (resultTimerCreate != 0)
    {
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Failed to create a system event");
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    timerspec.it_interval.tv_sec        = 0;
    timerspec.it_interval.tv_nsec       = pubIntervalNs;
    timerspec.it_value.tv_sec           = 0;
    timerspec.it_value.tv_nsec          = pubIntervalNs;
    resultTimerCreate                   = timer_settime(pubEventTimer, 0,
                                                        &timerspec, NULL);
    if (resultTimerCreate != 0)
    {
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Failed to arm the system timer");
        timer_delete(pubEventTimer);
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    clock_gettime(CLOCKID, &pubStartTime);
    return UA_STATUSCODE_GOOD;
}

/* Modify the interval of the callback for cyclic repetition */
UA_StatusCode
UA_PubSubManager_changeRepeatedCallbackInterval(UA_Server* server,
                                                UA_UInt64 callbackId,
                                                UA_UInt32 interval)
{
    struct itimerspec timerspec;
    int               resultTimerCreate = 0;
    pubIntervalNs                       = interval * MILLI_SECONDS;
    timerspec.it_interval.tv_sec        = 0;
    timerspec.it_interval.tv_nsec       = pubIntervalNs;
    timerspec.it_value.tv_sec           = 0;
    timerspec.it_value.tv_nsec          = pubIntervalNs;
    resultTimerCreate                   = timer_settime(pubEventTimer, 0,
                                                        &timerspec, NULL);
    if (resultTimerCreate != 0)
    {
        UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                       "Failed to arm the system timer");
        timer_delete(pubEventTimer);
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                "Changed the publish callback to interval %u", interval);
    return UA_STATUSCODE_GOOD;
}

/* Remove the callback added for cyclic repetition */
UA_StatusCode
UA_PubSubManager_removeRepeatedPubSubCallback(UA_Server* server,
                                              UA_UInt64 callbackId)
{
    timer_delete(pubEventTimer);
    return UA_STATUSCODE_GOOD;
}
#endif

#if defined(PUBLISHER)
/**
 * **PubSub connection handling**
 *
 * Create a new ConnectionConfig. The addPubSubConnection function takes the
 * config and create a new connection. The Connection identifier is
 * copied to the NodeId parameter.
 */
static void
addPubSubConnection(UA_Server* server)
{
    /* Details about the connection configuration and handling are located
     * in the pubsub connection tutorial */
    UA_PubSubConnectionConfig    connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name                          = UA_STRING
        ("UDP-UADP Connection 1");
    connectionConfig.transportProfileUri           = UA_STRING
        ("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    connectionConfig.enabled                       = UA_TRUE;
    UA_NetworkAddressUrlDataType networkAddressUrl = {UA_STRING
        (PUBLISHER_IP_ADDRESS), UA_STRING("opc.udp://224.0.0.22:4840/")};
    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    connectionConfig.publisherId.numeric           = UA_UInt32_random();
    UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);
}

/**
 * **PublishedDataSet handling**
 *
 * The PublishedDataSet (PDS) and PubSubConnection are the toplevel entities
 * and can exist alone. The PDS contains the collection of the published
 * fields. All other PubSub elements are directly or indirectly linked with
 * the PDS or connection.
 */
static void
addPublishedDataSet(UA_Server* server)
{
    /* The PublishedDataSetConfig contains all necessary public
    * informations for the creation of a new PublishedDataSet */
    UA_PublishedDataSetConfig publishedDataSetConfig;
    memset(&publishedDataSetConfig, 0, sizeof(UA_PublishedDataSetConfig));
    publishedDataSetConfig.publishedDataSetType    =
        UA_PUBSUB_DATASET_PUBLISHEDITEMS;
    publishedDataSetConfig.name                    = UA_STRING("Demo PDS");
    /* Create new PublishedDataSet based on the PublishedDataSetConfig. */
    UA_Server_addPublishedDataSet(server, &publishedDataSetConfig,
                                  &publishedDataSetIdent);
}

/**
 * **DataSetField handling**
 *
 * The DataSetField (DSF) is part of the PDS and describes exactly one
 * published field.
 */
static void
addDataSetField(UA_Server* server)
{
    /* Add a field to the previous created PublishedDataSet */
    UA_NodeId             dataSetFieldIdentCounter;
    UA_DataSetFieldConfig counterValue;
    memset(&counterValue, 0, sizeof(UA_DataSetFieldConfig));
    counterValue.dataSetFieldType                                   =
        UA_PUBSUB_DATASETFIELD_VARIABLE;
    counterValue.field.variable.fieldNameAlias                      = UA_STRING
        ("Counter Variable 1");
    counterValue.field.variable.promotedField                       = UA_FALSE;
    counterValue.field.variable.publishParameters.publishedVariable =
        counterNodePublisher;
    counterValue.field.variable.publishParameters.attributeId       =
        UA_ATTRIBUTEID_VALUE;
    UA_Server_addDataSetField(server, publishedDataSetIdent, &counterValue,
                              &dataSetFieldIdentCounter);
}

/**
 * **WriterGroup handling**
 *
 * The WriterGroup (WG) is part of the connection and contains the primary
 * configuration parameters for the message creation.
 */
static void
addWriterGroup(UA_Server* server)
{
    /* Now we create a new WriterGroupConfig and add the group to
     * the existing PubSubConnection. */
    UA_WriterGroupConfig writerGroupConfig;
    memset(&writerGroupConfig, 0, sizeof(UA_WriterGroupConfig));
    writerGroupConfig.name               = UA_STRING("Demo WriterGroup");
    writerGroupConfig.publishingInterval = PUB_INTERVAL;
    writerGroupConfig.enabled            = UA_FALSE;
    writerGroupConfig.encodingMimeType   = UA_PUBSUB_ENCODING_UADP;
    /* The configuration flags for the messages are encapsulated inside the
     * message- and transport settings extension objects. These extension
     * objects are defined by the standard.
     * e.g. UadpWriterGroupMessageDataType */
    UA_Server_addWriterGroup(server, connectionIdent, &writerGroupConfig,
                             &writerGroupIdent);
}

/**
 * **DataSetWriter handling**
 *
 * A DataSetWriter (DSW) is the glue between the WG and the PDS. The DSW is
 * linked to exactly one PDS and contains additional informations for the
 * message generation.
 */
static void
addDataSetWriter(UA_Server* server)
{
    /* We need now a DataSetWriter within the WriterGroup. This means we must
     * create a new DataSetWriterConfig and add call the
     * addWriterGroup function. */
    UA_NodeId              dataSetWriterIdent;
    UA_DataSetWriterConfig dataSetWriterConfig;
    memset(&dataSetWriterConfig, 0, sizeof(UA_DataSetWriterConfig));
    dataSetWriterConfig.name             = UA_STRING("Demo DataSetWriter");
    dataSetWriterConfig.dataSetWriterId  = DATA_SET_WRITER_ID;
    dataSetWriterConfig.keyFrameCount    = KEY_FRAME_COUNT;
    UA_Server_addDataSetWriter(server, writerGroupIdent, publishedDataSetIdent,
                               &dataSetWriterConfig, &dataSetWriterIdent);
}

/**
 * **Published data handling**
 *
 * The published data is updated in the array using this function
 */
static void
updateMeasurementsPublisher(struct timespec start_time,
                            UA_UInt64 counterValue)
{
    publishTimestamp[measurementsPublisher]        = start_time;
    publishCounterValue[measurementsPublisher]     = counterValue;
    measurementsPublisher++;
}

/**
 * **Nanosecond field handling**
 *
 * The nanoSecondFieldConversion function is used to
 * adjust the nanosecond field value.
 */
static void nanoSecondFieldConversion(struct timespec *timeSpecValue)
{
    /* Check if ns field is greater than '1 ns less than 1sec' */
    while (timeSpecValue->tv_nsec > (SECONDS - SECONDS_FIELD))
    {
        /* Move to next second and remove it from ns field */
        timeSpecValue->tv_sec                     += SECONDS_FIELD;
        timeSpecValue->tv_nsec                    -= SECONDS;
    }
}
/**
 * **Publisher thread routine**
 *
 * The publisherTBS function is the routine used by the publisher thread.
 * This routine publishes the data at a cycle time of 100us.
 */
void* publisherTBS(void* arg)
{
    struct timespec nextNanoSleepTime;
    struct timespec currentTime;
    UA_Int64        clockNanoSleep                 = 0;
    UA_Int32        txtime_reach                   = 0;

    /* Get current time and compute the next nanosleeptime */
    clock_gettime(CLOCKID, &nextNanoSleepTime);
    /* Variable to nano Sleep until 1ms before a 1 second boundary */
    nextNanoSleepTime.tv_sec                       += SECONDS_SLEEP;
    nextNanoSleepTime.tv_nsec                      = NANO_SECONDS_SLEEP;
    /* For spinloop until the second boundary is reached */
    clock_gettime(CLOCKID, &nextCycleStartTime);
    nextCycleStartTime.tv_sec                      += NEXT_CYCLE_START_TIME;
    nextCycleStartTime.tv_nsec                     = 0;
    clockNanoSleep                                 =
        clock_nanosleep(CLOCKID, TIMER_ABSTIME, &nextNanoSleepTime, NULL);

    while (txtime_reach == TX_TIME_ZERO)
    {
        clock_gettime(CLOCKID, &currentTime);
        if (currentTime.tv_sec == nextCycleStartTime.tv_sec)
        {
            txtime_reach                           = TX_TIME_ONE;
        }

    }

    txtime_reach = TX_TIME_ZERO;
    while (running)
    {
        /* TODO: For lower cycletimes, the value may have to be less than
         * 90% of cycle time */
        UA_Double cycleTimeValue                   =
            CYCLE_TIME_NINTY_PERCENT * (UA_Double)CYCLE_TIME;
        UA_NodeId currentNodeIdPublisher;
        nextNanoSleepTime.tv_nsec                  =
            nextCycleStartTime.tv_nsec + (__syscall_slong_t)cycleTimeValue;
        nanoSecondFieldConversion(&nextNanoSleepTime);
        clockNanoSleep                             =
            clock_nanosleep(CLOCKID, TIMER_ABSTIME, &nextNanoSleepTime, NULL);
        clock_gettime(CLOCKID, &dataModificationTime);
        UA_Variant_setScalar(&countPointerPublisher, &counterDataSubscriber,
                             &UA_TYPES[UA_TYPES_UINT64]);
        currentNodeIdPublisher                     =
            UA_NODEID_STRING(1, "PublisherCounter");
        UA_Server_writeValue(pubServer, currentNodeIdPublisher,
                             countPointerPublisher);
        /* Lock the code section */
        pthread_mutex_lock(&threadLock);
        pubCallback(pubServer, pubData);
        /* Unlock the code section */
        pthread_mutex_unlock(&threadLock);
        if (counterDataSubscriber > COUNTER_ZERO)
        {
            updateMeasurementsPublisher(dataModificationTime,
                                        counterDataSubscriber);
        }

        switch (clockNanoSleep)
        {
            case 0:
                txtime_reach                       = TX_TIME_ZERO;

                while (txtime_reach == TX_TIME_ZERO)
                {
                    clock_gettime(CLOCKID, &currentTime);
                    if (currentTime.tv_sec == nextCycleStartTime.tv_sec &&
                        currentTime.tv_nsec >
                        nextCycleStartTime.tv_nsec)
                    {
                        nextCycleStartTime.tv_nsec =
                                nextCycleStartTime.tv_nsec + (CYCLE_TIME);
                        nanoSecondFieldConversion(&nextCycleStartTime);
                        txtime_reach               = TX_TIME_ONE;
                    }
                    else if (currentTime.tv_sec > nextCycleStartTime.tv_sec)
                    {
                        nextCycleStartTime.tv_nsec = nextCycleStartTime.tv_nsec
                            + (CYCLE_TIME);
                        nanoSecondFieldConversion(&nextCycleStartTime);
                        txtime_reach               = TX_TIME_ONE;
                    }

                }
                // no break
        }
    }

    return (void*)NULL;
}

#endif

#if defined(SUBSCRIBER)
/**
 * **Subscribed data handling**
 *
 * The subscribed data is updated in the array using this function
 */
static void
updateMeasurementsSubscriber(struct timespec receive_time,
                             UA_UInt64 counterValue)
{
    subscribeTimestamp[measurementsSubscriber]     = receive_time;
    subscribeCounterValue[measurementsSubscriber]  = counterValue;
    measurementsSubscriber++;
}

/**
 * **Subscriber thread routine**
 *
 * The subscriber function is the routine used by the subscriber thread.
 */
void* subscriber(void* arg)
{
    while (running)
    {
        subscribe();
    }
    return (void*)NULL;
}

/**
 * **Subscription handling**
 *
 * The subscribe function is the function that is used by subscriber thread
 * routine.
 */
void subscribe(void)
{
    UA_ByteString buffer;
    if (UA_ByteString_allocBuffer(&buffer, BUFFER_LENGTH) != UA_STATUSCODE_GOOD)
    {
        UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                     "Message buffer allocation failed!");
        return;
    }

    /* Receive the message. Blocks for 5ms */
    UA_StatusCode retval                 =
        connection->channel->receive(connection->channel, &buffer, NULL,
                                     FIVE_MICRO_SECOND);
    if (retval != UA_STATUSCODE_GOOD || buffer.length == 0)
    {
        /* Workaround! Reset buffer length. Receive can set the length
         * to zero. Then the buffer is not deleted because no memory
         * allocation is assumed.
         * TODO: Return an error code in 'receive' instead of setting the
         *  buf length to zero. */
        buffer.length                    = BUFFER_LENGTH;
        UA_ByteString_deleteMembers(&buffer);
        return;
    }

    UA_NetworkMessage networkMessage;
    memset(&networkMessage, 0, sizeof(UA_NetworkMessage));
    size_t currentPosition               = 0;
    UA_NetworkMessage_decodeBinary(&buffer, &currentPosition,
                                   &networkMessage);
    UA_ByteString_deleteMembers(&buffer);
    /* Is this the correct message type? */
    if (networkMessage.networkMessageType != UA_NETWORKMESSAGE_DATASET)
    {
        goto cleanup;
    }

    /* At least one DataSetMessage in the NetworkMessage? */
    if (networkMessage.payloadHeaderEnabled &&
        networkMessage.payloadHeader.dataSetPayloadHeader.count <
        NETWORK_MSG_COUNT)
    {
        goto cleanup;
    }

    /* Is this a KeyFrame-DataSetMessage? */
    UA_DataSetMessage* dsm               =
        &networkMessage.payload.dataSetPayload.dataSetMessages[0];
    if (dsm->header.dataSetMessageType != UA_DATASETMESSAGE_DATAKEYFRAME)
    {
        goto cleanup;
    }

    /* Loop over the fields and print well-known content types */
    for (int fieldCount = 0; fieldCount <
         dsm->data.keyFrameData.fieldCount; fieldCount++)
    {
        UA_UInt64         counterBuffer;
        UA_NodeId         currentNodeIdSubscriber;
        const UA_DataType *currentType   =
            dsm->data.keyFrameData.dataSetFields[fieldCount].value.type;
        if (currentType == &UA_TYPES[UA_TYPES_UINT64])
        {
            /* Loopback the received data and store the same in address space */
            clock_gettime(CLOCKID, &dataReceiveTime);
        }
        else
        {
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Message content is not of type UInt64 ");
        }

        counterDataSubscriber            =
            *(UA_UInt64 *) dsm->data.keyFrameData.dataSetFields[0].value.data;
        counterBuffer                    = counterDataSubscriber;
        UA_Variant_setScalar(&countPointerSubscriber, &counterDataSubscriber,
                             &UA_TYPES[UA_TYPES_UINT64]);
        currentNodeIdSubscriber          = UA_NODEID_STRING(1,
                                                            "SubscriberCounter"
                                                           );
        /* Lock the code section */
        pthread_mutex_lock(&threadLock);
        UA_Server_writeValue(pubServer, currentNodeIdSubscriber,
                             countPointerSubscriber);
        /* Unlock the code section */
        pthread_mutex_unlock(&threadLock);
        updateMeasurementsSubscriber(dataReceiveTime, counterBuffer);
    }

    cleanup:
        UA_NetworkMessage_deleteMembers(&networkMessage);
}
#endif

/**
 * **Creation of nodes**
 *
 * The addServerNodes function is used to create the publisher and subscriber
 * nodes.
 */
static void addServerNodes(UA_Server* server)
{
    UA_NodeId             rttUseCaseID;
    UA_NodeId             newNodeId;
    UA_VariableAttributes publisherAttr;
    UA_VariableAttributes subscriberAttr;
    UA_UInt64             publishValue   = 0;
    UA_UInt64             subscribeValue = 0;
    UA_ObjectAttributes   rttUseCasettr  = UA_ObjectAttributes_default;
    rttUseCasettr.displayName            = UA_LOCALIZEDTEXT("en-US",
                                                            "RTT Use case");
    UA_Server_addObjectNode(server, UA_NODEID_NULL,
                            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                            UA_QUALIFIEDNAME(1, "RTT Use case"), UA_NODEID_NULL,
                            rttUseCasettr, NULL, &rttUseCaseID);

    publisherAttr                        = UA_VariableAttributes_default;
    UA_Variant_setScalar(&publisherAttr.value, &publishValue,
                         &UA_TYPES[UA_TYPES_UINT64]);
    publisherAttr.displayName            = UA_LOCALIZEDTEXT("en-US",
                                                            "Publisher Counter"
                                                           );
    newNodeId                            = UA_NODEID_STRING(1,
                                                            "PublisherCounter");
    UA_Server_addVariableNode(server, newNodeId, rttUseCaseID,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                              UA_QUALIFIEDNAME(1, "Publisher Counter"),
                              UA_NODEID_NULL, publisherAttr, NULL,
                              &counterNodePublisher);
    subscriberAttr                       = UA_VariableAttributes_default;
    UA_Variant_setScalar(&subscriberAttr.value, &subscribeValue,
                         &UA_TYPES[UA_TYPES_UINT64]);
    subscriberAttr.displayName           = UA_LOCALIZEDTEXT("en-US",
                                                            "Subscriber Counter"
                                                           );
    newNodeId                            = UA_NODEID_STRING(1,
                                                            "SubscriberCounter"
                                                           );
    UA_Server_addVariableNode(server, newNodeId, rttUseCaseID,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                              UA_QUALIFIEDNAME(1, "Subscriber Counter"),
                              UA_NODEID_NULL, subscriberAttr, NULL,
                              &counterNodeSubscriber);
}

/**
 * **Main Server code**
 *
 * The main function contains publisher and subscriber threads running in
 * parallel.
 */
int main(void)
{
    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_Int32         returnValue         = 0;
    UA_Int32         errorSetAffinity    = 0;
    UA_Server*       server;
    UA_StatusCode    retval              = UA_STATUSCODE_GOOD;
    UA_ServerConfig* config              =
        UA_ServerConfig_new_minimal(DATA_SET_WRITER_ID, NULL);
#if defined(PUBLISHER)
    fpPublisher                          = fopen(filePublishedData, "a");
#endif
#if defined(SUBSCRIBER)
    fpSubscriber                         = fopen(fileSubscribedData, "a");
#endif

#if defined(PUBLISHER) && defined(SUBSCRIBER)
    /* Details about the connection configuration and handling
     *  are located in the pubsub connection tutorial */
    config->pubsubTransportLayers        = (UA_PubSubTransportLayer *)
                                            UA_malloc(2 *
                                            sizeof(UA_PubSubTransportLayer));
#else
    config->pubsubTransportLayers        = (UA_PubSubTransportLayer *)
                                            UA_malloc(
                                            sizeof(UA_PubSubTransportLayer));
#endif
    if (!config->pubsubTransportLayers)
    {
        UA_ServerConfig_delete(config);
        return FAILURE_EXIT;
    }

    /* It is possible to use multiple PubSubTransportLayers on runtime.
     * The correct factory is selected on runtime by the standard defined
     * PubSub TransportProfileUri's.
     */
#if defined(PUBLISHER) && defined(SUBSCRIBER)
    config->pubsubTransportLayers[0]     = UA_PubSubTransportLayerUDPMP();
    config->pubsubTransportLayersSize++;
#else
    config->pubsubTransportLayers[1]     = UA_PubSubTransportLayerUDPMP();
    config->pubsubTransportLayersSize++;
#endif

#if defined(PUBLISHER) || defined(SUBSCRIBER)
    /* Server is the new OPCUA model which has
     *  both publisher and subscriber configuration */
    server                               = UA_Server_new(config);
    /* Add axis node and OPCUA pubsub client server counter nodes */
    addServerNodes(server);
#endif

#if defined(PUBLISHER)
    addPubSubConnection(server);
    addPublishedDataSet(server);
    addDataSetField(server);
    addWriterGroup(server);
    addDataSetWriter(server);
#endif

#if defined(SUBSCRIBER)
    /* Details about the connection configuration and handling are located
     * in the pubsub connection tutorial */
    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name                          = UA_STRING
        ("UDP-UADP Connection 2");
    connectionConfig.transportProfileUri           = UA_STRING
        ("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    connectionConfig.enabled                       = UA_TRUE;
    UA_NetworkAddressUrlDataType networkAddressUrl = {UA_STRING
        (PUBLISHER_IP_ADDRESS), UA_STRING("opc.udp://224.0.0.32:4840/")};
    UA_Variant_setScalar(&connectionConfig.address, &networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    connectionConfig.publisherId.numeric           = UA_UInt32_random();
    UA_Server_addPubSubConnection(server, &connectionConfig,
                                  &connectionIdentSubscriber);
    connection                                     =
        UA_PubSubConnection_findConnectionbyId(server,
                                               connectionIdentSubscriber);
    if (connection != NULL)
    {
        UA_StatusCode retValConnection             =
            connection->channel->regist(connection->channel, NULL);
        if (retValConnection != UA_STATUSCODE_GOOD)
        {
            printf("PubSub registration failed for the requested channel\n");
        }

    }

#endif
    /* Initialize the mutex variable to lock and unlock */
    if (pthread_mutex_init(&threadLock, NULL) != 0)
    {
        printf("\n Mutex init has failed");
    }
#if defined(PUBLISHER)
    /* Data structure for representing CPU for publisher */
    cpu_set_t cpusetPublisher;
    /* Return the ID for publisher thread */
    tidPublisher                         = pthread_self();
    /* Get maximum priority for publisher thread */
    schedParamPublisher.sched_priority   = sched_get_priority_max(SCHED_FIFO);
    /* Set the scheduling policy and parameters for publisher thread */
    returnValue                          =
        pthread_setschedparam(tidPublisher, SCHED_FIFO, &schedParamPublisher);
    if (returnValue != 0)
    {
        printf("pthread_setschedparam for publisher: failed\n");
    }

    printf("\npthread_setschedparam: publisher thread priority is %d \n",
           schedParamPublisher.sched_priority);
    /* Publisher thread is assigned to core 2 */
    CPU_ZERO(&cpusetPublisher);
    CPU_SET(CORE_TWO, &cpusetPublisher);
    errorSetAffinity                     =
        pthread_setaffinity_np(tidPublisher, sizeof(cpu_set_t),
                               &cpusetPublisher);
    if (errorSetAffinity != 0)
    {
        fprintf(stderr, "pthread_setaffinity_np for publisher: %s\n",
                strerror(errorSetAffinity));
        return FAILURE_EXIT;
    }

    /* Create the publisher thread */
    returnValue                          = pthread_create(&tidPublisher, NULL,
                                                          &publisherTBS, NULL);
    if (returnValue != 0)
    {
        printf("Publisher thread cannot be created\n");
    }

#endif
#if defined(SUBSCRIBER)
    /* Data structure for representing CPU for subscriber */
    cpu_set_t cpusetSubscriber;
    /* Return the ID for subscriber thread */
    tidSubscriber                        = pthread_self();
    /* Get maximum priority for subscriber thread */
    schedParamSubscriber.sched_priority  = sched_get_priority_max(SCHED_FIFO);
    /* Set the scheduling policy and parameters for subscriber thread */
    returnValue                          =
        pthread_setschedparam(tidSubscriber, SCHED_FIFO,
                              &schedParamSubscriber);
    if (returnValue != 0)
    {
        printf("pthread_setschedparam for subscriber: failed\n");
    }

    printf("\npthread_setschedparam: subscriber thread priority is %d \n",
           schedParamSubscriber.sched_priority);
    /* Subscriber thread is assigned to core 3 */
    CPU_ZERO(&cpusetSubscriber);
    CPU_SET(CORE_THREE, &cpusetSubscriber);
    errorSetAffinity                     =
        pthread_setaffinity_np(tidSubscriber, sizeof(cpu_set_t),
                               &cpusetSubscriber);
    if (errorSetAffinity != 0)
    {
        fprintf(stderr, "pthread_setaffinity_np for subscriber: %s\n",
                strerror(errorSetAffinity));
        return FAILURE_EXIT;
    }

    /* Create the subscriber thread */
    returnValue                          = pthread_create(&tidSubscriber, NULL,
                                                          &subscriber, NULL);
    if (returnValue != 0)
    {
        printf("Subscriber thread cannot be created\n");
    }

#endif

    /* Run the server */
    retval                               |= UA_Server_run(server, &running);
#if defined(PUBLISHER)
    /* Write the published data in the publisher_T1.csv file */
    size_t pubLoopVariable               = 0;
    for (pubLoopVariable = 0; pubLoopVariable < measurementsPublisher;
         pubLoopVariable++)
    {
        fprintf(fpPublisher, "%ld,%ld.%09ld\n",
                publishCounterValue[pubLoopVariable],
                publishTimestamp[pubLoopVariable].tv_sec,
                publishTimestamp[pubLoopVariable].tv_nsec);
    }
#endif
#if defined(SUBSCRIBER)
    /* Write the subscribed data in the subscriber_T8.csv file */
    size_t subLoopVariable               = 0;
    for (subLoopVariable = 0; subLoopVariable < measurementsSubscriber;
         subLoopVariable++)
    {
        fprintf(fpSubscriber, "%ld,%ld.%09ld\n",
                subscribeCounterValue[subLoopVariable],
                subscribeTimestamp[subLoopVariable].tv_sec,
                subscribeTimestamp[subLoopVariable].tv_nsec);
    }
#endif
    /* Delete the server created */
    UA_Server_delete(server);
    /* Delete the server configuration */
    UA_ServerConfig_delete(config);

#if defined(PUBLISHER)
    /* Close the publisher file pointer */
    fclose(fpPublisher);
#endif
#if defined(SUBSCRIBER)
    /* Close the subscriber file pointer */
    fclose(fpSubscriber);
#endif

    return (int)retval;
}
