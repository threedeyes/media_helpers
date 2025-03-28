/*
 * Copyright 2025, Gerasim Troeglazov, 3dEyes@gmail.com
 * Distributed under the terms of the MIT License.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <scheduler.h>

#include "VideoConsumer.h"

VideoConsumer::VideoConsumer(const char* name, BMediaAddOn* addon,
		const uint32 internal_id)
	: BMediaNode(name),
	  BMediaEventLooper(),
	  BBufferConsumer(B_MEDIA_RAW_VIDEO),
	  fInternalID(internal_id),
	  fAddOn(addon),
	  fConnectionActive(false),
	  fMyLatency(3000),
	  fOurBuffers(false),
	  fBuffers(NULL),
	  fLastBufferIndex(-1),
      fFrameCallback(nullptr),
      fUserData(nullptr)
{
	AddNodeKind(B_PHYSICAL_OUTPUT);
	SetEventLatency(0);

	for (uint32 i = 0; i < kBufferCount; i++) {
		fBitmap[i] = NULL;
		fBufferMap[i] = NULL;
	}

	SetPriority(B_DISPLAY_PRIORITY);
}


VideoConsumer::~VideoConsumer()
{
	Quit();
	DeleteBuffers();
}


void
VideoConsumer::SetFrameCallback(FrameCallback callback, void* userData)
{
    fFrameCallback = callback;
    fUserData = userData;
}


BMediaAddOn*
VideoConsumer::AddOn(int32* cookie) const
{
	*cookie = fInternalID;
	return fAddOn;
}


void
VideoConsumer::NodeRegistered()
{
	fIn.destination.port = ControlPort();
	fIn.destination.id = 0;
	fIn.source = media_source::null;
	fIn.format.type = B_MEDIA_RAW_VIDEO;

	fIn.format.u.raw_video = media_raw_video_format::wildcard;
	fIn.format.u.raw_video.interlace = 1;
	fIn.format.u.raw_video.display.format = B_NO_COLOR_SPACE;
	fIn.format.u.raw_video.display.bytes_per_row = 0;
	fIn.format.u.raw_video.display.line_width = 0;
	fIn.format.u.raw_video.display.line_count = 0;

	Run();
}


status_t
VideoConsumer::RequestCompleted(const media_request_info& info)
{
	switch(info.what) {
		case media_request_info::B_SET_OUTPUT_BUFFERS_FOR:
			if (info.status != B_OK)
				fprintf(stderr, "VideoConsumer::RequestCompleted: Not using our buffers!\n");
			break;
		default:
			break;
	}
	return B_OK;
}


status_t
VideoConsumer::HandleMessage(int32 message, const void* data, size_t size)
{
	return B_OK;
}


void
VideoConsumer::BufferReceived(BBuffer* buffer)
{
	if (RunState() == B_STOPPED) {		
		buffer->Recycle();
		return;
	}
	media_timed_event event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER);
	EventQueue()->AddEvent(event);
}


void
VideoConsumer::ProducerDataStatus(const media_destination& forWhom,
	int32 status, bigtime_t atMediaTime)
{
	if (forWhom != fIn.destination)
		return;
}


status_t
VideoConsumer::CreateBuffers(const media_format& format)
{
	DeleteBuffers();	

	status_t status = B_OK;

	uint32 width = format.u.raw_video.display.line_width;
	uint32 height = format.u.raw_video.display.line_count;	
	color_space colorSpace = format.u.raw_video.display.format;

	fBuffers = new BBufferGroup();
	status = fBuffers->InitCheck();
	if (B_OK != status)
		return status;

	BRect bounds(0, 0, width - 1, height - 1);
	for (uint32 i = 0; i < kBufferCount; i++) {
		uint32 bitmapFlags = 0;
		bitmapFlags = B_BITMAP_IS_LOCKED;
			
		fBitmap[i] = new BBitmap(bounds, bitmapFlags, colorSpace);
		status = fBitmap[i]->InitCheck();
		if (status >= B_OK) {
			buffer_clone_info info;

			uint8* bits = (uint8*)fBitmap[i]->Bits();
			info.area = area_for(bits);
			area_info bitmapAreaInfo;
			status = get_area_info(info.area, &bitmapAreaInfo);
			if (status != B_OK) {
				fprintf(stderr, "VideoConsumer::CreateBuffers() - "
					"get_area_info(): %s\n", strerror(status));
				return status;
			}

			info.offset = bits - (uint8*)bitmapAreaInfo.address;
			info.size = (size_t)fBitmap[i]->BitsLength();
			info.flags = 0;
			info.buffer = 0;

			BBuffer* buffer = NULL;
			if ((status = fBuffers->AddBuffer(info, &buffer)) != B_OK) {
				fprintf(stderr, "VideoConsumer::CreateBuffers - ERROR ADDING BUFFER "
					"TO GROUP (%" B_PRId32 "): %s\n", i, strerror(status));
				return status;
			}
			fBufferMap[i] = buffer;
		} else {
			fprintf(stderr, "VideoConsumer::CreateBuffers - ERROR CREATING VIDEO RING "
				"BUFFER (Index %" B_PRId32 " Width %" B_PRId32 " Height %"
				B_PRId32 " Colorspace %d: %s\n", i, width, height, colorSpace,
				strerror(status));
			return status;
		}
	}

	return status;
}


void
VideoConsumer::DeleteBuffers()
{
	if (fBuffers) {
		fTargetLock.Lock();
		if (fLastBufferIndex >= 0) {
			fLastBufferIndex = -1;
		}
		fTargetLock.Unlock();

		delete fBuffers;
		fBuffers = NULL;

		for (uint32 i = 0; i < kBufferCount; i++) {
			snooze(20000);
			delete fBitmap[i];
			fBitmap[i] = NULL;
		}
	}
}


status_t
VideoConsumer::Connected(const media_source& producer,
	const media_destination& where, const media_format& format,
	media_input* outInput)
{
	fIn.source = producer;
	fIn.format = format;
	fIn.node = Node();
	sprintf(fIn.name, "Video Consumer");
	*outInput = fIn;

	uint32 userData = 0;
	int32 changeTag = 1;
	status_t ret = CreateBuffers(format);
	if (ret == B_OK) {
		ret = SetOutputBuffersFor(producer, fIn.destination, 
			fBuffers, &userData, &changeTag, true);
		if (ret != B_OK)
			fprintf(stderr, "SetOutputBuffersFor() failed: %s\n", strerror(ret));

		fIn.format.u.raw_video.display.bytes_per_row
			= fBitmap[0]->BytesPerRow();
	} else {
		fprintf(stderr, "VideoConsumer::Connected - COULDN'T CREATE BUFFERS\n");
		return ret;
	}

	*outInput = fIn;
	fConnectionActive = true;

	return B_OK;
}


void
VideoConsumer::Disconnected(const media_source& producer,
	const media_destination& where)
{
	if (where != fIn.destination || producer != fIn.source)
		return;

	int32 changeTag = 0;
	SetOutputBuffersFor(producer, fIn.destination, NULL, NULL, &changeTag,
		false);
	if (fOurBuffers) {
		status_t reclaimError = fBuffers->ReclaimAllBuffers();
		if (reclaimError != B_OK) {
			fprintf(stderr, "VideoConsumer::Disconnected() - Failed to "
				"reclaim our buffers: %s\n", strerror(reclaimError));
		}
	}

	fIn.source = media_source::null;
	fConnectionActive = false;

	_UnsetTargetBuffer();
}


status_t
VideoConsumer::AcceptFormat(const media_destination& dest, media_format* format)
{
	if (dest != fIn.destination) {
		fprintf(stderr, "VideoConsumer::AcceptFormat - BAD DESTINATION\n");
		return B_MEDIA_BAD_DESTINATION;	
	}
	
	if (format->type == B_MEDIA_NO_TYPE)
		format->type = B_MEDIA_RAW_VIDEO;
	
	if (format->type != B_MEDIA_RAW_VIDEO) {
		fprintf(stderr, "VideoConsumer::AcceptFormat - BAD FORMAT\n");
		return B_MEDIA_BAD_FORMAT;
	}

	if (format->u.raw_video.display.format
			!= media_raw_video_format::wildcard.display.format) {
		uint32 flags = 0;
		bool supported = bitmaps_support_space(
			format->u.raw_video.display.format, &flags);

		if (!supported) {
			fprintf(stderr, "AcceptFormat - unsupported color space for BBitmaps !\n");
			return B_MEDIA_BAD_FORMAT;
		}
		if (flags & B_VIEWS_SUPPORT_DRAW_BITMAP == 0) {
			fprintf(stderr, "AcceptFormat - BViews cannot draw bitmaps in given colorspace !\n");
			return B_MEDIA_BAD_FORMAT;
		}
	}

	return B_OK;
}


status_t
VideoConsumer::GetNextInput(int32* cookie, media_input* outInput)
{
	if (*cookie < 1) {
		fIn.node = Node();
		fIn.destination.id = *cookie;
		sprintf(fIn.name, "Video Consumer");
		*outInput = fIn;
		(*cookie)++;
		return B_OK;
	}
	return B_MEDIA_BAD_DESTINATION;
}


void
VideoConsumer::DisposeInputCookie(int32 /*cookie*/)
{
}


