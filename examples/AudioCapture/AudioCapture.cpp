/*
 * Copyright 2025, Gerasim Troeglazov, 3dEyes@gmail.com
 * Distributed under the terms of the MIT License.
 */

#include <vector>
#include <stdio.h>

#include <Application.h>

#include "AudioCapture.h"

void
myAudioCallback(const float* stereoData, size_t frameCount, void* userData)
{
    printf("Callback: Received %zu stereo frames. UserData ptr: %p\n", frameCount, userData);
}

int main()
{
    BApplication app("application/x-vnd.my-audio-capture-test");

    int myUserDataValue = 0;

    printf("Creating AudioCapture...\n");
    AudioCapture capture(myAudioCallback, &myUserDataValue);

    if (capture.Status() != B_OK) {
        fprintf(stderr, "Failed to initialize AudioCapture: %s\n", strerror(capture.Status()));
        return 1;
    }

    printf("Device Info - Name: [%s] Rate: %.1f Hz, Channels: %u, Format: 0x%x\n",
           capture.InputDeviceName(),
           capture.DeviceSampleRate(),
           capture.InputChannelCount(),
           capture.InputFormatCode());

    printf("Starting capture...\n");
    status_t startStatus = capture.Start();
    if (startStatus != B_OK) {
         fprintf(stderr, "Failed to start capture: %s\n", strerror(startStatus));
        return 1;
    }

    if (capture.IsRunning()) {
        printf("Capture running for 5 seconds...\n");
        snooze(1 * 1000 * 1000);
    } else {
         fprintf(stderr, "Capture did not start correctly.\n");
    }


    printf("Stopping capture...\n");
    capture.Stop();

    printf("Capture finished.\n");

    return 0;
}
