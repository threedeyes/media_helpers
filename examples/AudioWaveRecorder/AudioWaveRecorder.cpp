/*
 * Copyright 2025, Gerasim Troeglazov, 3dEyes@gmail.com
 * Distributed under the terms of the MIT License.
 */

#include "AudioCapture.h"

#include <Application.h>
#include <OS.h>
#include <Locker.h>

#include <vector>
#include <stdio.h>

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

static drwav gWavWriter;
static bool gIsWriterInitialized = false;
static BLocker gBufferLock("AudioBufferLock");
static std::vector<float> gAudioDataBuffer;


void
myAudioCallback(const float* stereoData, size_t frameCount, void* userData)
{
    if (gBufferLock.Lock()) {
        size_t samplesToAdd = frameCount * 2;
        try {
            gAudioDataBuffer.insert(gAudioDataBuffer.end(), stereoData, stereoData + samplesToAdd);
        } catch (const std::bad_alloc& e) {
            fprintf(stderr, "Memory allocation failed in callback: %s\n", e.what());
        }
        gBufferLock.Unlock();
    } else {
        fprintf(stderr, "Warning: Failed to lock buffer mutex in callback!\n");
    }
}


void
writeBufferedData()
{
    if (!gIsWriterInitialized)
    	return;

    std::vector<float> localBuffer;

    if (gBufferLock.Lock()) {
        if (!gAudioDataBuffer.empty()) {
            localBuffer.swap(gAudioDataBuffer);
        }
        gBufferLock.Unlock();
    } else {
         fprintf(stderr, "Warning: Failed to lock buffer mutex in writeBufferedData!\n");
         return;
    }

    if (!localBuffer.empty()) {
        size_t frameCount = localBuffer.size() / 2;
        if (frameCount > 0) {
            drwav_uint64 framesWritten = drwav_write_pcm_frames(&gWavWriter, frameCount, localBuffer.data());
            if (framesWritten != frameCount) {
                fprintf(stderr, "Error writing WAV data! framesWritten: %llu, expected: %zu\n", framesWritten, frameCount);
            }
        }
    }
}


int main() {
    BApplication app("application/x-vnd.my-audio-recorder-test");

	float fileSampleRate = 48000.0f;

    printf("Creating AudioCapture...\n");
    AudioCapture capture(myAudioCallback, 0, fileSampleRate, "Example");

    if (capture.Status() != B_OK) {
        fprintf(stderr, "Failed to initialize AudioCapture: %s\n", strerror(capture.Status()));
        return 1;
    }

    float deviceSampleRate = capture.DeviceSampleRate();
    uint32 inputChannels = capture.InputChannelCount();
    printf("Device Info - Rate: %.1f Hz, Input Channels: %u\n", deviceSampleRate, inputChannels);

    drwav_data_format format = {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = (drwav_uint32)fileSampleRate;
    format.bitsPerSample = 32;

    const char* outputFilename = "output.wav";
    if (!drwav_init_file_write(&gWavWriter, outputFilename, &format, NULL)) {
        fprintf(stderr, "Failed to initialize WAV writer for file: %s\n", outputFilename);
        return 1;
    }
    gIsWriterInitialized = true;
    printf("Initialized WAV writer for: %s\n", outputFilename);

    printf("Starting audio capture...\n");
    status_t startStatus = capture.Start();
    if (startStatus != B_OK) {
         fprintf(stderr, "Failed to start capture: %s\n", strerror(startStatus));
         drwav_uninit(&gWavWriter);
         gIsWriterInitialized = false;
         return 1;
    }

    printf("Capture running. Recording to %s for 10 seconds...\n", outputFilename);
    bigtime_t startTime = system_time();
    bigtime_t duration = 10 * 1000 * 1000;

    while (capture.IsRunning() && (system_time() - startTime < duration)) {
        writeBufferedData();
        snooze(100 * 1000);
    }

    printf("Stopping capture...\n");
    capture.Stop();

    printf("Writing remaining buffered data...\n");
    writeBufferedData();

    if (gIsWriterInitialized) {
        drwav_uninit(&gWavWriter);
        gIsWriterInitialized = false;
        printf("Finalized WAV file: %s\n", outputFilename);
    }

    return 0;
}