status_t
VideoConsumer::GetLatencyFor(const media_destination& whom,
	bigtime_t* _latency, media_node_id* _timeSource)
{
	if (whom != fIn.destination)
		return B_MEDIA_BAD_DESTINATION;
	
	*_latency = fMyLatency;
	*_timeSource = TimeSource()->ID();
	return B_OK;
}


status_t
VideoConsumer::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 fromChangeCount,
	const media_format& format)
{
	if (consumer != fIn.destination)
		return B_MEDIA_BAD_DESTINATION;

	if (producer != fIn.source)
		return B_MEDIA_BAD_SOURCE;

	fIn.format = format;

	return CreateBuffers(format);
}


void
VideoConsumer::HandleEvent(const media_timed_event* event, bigtime_t lateness,
	bool realTimeEvent)
{
	switch (event->type) {
		case BTimedEventQueue::B_START:
			_SetPerformanceTimeBase(event->event_time);
			break;
		case BTimedEventQueue::B_WARP:
		case BTimedEventQueue::B_SEEK:
			_SetPerformanceTimeBase(event->bigdata);
			break;
		case BTimedEventQueue::B_STOP:
			EventQueue()->FlushEvents(event->event_time, BTimedEventQueue::B_ALWAYS,
				true, BTimedEventQueue::B_HANDLE_BUFFER);
			_UnsetTargetBuffer();
			break;
		case BTimedEventQueue::B_HANDLE_BUFFER:
			_HandleBuffer(static_cast<BBuffer*>(event->pointer));
			break;
		default:
			fprintf(stderr, "VideoConsumer::HandleEvent - BAD EVENT\n");
			break;
	}			
}


