#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#pragma GCC visibility push(default)
#include <media/MediaAddOn.h>
#include <media/MediaDefs.h>
#include <media/MediaNode.h>
#include <media/MediaRecorder.h>
#include <media/MediaRoster.h>
#include <support/SupportDefs.h>
#include <support/String.h>
#include <support/Errors.h>
#pragma GCC visibility pop

class BMediaRoster;

typedef void (*AudioCallbackFunc)(const float* stereoData, size_t frameCount, void* userData);

class AudioCapture {
public:
    AudioCapture(AudioCallbackFunc callback = NULL,
                 void* userData = NULL,
                 float targetSampleRate = 0.0f,
                 const char* nodeName = "AudioCaptureClient");
    ~AudioCapture();

    status_t Start();
    status_t Stop();

    bool IsRunning() const { return mIsRecording; }
    status_t Status() const { return mLastStatus; }
    float DeviceSampleRate() const { return mDeviceSampleRate; }
    float TargetSampleRate() const { return mTargetSampleRate; }
    uint32 InputChannelCount() const { return mDeviceChannelCount; }
    uint32 InputFormatCode() const { return mDeviceMediaFormatCode; }
    const char* InputDeviceName() const { return mDeviceName.String(); }

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;
    AudioCapture(AudioCapture&&) = delete;
    AudioCapture& operator=(AudioCapture&&) = delete;

private:
    status_t initializeDevice();
    void cleanupMediaResources();
    void cleanupBuffers();
    void processData(void* data, size_t size, const media_raw_audio_format& format) noexcept;
    void linearResample(float* outBuffer, size_t& outFrameCount, const float* inBuffer, size_t inFrameCount);

    static void readCallbackC(void* cookie, bigtime_t timestamp, void* data, size_t size, const media_format& format) noexcept;
    static void notifyCallbackC(void* cookie, BMediaRecorder::notification code, ...);

    AudioCallbackFunc mUserCallback;
    void*             mUserData;
    BString           mNodeName;

    BMediaRoster*     mRoster;
    BMediaRecorder*   mRecorder;
    media_node        mAudioInputNode;
    media_format      mNegotiatedFormat;
    media_raw_audio_format mInputFormatDetails;

    float             mDeviceSampleRate;
    uint32            mDeviceChannelCount;
    uint32            mDeviceMediaFormatCode;
    BString           mDeviceName;

    bool              mIsInitialized;
    bool              mIsRecording;
    status_t          mLastStatus;

    float*            mDeviceFloatBuffer;
    size_t            mDeviceFloatBufferSize;
    float*            mResampledBuffer;
    size_t            mResampledBufferSize;

    float             mTargetSampleRate;
    double            mResamplingRatio;

    double            mInputBufferOffset;
};

#endif // AUDIO_CAPTURE_H
