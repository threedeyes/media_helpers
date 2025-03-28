/*
 * Copyright 2025, Gerasim Troeglazov, 3dEyes@gmail.com
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>

#include <stdio.h>
#include <ostream>
#include <iostream>

#include "VideoConsumer.h"

void MyFrameCallback(BBitmap* frame, void* userData)
{
    if (frame) {
        std::cout << "Received frame: width=" << frame->Bounds().Width() + 1
                  << ", height=" << frame->Bounds().Height() + 1
                  << ", bytesPerRow=" << frame->BytesPerRow() << std::endl;

        int* myCounter = static_cast<int*>(userData);
        if (myCounter) {
            (*myCounter)++;
            std::cout << "Frame counter: " << *myCounter << std::endl;
        }

    } else {
        std::cerr << "Received null frame!" << std::endl;
    }
}


int32 main(int argc, char *argv[])
{
	BApplication app("application/x-vnd.my-video-capture-test");

	status_t fStatus;

	BMediaRoster *fMediaRoster = BMediaRoster::Roster(&fStatus);
	if (fStatus != B_OK) {
		printf("Can't find the media roster\n");
		return fStatus;
	}

	media_node fTimeSource;
	fStatus = fMediaRoster->GetTimeSource(&fTimeSource);
	if (fStatus != B_OK) {
		printf("Can't get a time source\n");
		return fStatus;
	}
	
	VideoConsumer *fVideoConsumer = new VideoConsumer("Capture video in", NULL, 0);
	
	int frameCounter = 0;
    fVideoConsumer->SetFrameCallback(MyFrameCallback, &frameCounter);
    
	printf("VideoConsumer: %ld\n", fVideoConsumer);

	media_node		fProducerNode;
	fStatus = fMediaRoster->GetVideoInput(&fProducerNode);
	if (fStatus != B_OK) {
		printf("Cannot find a video source. \n");
		return fStatus;
	}	
	
	fStatus = fMediaRoster->RegisterNode(fVideoConsumer);
	if (fStatus != B_OK) {
		printf("Can't register the video consumer\n");
		return fStatus;
	}

	media_node fConsumerNode = fVideoConsumer->Node();

	// find free producer output
	media_input videoInput;
	media_output videoOutput;
	int32 count = 1;
	fStatus = fMediaRoster->GetFreeOutputsFor(fProducerNode,
		&videoOutput, 1, &count, B_MEDIA_RAW_VIDEO);
	if (fStatus != B_OK || count < 1) {
		fStatus = B_RESOURCE_UNAVAILABLE;
		printf("Can't find an available video stream\n");
		return fStatus;
	}

	// find free consumer input
	count = 1;
	fStatus = fMediaRoster->GetFreeInputsFor(fConsumerNode,
		&videoInput, 1, &count, B_MEDIA_RAW_VIDEO);
	if (fStatus != B_OK || count < 1) {
		fStatus = B_RESOURCE_UNAVAILABLE;
		printf("Can't find an available connection to the video window");
		return fStatus;
	}
	
	// connect the nodes
	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	media_raw_video_format videoFormat = {
		30.0f, 1, 0,
		639,
		B_VIDEO_TOP_LEFT_RIGHT, 1, 1,
		{
			B_RGB32,
			(uint32)(640),
			(uint32)(480),
			0, 0, 0
		}
	};
	format.u.raw_video = videoFormat;
	
	// connect video producer to consumer (hopefully using overlays)
	fStatus = fMediaRoster->Connect(videoOutput.source, videoInput.destination,
		&format, &videoOutput, &videoInput);

	if (fStatus != B_OK) {
		printf("Can't connect the video source to the video window... \n");
		return fStatus;
	}
		
	BTimeSource* timeSource = fMediaRoster->MakeTimeSourceFor(fTimeSource);
	bigtime_t real = BTimeSource::RealTime();
	bigtime_t perf = timeSource->PerformanceTimeFor(real) + 1000000;
			
	// start the nodes
	fStatus = fMediaRoster->StartNode(fConsumerNode, perf);
	if (fStatus != B_OK) {
		printf("Can't start the video consumer\n");
		return fStatus;
	}
	fStatus = fMediaRoster->StartNode(fProducerNode, perf);
	if (fStatus != B_OK) {
		printf("Can't start the video producer\n");
		return fStatus;
	}

	snooze(5000000);

  	fMediaRoster->StopNode(fProducerNode, BTimeSource::RealTime(), true);
  	fMediaRoster->Disconnect(fProducerNode.node, videoOutput.source,
  		fConsumerNode.node, videoInput.destination);
  	fMediaRoster->UnregisterNode(fVideoConsumer);
  	delete fVideoConsumer;
}