void
VideoConsumer::_SetPerformanceTimeBase(bigtime_t performanceTime)
{
	fPerformanceTimeBase = performanceTime;
}


void
VideoConsumer::_HandleBuffer(BBuffer* buffer)
{
	if (RunState() != B_STARTED || !fConnectionActive) {
		buffer->Recycle();
		return;
	}

	uint32 index = 0;
	fOurBuffers = true;
	while (index < kBufferCount) {
		if (buffer->ID() == fBufferMap[index]->ID())
			break;
		else
			index++;
	}
	if (index == kBufferCount) {
		fOurBuffers = false;
		index = (fLastBufferIndex + 1) % kBufferCount;
	}

	bool recycle = true;
	bigtime_t now = TimeSource()->Now();

	if (!fOurBuffers) {
		memcpy(fBitmap[index]->Bits(), buffer->Data(),
			fBitmap[index]->BitsLength());
	}

	bigtime_t tooEarly = buffer->Header()->start_time - now;
	if (tooEarly > 3000)
		snooze(tooEarly);

    fTargetLock.Lock();

    if (fFrameCallback != nullptr)
        fFrameCallback(fBitmap[index], fUserData);

	if (fOurBuffers) {
		if (fLastBufferIndex >= 0)
			fBufferMap[fLastBufferIndex]->Recycle();
		recycle = false;
	}

	fLastBufferIndex = index;

	fTargetLock.Unlock();

	if (recycle)
		buffer->Recycle();
}


void
VideoConsumer::_UnsetTargetBuffer()
{
	fTargetLock.Lock();
	if (fLastBufferIndex >= 0) {
		if (fOurBuffers)
			fBufferMap[fLastBufferIndex]->Recycle();
		fLastBufferIndex = -1;
	}
	fTargetLock.Unlock();
}
