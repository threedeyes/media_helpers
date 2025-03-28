#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "AudioCapture.h"

inline float int16ToFloat(int16 sample) { return sample / 32768.0f; }
inline float int8ToFloat(int8 sample) { return sample / 128.0f; }
inline float uint8ToFloat(uint8 sample) { return (static_cast<int16>(sample) - 128) / 128.0f; }
inline float int32ToFloat(int32 sample) { return sample / 2147483648.0f; }

inline size_t getSampleSize(uint32 formatCode)
{
     switch(formatCode) {
        case media_raw_audio_format::B_AUDIO_FLOAT: return sizeof(float);
        case media_raw_audio_format::B_AUDIO_INT:   return sizeof(int32);
        case media_raw_audio_format::B_AUDIO_SHORT: return sizeof(int16);
        case media_raw_audio_format::B_AUDIO_CHAR:  return sizeof(int8);
        case media_raw_audio_format::B_AUDIO_UCHAR: return sizeof(uint8);
        default: return 0;
    }
}


AudioCapture::AudioCapture(AudioCallbackFunc callback, void* userData, float targetSampleRate, const char* nodeName)
    : mUserCallback(callback),
      mUserData(userData),
      mNodeName(nodeName),
      mRoster(NULL),
      mRecorder(NULL),
      mDeviceSampleRate(0.0f),
      mDeviceChannelCount(0),
      mDeviceMediaFormatCode(0),
      mIsInitialized(false),
      mIsRecording(false),
      mLastStatus(B_NO_INIT),
      mDeviceFloatBuffer(NULL),
      mDeviceFloatBufferSize(0),
      mResampledBuffer(NULL),
      mResampledBufferSize(0),
      mTargetSampleRate(targetSampleRate),
      mResamplingRatio(1.0),
      mInputBufferOffset(0.0)
{
    mLastStatus = initializeDevice();
    if (mLastStatus == B_OK) {
        mIsInitialized = true;

        if (mTargetSampleRate > 0.0f && mDeviceSampleRate > 0.0f && fabsf(mTargetSampleRate - mDeviceSampleRate) > 1e-5) {
             mResamplingRatio = static_cast<double>(mDeviceSampleRate) / mTargetSampleRate;
        } else {
             mTargetSampleRate = 0.0f;
             mResamplingRatio = 1.0;
        }
    } else {
         fprintf(stderr, "AudioCapture: Error - Failed to initialize device: %s\n", strerror(mLastStatus));
    }
}


AudioCapture::~AudioCapture()
{
    Stop();
}


status_t
AudioCapture::initializeDevice()
{
    status_t status = B_OK;

    mRoster = BMediaRoster::CurrentRoster();
    if (!mRoster) {
        mRoster = BMediaRoster::Roster(&status);
        if (!mRoster || status != B_OK) {
            return status != B_OK ? status : B_ERROR;
        }
    }

    status = mRoster->GetAudioInput(&mAudioInputNode);
    if (status != B_OK)
    	return status;

    if (mAudioInputNode == media_node::null)
		return B_DEVICE_NOT_FOUND;

    live_node_info liveInfo;
    status = mRoster->GetLiveNodeInfo(mAudioInputNode, &liveInfo);
    if (status == B_OK)
    	mDeviceName.SetTo(liveInfo.name);

    media_output audioOutput;
    int32 count = 0;
    status = mRoster->GetFreeOutputsFor(mAudioInputNode, &audioOutput, 1, &count, B_MEDIA_RAW_AUDIO);
    if (status != B_OK || count < 1) {
        fprintf(stderr, "AudioCapture: Could not find a free raw audio output.\n");
        return (status != B_OK) ? status : B_ERROR;
    }

    mInputFormatDetails = audioOutput.format.u.raw_audio;
    mDeviceSampleRate = mInputFormatDetails.frame_rate;
    mDeviceChannelCount = mInputFormatDetails.channel_count;
    mDeviceMediaFormatCode = mInputFormatDetails.format;

    if (mDeviceSampleRate <= 0 || mDeviceChannelCount == 0 || getSampleSize(mDeviceMediaFormatCode) == 0) {
         fprintf(stderr, "AudioCapture: Warning - Got invalid audio format details from input device.\n");
    }

    if (mTargetSampleRate > 0.0f && mDeviceSampleRate > 0.0f && fabsf(mTargetSampleRate - mDeviceSampleRate) > 1e-5) {
        mResamplingRatio = static_cast<double>(mDeviceSampleRate) / mTargetSampleRate;
    } else {
        mTargetSampleRate = 0.0f;
        mResamplingRatio = 1.0;
    }

    return B_OK;
}


