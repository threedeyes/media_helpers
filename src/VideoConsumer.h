#ifndef VIDEO_CONSUMER_H
#define VIDEO_CONSUMER_H

#pragma GCC visibility push(default)
#include <kernel/OS.h>
#include <app/Roster.h>
#include <app/Application.h>
#include <interface/Bitmap.h>
#include <interface/View.h>
#include <media/Buffer.h>
#include <media/BufferGroup.h>
#include <media/BufferConsumer.h>
#include <media/MediaAddOn.h>
#include <media/MediaDefs.h>
#include <media/MediaEventLooper.h>
#include <media/MediaFile.h>
#include <media/MediaNode.h>
#include <media/MediaRecorder.h>
#include <media/MediaRoster.h>
#include <media/MediaTrack.h>
#include <media/TimeSource.h>
#include <storage/NodeInfo.h>
#include <support/Locker.h>
#include <support/String.h>
#pragma GCC visibility pop

class BBitmap;

static const unsigned int kBufferCount = 4;

typedef void (*FrameCallback)(BBitmap* frame, void* userData);

class VideoConsumer : public BMediaEventLooper, public BBufferConsumer {
public:
    VideoConsumer(const char* name, BMediaAddOn* addon, const uint32 internal_id);
    ~VideoConsumer();

public:
    void SetFrameCallback(FrameCallback callback, void* userData = nullptr);

public:
    virtual BMediaAddOn* AddOn(int32* cookie) const;

protected:
    virtual void NodeRegistered();
    virtual status_t RequestCompleted(const media_request_info& info);
    virtual status_t HandleMessage(int32 message, const void* data, size_t size);

    // BMediaEventLooper interface
protected:
    virtual void HandleEvent(const media_timed_event* event, bigtime_t lateness, bool realTimeEvent);

    // BBufferConsumer interface
public:
    virtual status_t AcceptFormat(const media_destination& dest, media_format* format);
    virtual status_t GetNextInput(int32* cookie, media_input* _input);
    virtual void DisposeInputCookie(int32 cookie);

protected:
    virtual void BufferReceived(BBuffer* buffer);

private:
    virtual void ProducerDataStatus(const media_destination& forWhom, int32 status, bigtime_t atMediaTime);
    virtual status_t GetLatencyFor(const media_destination& forWhom, bigtime_t* outLatency, media_node_id* outId);
    virtual status_t Connected(const media_source& producer, const media_destination& where, const media_format& withFormat, media_input* outInput);
    virtual void Disconnected(const media_source& producer, const media_destination& where);
    virtual status_t FormatChanged(const media_source& producer, const media_destination& consumer, int32 from_change_count, const media_format& format);

    // VideoConsumer
public:
    status_t CreateBuffers(const media_format& withFormat);
    void DeleteBuffers();

private:
    void _SetPerformanceTimeBase(bigtime_t performanceTime);
    void _HandleBuffer(BBuffer* buffer);
    void _UnsetTargetBuffer();

private:
    int32 fInternalID;
    BMediaAddOn* fAddOn;

    bool fConnectionActive;
    media_input fIn;
    bigtime_t fMyLatency;
    bigtime_t fPerformanceTimeBase;

    BBitmap* fBitmap[kBufferCount];
    bool fOurBuffers;
    BBufferGroup* fBuffers;
    BBuffer* fBufferMap[kBufferCount];

    BLocker fTargetLock;
    int32 fLastBufferIndex;

    FrameCallback fFrameCallback;
    void* fUserData;
};

#endif // VIDEO_CONSUMER_H