status_t
AudioCapture::Start()
{
    if (mIsRecording)
    	return B_OK;

    if (!mIsInitialized)
    	return mLastStatus;

     if (mRecorder != NULL) {
         fprintf(stderr, "AudioCapture: Warning - Recorder object already exists before start(). Cleaning up.\n");
         cleanupMediaResources();
         cleanupBuffers();
     }

    status_t status = B_OK;

    mRecorder = new BMediaRecorder(mNodeName.String(), B_MEDIA_RAW_AUDIO);
    if (!mRecorder) {
    	mLastStatus = B_NO_MEMORY;
    	return mLastStatus;
    }

    status = mRecorder->InitCheck();
    if (status != B_OK) {
        delete mRecorder;
        mRecorder = NULL;
        mLastStatus = status;
        fprintf(stderr, "AudioCapture: Failed to create BMediaRecorder: %s\n", strerror(status));
        return status;
    }

    status = mRecorder->SetHooks(readCallbackC, notifyCallbackC, this);
    if (status != B_OK) {
        delete mRecorder;
        mRecorder = NULL;
        mLastStatus = status;
        fprintf(stderr, "AudioCapture: Failed to set BMediaRecorder hooks: %s\n", strerror(status));
        return status;
    }

    media_output audioOutput;
    int32 count = 0;
    status = mRoster->GetFreeOutputsFor(mAudioInputNode, &audioOutput, 1, &count, B_MEDIA_RAW_AUDIO);
    if (status != B_OK || count < 1) {
         cleanupMediaResources();
         mLastStatus = (status != B_OK) ? status : B_ERROR;
         fprintf(stderr, "AudioCapture: Failed to find free output before connection: %s\n", strerror(mLastStatus));
         return mLastStatus;
    }

    media_format connectionFormat = audioOutput.format;
    connectionFormat.type = B_MEDIA_RAW_AUDIO;
    status = mRecorder->Connect(mAudioInputNode, &audioOutput, &connectionFormat);
    if (status != B_OK) {
        cleanupMediaResources();
        mLastStatus = status;
        fprintf(stderr, "AudioCapture: Failed to connect BMediaRecorder: %s\n", strerror(status));
        return status;
    }

    mNegotiatedFormat = mRecorder->AcceptedFormat();
    if (mNegotiatedFormat.type != B_MEDIA_RAW_AUDIO) {
         fprintf(stderr, "AudioCapture: Error - Connection resulted in non-raw-audio format!\n");
         Stop();
         mLastStatus = B_MISMATCHED_VALUES;
         return mLastStatus;
    }
    mInputFormatDetails = mNegotiatedFormat.u.raw_audio;

    float oldDeviceRate = mDeviceSampleRate;
    mDeviceSampleRate = mInputFormatDetails.frame_rate;
    mDeviceChannelCount = mInputFormatDetails.channel_count;
    mDeviceMediaFormatCode = mInputFormatDetails.format;

    if (fabsf(oldDeviceRate - mDeviceSampleRate) > 1e-5) {
        if (mTargetSampleRate > 0.0f && mDeviceSampleRate > 0.0f && fabsf(mTargetSampleRate - mDeviceSampleRate) > 1e-5) {
            mResamplingRatio = static_cast<double>(mDeviceSampleRate) / mTargetSampleRate;
        } else {
            mTargetSampleRate = 0.0f;
            mResamplingRatio = 1.0;
        }
    }

    mInputBufferOffset = 0.0;

    status = mRecorder->Start();
    if (status != B_OK) {
        Stop();
        mLastStatus = status;
        fprintf(stderr, "AudioCapture: Failed to start BMediaRecorder processing: %s\n", strerror(status));
        return status;
    }

    mIsRecording = true;
    mLastStatus = B_OK;
    return B_OK;
}


status_t
AudioCapture::Stop()
{
    mIsRecording = false;

    if (mRecorder) {
		cleanupMediaResources();
		cleanupBuffers();
		mLastStatus = B_OK;
		return mLastStatus;
    } else {
        cleanupBuffers();
    }

    return B_OK;
}


void
AudioCapture::cleanupMediaResources()
{
     if (!mRecorder)
     	return;

    mRecorder->Stop();
    if (mRecorder->IsConnected()) {
        mRecorder->Disconnect();
    }

    mRecorder->SetHooks(NULL, NULL, NULL);

    delete mRecorder;
    mRecorder = NULL;
}


void
AudioCapture::cleanupBuffers()
{
    free(mDeviceFloatBuffer);
    mDeviceFloatBuffer = NULL;
    mDeviceFloatBufferSize = 0;
    
    free(mResampledBuffer);
    mResampledBuffer = NULL;
    mResampledBufferSize = 0;
    mInputBufferOffset = 0.0;
}


void
AudioCapture::readCallbackC(void* cookie, bigtime_t /*timestamp*/, void* data, size_t size, const media_format& format) noexcept
{
    AudioCapture* self = static_cast<AudioCapture*>(cookie);
    if (self && self->mIsRecording && format.type == B_MEDIA_RAW_AUDIO) {
        self->processData(data, size, format.u.raw_audio);
    }
}

void
AudioCapture::notifyCallbackC(void* cookie, BMediaRecorder::notification code, ...)
{
     AudioCapture* self = static_cast<AudioCapture*>(cookie);
     if (!self)
     	return;

     switch (code) {
        case BMediaRecorder::B_WILL_STOP:
            if (self->mIsRecording)
                self->mIsRecording = false;
            break;
        default:
            break;
    }
}


void
AudioCapture::processData(void* data, size_t size, const media_raw_audio_format& inputFormat) noexcept
{
    if (!mUserCallback || size == 0 || !data)
    	return;

    // Get input buffer details
    const uint32 inputChannels = inputFormat.channel_count;
    const size_t bytesPerSample = getSampleSize(inputFormat.format);
    if (inputChannels == 0 || bytesPerSample == 0) {
        fprintf(stderr, "AudioCapture: processData - Invalid input format (channels=%u, sampleSize=%lu).\n", inputChannels, bytesPerSample);
        return;
    }
    const size_t inputFrameSize = inputChannels * bytesPerSample;
    const size_t inputFrameCount = size / inputFrameSize;

    if (inputFrameCount == 0)
    	return;

    // Ensure intermediate buffer is large enough
    const size_t deviceFloatChannels = 2; // Convert to stereo
    const size_t deviceFrameSize = deviceFloatChannels * sizeof(float);
    const size_t neededDeviceBufferSize = inputFrameCount * deviceFrameSize;

    if (mDeviceFloatBufferSize < neededDeviceBufferSize) {
        void* newBuffer = realloc(mDeviceFloatBuffer, neededDeviceBufferSize);
        if (!newBuffer) {
            fprintf(stderr, "AudioCapture: Failed to realloc device float buffer (size %lu)!\n", neededDeviceBufferSize);
            return;
        }
        mDeviceFloatBuffer = static_cast<float*>(newBuffer);
        mDeviceFloatBufferSize = neededDeviceBufferSize;
    }

    // Convert input data -> mDeviceFloatBuffer
    float* deviceFloatData = mDeviceFloatBuffer;
    const char* inputData = static_cast<const char*>(data);

    for (size_t i = 0; i < inputFrameCount; ++i) {
        float left = 0.0f, right = 0.0f;
        const void* framePtr = inputData + i * inputFrameSize;

        // Convert left channel
        if (inputChannels >= 1) {
            switch(inputFormat.format) {
                case media_raw_audio_format::B_AUDIO_FLOAT: left = static_cast<const float*>(framePtr)[0]; break;
                case media_raw_audio_format::B_AUDIO_INT:   left = int32ToFloat(reinterpret_cast<const int32*>(framePtr)[0]); break;
                case media_raw_audio_format::B_AUDIO_SHORT: left = int16ToFloat(reinterpret_cast<const int16*>(framePtr)[0]); break;
                case media_raw_audio_format::B_AUDIO_UCHAR: left = uint8ToFloat(reinterpret_cast<const uint8*>(framePtr)[0]); break;
                case media_raw_audio_format::B_AUDIO_CHAR:  left = int8ToFloat(reinterpret_cast<const int8*>(framePtr)[0]); break;
                default: left = 0.0f; break;
            }
        }
        // Convert right channel (or duplicate left for mono)
        if (inputChannels >= 2) {
             const void* sample2Ptr = static_cast<const char*>(framePtr) + bytesPerSample;
             switch(inputFormat.format) {
                case media_raw_audio_format::B_AUDIO_FLOAT: right = static_cast<const float*>(sample2Ptr)[0]; break;
                case media_raw_audio_format::B_AUDIO_INT:   right = int32ToFloat(reinterpret_cast<const int32*>(sample2Ptr)[0]); break;
                case media_raw_audio_format::B_AUDIO_SHORT: right = int16ToFloat(reinterpret_cast<const int16*>(sample2Ptr)[0]); break;
                case media_raw_audio_format::B_AUDIO_UCHAR: right = uint8ToFloat(reinterpret_cast<const uint8*>(sample2Ptr)[0]); break;
                case media_raw_audio_format::B_AUDIO_CHAR:  right = int8ToFloat(reinterpret_cast<const int8*>(sample2Ptr)[0]); break;
                 default: right = 0.0f; break;
            }
        } else {
            right = left; // Mono -> Stereo
        }        
        // Write stereo float frame
        deviceFloatData[i * deviceFloatChannels + 0] = left;
        deviceFloatData[i * deviceFloatChannels + 1] = right;
    }

    // Resample if needed
    bool resamplingNeeded = (mResamplingRatio != 1.0);
    if (resamplingNeeded) {
        size_t outputFramesAvailable = 0;

        // Estimate max output frames for buffer allocation
        size_t maxOutputFrames = static_cast<size_t>(ceil((inputFrameCount + mInputBufferOffset) / mResamplingRatio)) + 2; // Add padding
        size_t neededResampledBufferSize = maxOutputFrames * deviceFrameSize;

        // Ensure output buffer is large enough
        if (mResampledBufferSize < neededResampledBufferSize) {
             void* newBuffer = realloc(mResampledBuffer, neededResampledBufferSize);
             if (!newBuffer) {
                fprintf(stderr, "AudioCapture: Failed to realloc resampled buffer (size %lu)!\n", neededResampledBufferSize);
                return;
            }
            mResampledBuffer = static_cast<float*>(newBuffer);
            mResampledBufferSize = neededResampledBufferSize;
        }

        // Perform linear resampling
        linearResample(mResampledBuffer, outputFramesAvailable, mDeviceFloatBuffer, inputFrameCount);

        // Call user callback with RESAMPLED data
        if (mUserCallback && outputFramesAvailable > 0) {
            mUserCallback(mResampledBuffer, outputFramesAvailable, mUserData);
        }

    } else {
        // Resampling disabled, call user callback with data device rate
        if (mUserCallback && inputFrameCount > 0) {
            mUserCallback(mDeviceFloatBuffer, inputFrameCount, mUserData);
        }
    }
}


void
AudioCapture::linearResample(float* outBuffer, size_t& outFrameCount, const float* inBuffer, size_t inFrameCount)
{
    outFrameCount = 0;
    if (inFrameCount == 0)
    	return;

    // Current position in the input buffer, considering offset from previous buffer
    double currentInputPos = mInputBufferOffset;

    while (true) {
        // Calculate integer index and fractional part for interpolation
        size_t index1 = static_cast<size_t>(floor(currentInputPos));
        double alpha = currentInputPos - index1;

        // Check if we have enough input samples for interpolation
        // Need sample at index1 and index1 + 1
        if (index1 + 1 >= inFrameCount) {
             // Not enough data in the current input buffer to generate more output samples
            break;
        }

        // Get the two stereo sample pairs
        const float* sample1Pair = &inBuffer[index1 * 2];
        const float* sample2Pair = &inBuffer[(index1 + 1) * 2];

        // Perform linear interpolation for left and right channels
        float outL = static_cast<float>(sample1Pair[0] * (1.0 - alpha) + sample2Pair[0] * alpha);
        float outR = static_cast<float>(sample1Pair[1] * (1.0 - alpha) + sample2Pair[1] * alpha);

        outBuffer[outFrameCount * 2 + 0] = outL;
        outBuffer[outFrameCount * 2 + 1] = outR;
        outFrameCount++;

        currentInputPos += mResamplingRatio; // mResamplingRatio = InputRate / OutputRate
    }

    mInputBufferOffset = currentInputPos - inFrameCount;
    mInputBufferOffset = fmax(0.0, mInputBufferOffset);
}
