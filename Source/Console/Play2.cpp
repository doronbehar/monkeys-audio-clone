/**************************************************************************************************
Play with R128 Volume Level Normalization & Background Threading (WASAPI High Priority)
**************************************************************************************************/
#include <fstream>
#include <vector>
#include <string>
#include <future>
#include <iostream>
#include <algorithm>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include <tuple>
#include <cwctype>
#include <atomic>
#include <windows.h>
#include <mmsystem.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EXIT_ON_ERROR(hres, msg) \
    if (FAILED(hres)) { \
        std::cerr << msg << " (HRESULT: " << std::hex << hres << ")" << std::endl; \
        goto Exit; \
    }

template <class T>
inline void SafeRelease(T * *ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

struct CPlaybackFile
{
    int64 m_nBlocks;
    int64 m_nChannels;
    int64 m_nSampleRate;
    CSmartPtr<double> m_spData;
};

class CAudioPlayer
{
public:
    CAudioPlayer() : m_bPlaying(false), m_bStop(false) {}
    ~CAudioPlayer() {
        Stop();
    }

    // Starts playback asynchronously on a high-priority worker thread
    bool Play(const str_utfn * pFilename) {
        if (m_bPlaying) {
            Stop();
        }

        m_bStop = false;
        m_bPlaying = true;

        // Copy filename string to pass safely to the background thread
        std::wstring filenameCopy(pFilename);

        m_workerThread = std::thread([this, filenameCopy]() {
            WorkerThreadProc(filenameCopy);
            m_bPlaying = false;
        });

        return true;
    }

    // Stops playback cleanly and waits for the worker thread to finish
    void Stop() {
        if (!m_bPlaying) return;

        m_bStop = true;

        if (m_audioClient) {
            m_audioClient->Stop();
        }

        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }

        m_bPlaying = false;
    }

    bool IsPlaying() const {
        return m_bPlaying;
    }

private:
    std::thread m_workerThread;
    std::atomic<bool> m_bPlaying;
    std::atomic<bool> m_bStop;
    IAudioClient * m_audioClient = nullptr; // Tracked for immediate cancellation

    static int LoadFile(const str_utfn * pFilename, CPlaybackFile & File)
    {
        int nRetVal = APE_ERROR_UNDEFINED;
        try
        {
            int nFunctionRetVal = APE_ERROR_SUCCESS;

            CSmartPtr<IAPEDecompress> spAPEDecompress;
            spAPEDecompress.Assign(CreateIAPEDecompress(pFilename, &nFunctionRetVal, false, true, false));
            if (spAPEDecompress == APE_NULL || nFunctionRetVal != APE_ERROR_SUCCESS)
                throw(static_cast<intn>(nFunctionRetVal));

            int64 nBlockAlign = spAPEDecompress->GetInfo(IAPEInfo::APE_INFO_BLOCK_ALIGN);
            int64 nBitDepth = spAPEDecompress->GetInfo(IAPEInfo::APE_INFO_BITS_PER_SAMPLE);
            File.m_nSampleRate = spAPEDecompress->GetInfo(IAPEInfo::APE_INFO_SAMPLE_RATE);
            File.m_nBlocks = spAPEDecompress->GetInfo(IAPEInfo::APE_DECOMPRESS_TOTAL_BLOCKS);
            File.m_nChannels = spAPEDecompress->GetInfo(IAPEInfo::APE_INFO_CHANNELS);

            bool isInputFloat = false;
            if (spAPEDecompress->GetInfo(APE::IAPEDecompress::APE_INFO_FORMAT_FLAGS) & APE_FORMAT_FLAG_FLOATING_POINT)
                isInputFloat = true;

            CSmartPtr<unsigned char> spData(new unsigned char[static_cast<size_t>(File.m_nBlocks * nBlockAlign)]);
            int64 nBlocksRetrieved = 0;
            spAPEDecompress->GetData(spData.GetPtr(), File.m_nBlocks, &nBlocksRetrieved);
            if (nBlocksRetrieved != File.m_nBlocks)
                throw(APE_ERROR_UNDEFINED);

            File.m_spData.Assign(new double[File.m_nBlocks * File.m_nChannels]);
            const unsigned char * rawSrc = spData.GetPtr();
            double * decodedDouble = File.m_spData.GetPtr();

            if (nBitDepth == 8) {
                for (size_t i = 0; i < File.m_nBlocks * File.m_nChannels; ++i)
                    decodedDouble[i] = (static_cast<double>(rawSrc[i]) - 128.0) / 128.0;
            }
            else if (nBitDepth == 16) {
                const int16_t * p16 = reinterpret_cast<const int16_t *>(rawSrc);
                for (size_t i = 0; i < File.m_nBlocks * File.m_nChannels; ++i)
                    decodedDouble[i] = static_cast<double>(p16[i]) / 32768.0;
            }
            else if (nBitDepth == 24) {
                for (size_t i = 0; i < File.m_nBlocks * File.m_nChannels; ++i) {
                    const uint8_t * p24 = rawSrc + i * 3;
                    int32_t val = (p24[0] | (p24[1] << 8) | (p24[2] << 16));
                    if (val & 0x800000) val |= static_cast<int32_t>(0xFF000000);
                    decodedDouble[i] = static_cast<double>(val) / 8388608.0;
                }
            }
            else if (nBitDepth == 32) {
                if (isInputFloat) {
                    const float * pFloat = reinterpret_cast<const float *>(rawSrc);
                    for (size_t i = 0; i < File.m_nBlocks * File.m_nChannels; ++i)
                        decodedDouble[i] = static_cast<double>(pFloat[i]);
                }
                else {
                    const int32_t * pInt32 = reinterpret_cast<const int32_t *>(rawSrc);
                    for (size_t i = 0; i < File.m_nBlocks * File.m_nChannels; ++i)
                        decodedDouble[i] = static_cast<double>(pInt32[i]) / 2147483648.0;
                }
            }

            // --- TRIM NEAR-SILENCE FROM START AND END ---
            const double silenceThreshold = 0.0005; // ~ -66 dB
            int64 numBlocks = File.m_nBlocks;
            int64 channels = File.m_nChannels;
            double * data = File.m_spData.GetPtr();

            int64 startBlock = 0;
            while (startBlock < numBlocks) {
                bool isSilent = true;
                for (int64 ch = 0; ch < channels; ++ch) {
                    if (std::abs(data[startBlock * channels + ch]) > silenceThreshold) {
                        isSilent = false;
                        break;
                    }
                }
                if (!isSilent) break;
                startBlock++;
            }

            int64 endBlock = numBlocks - 1;
            while (endBlock > startBlock) {
                bool isSilent = true;
                for (int64 ch = 0; ch < channels; ++ch) {
                    if (std::abs(data[endBlock * channels + ch]) > silenceThreshold) {
                        isSilent = false;
                        break;
                    }
                }
                if (!isSilent) break;
                endBlock--;
            }

            if (startBlock > 0 || endBlock < numBlocks - 1) {
                if (startBlock <= endBlock) {
                    int64 newBlocks = (endBlock - startBlock) + 1;
                    double * pNewData = new double[newBlocks * channels];
                    std::copy(data + startBlock * channels, data + (endBlock + 1) * channels, pNewData);
                    File.m_spData.Assign(pNewData);
                    File.m_nBlocks = newBlocks;
                }
            }

            nRetVal = APE_ERROR_SUCCESS;
        }
        catch (...)
        {
            nRetVal = APE_ERROR_UNDEFINED;
        }

        return nRetVal;
    }

    static std::wstring GetDirectoryPath(const std::wstring & path) {
        size_t found = path.find_last_of(L"/\\");
        if (found == std::wstring::npos) return L"";
        return path.substr(0, found + 1);
    }

    static bool IsM3uPlaylist(const wchar_t * filename) {
        if (!filename) return false;
        std::wstring p(filename);
        if (p.length() >= 5) {
            std::wstring ext5 = p.substr(p.length() - 5);
            std::transform(ext5.begin(), ext5.end(), ext5.begin(), towlower);
            if (ext5 == L".m3u8") return true;
        }
        if (p.length() >= 4) {
            std::wstring ext4 = p.substr(p.length() - 4);
            std::transform(ext4.begin(), ext4.end(), ext4.begin(), towlower);
            if (ext4 == L".m3u") return true;
        }
        return false;
    }

    static bool IsApeFile(const wchar_t * filename) {
        if (!filename) return false;
        std::wstring p(filename);
        if (p.length() >= 4) {
            std::wstring ext4 = p.substr(p.length() - 4);
            std::transform(ext4.begin(), ext4.end(), ext4.begin(), towlower);
            if (ext4 == L".ape") return true;
        }
        return false;
    }

    static std::vector<std::wstring> LoadM3uPlaylist(const wchar_t * playlistPath) {
        std::vector<std::wstring> files;
        std::wifstream file(playlistPath);
        if (!file.is_open()) {
            return files;
        }

        std::wstring baseDir = GetDirectoryPath(playlistPath);
        std::wstring line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            if (line.empty() || line[0] == L'#') {
                continue;
            }

            bool isAbsolute = (line.length() >= 2 && line[1] == L':') ||
                (!line.empty() && (line[0] == L'/' || line[0] == L'\\'));

            std::wstring resolvedPath = isAbsolute ? line : (baseDir + line);

            if (IsApeFile(resolvedPath.c_str())) {
                files.push_back(resolvedPath);
            }
        }
        return files;
    }

    static double ParseR128Value(const char * pTagStr) {
        if (!pTagStr) return 0.0;
        try {
            std::string s(pTagStr);
            size_t dbPos = s.find("dB");
            if (dbPos != std::string::npos) {
                s = s.substr(0, dbPos);
            }
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](wchar_t ch) { return !std::iswspace(ch); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](wchar_t ch) { return !std::iswspace(ch); }).base(), s.end());
            if (s.empty()) return 0.0;
            return std::stod(s);
        }
        catch (...) {
            return 0.0;
        }
    }

    void WorkerThreadProc(const std::wstring & filename) {
        // Elevate thread priority to High for minimal audio latency & buffer stability
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        const wchar_t * pFilename = filename.c_str();

        // Initialize COM Library for the worker thread
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            std::cerr << "Failed to initialize COM on worker thread." << std::endl;
            return;
        }

        IMMDeviceEnumerator * pEnumerator = nullptr;
        IMMDevice * pDevice = nullptr;
        IAudioRenderClient * pRenderClient = nullptr;
        ::WAVEFORMATEX * pwfx = nullptr;
        std::shared_ptr<CPlaybackFile> pCurrFile;

        // 1. Resolve Playlist vs Single APE File
        std::vector<std::wstring> playlistFiles;
        if (IsM3uPlaylist(pFilename)) {
            playlistFiles = LoadM3uPlaylist(pFilename);
            if (playlistFiles.empty()) {
                std::cerr << "Failed to load M3U playlist or playlist contains no valid APE tracks." << std::endl;
                goto Cleanup;
            }
            std::cout << "Loaded M3U playlist with " << playlistFiles.size() << " valid APE tracks." << std::endl;
        }
        else if (IsApeFile(pFilename)) {
            playlistFiles.push_back(pFilename);
        }
        else {
            std::cerr << "Invalid input file. Only M3U/M3U8 playlists or APE audio files are supported." << std::endl;
            goto Cleanup;
        }

        // --- PRE-SCAN R128 VOLUME LEVELS ACROSS ALL FILES ---
        std::vector<double> r128Values(playlistFiles.size(), 0.0);
        for (size_t i = 0; i < playlistFiles.size(); ++i) {
            if (m_bStop) goto Cleanup;
            int nFuncRet = APE_ERROR_SUCCESS;
            CSmartPtr<IAPEDecompress> spDecompress;
            spDecompress.Assign(CreateIAPEDecompress(playlistFiles[i].c_str(), &nFuncRet, false, false, false));
            if (spDecompress != APE_NULL && nFuncRet == APE_ERROR_SUCCESS) {
                IAPETagField * pField = spDecompress->GetTag()->GetTagField(L"Volume Level (R128)");
                if (pField && pField->GetFieldValue()) {
                    r128Values[i] = ParseR128Value(pField->GetFieldValue());
                }
            }
        }

        double maxR128 = *std::max_element(r128Values.begin(), r128Values.end());
        std::cout << "R128 Zero Point Reference: " << maxR128 << " dB" << std::endl;

        std::vector<double> gainMultipliers(playlistFiles.size(), 1.0);
        for (size_t i = 0; i < playlistFiles.size(); ++i) {
            double diffDb = maxR128 - r128Values[i];
            gainMultipliers[i] = std::pow(10.0, diffDb / 20.0);
            std::cout << "Track " << (i + 1) << " R128: " << r128Values[i] << " dB | Diff: " << diffDb << " dB | Gain Multiplier: " << gainMultipliers[i] << std::endl;
        }

        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), NULL,
            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
            (void **)&pEnumerator
        );
        EXIT_ON_ERROR(hr, "Failed to create MMDeviceEnumerator.");

        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        EXIT_ON_ERROR(hr, "Failed to get default audio endpoint.");

        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&m_audioClient);
        EXIT_ON_ERROR(hr, "Failed to activate Audio Client.");

        hr = m_audioClient->GetMixFormat(&pwfx);
        EXIT_ON_ERROR(hr, "Failed to get device mix format.");

        pCurrFile = std::make_shared<CPlaybackFile>();
        int firstLoadRes = LoadFile(playlistFiles[0].c_str(), *pCurrFile);
        if (firstLoadRes != APE_ERROR_SUCCESS) {
            std::cerr << "Failed to load the first track." << std::endl;
            goto Exit;
        }

        // --- FORMAT NEGOTIATION ---
        bool formatFound = false;
        WAVEFORMATEXTENSIBLE chosenWfx = {};
        AUDCLNT_SHAREMODE shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;

        DWORD fileRate = static_cast<DWORD>(pCurrFile->m_nSampleRate > 0 ? pCurrFile->m_nSampleRate : 44100);
        WORD fileChannels = static_cast<WORD>(pCurrFile->m_nChannels > 0 ? pCurrFile->m_nChannels : 2);

        struct FormatCandidate {
            WORD bits;
            WORD validBits;
            GUID subFormat;
        };

        FormatCandidate candidates[] = {
            { 32, 32, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT },
            { 32, 24, KSDATAFORMAT_SUBTYPE_PCM },
            { 16, 16, KSDATAFORMAT_SUBTYPE_PCM }
        };

        for (const auto & cand : candidates) {
            WAVEFORMATEXTENSIBLE testWfx = {};
            testWfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            testWfx.Format.nChannels = fileChannels;
            testWfx.Format.nSamplesPerSec = fileRate;
            testWfx.Format.wBitsPerSample = cand.bits;
            testWfx.Format.nBlockAlign = (fileChannels * cand.bits) / 8;
            testWfx.Format.nAvgBytesPerSec = fileRate * testWfx.Format.nBlockAlign;
            testWfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(::WAVEFORMATEX);
            testWfx.Samples.wValidBitsPerSample = cand.validBits;
            testWfx.dwChannelMask = (fileChannels == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
            testWfx.SubFormat = cand.subFormat;

            if (m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, reinterpret_cast<::WAVEFORMATEX *>(&testWfx), NULL) == S_OK) {
                chosenWfx = testWfx;
                formatFound = true;
                shareMode = AUDCLNT_SHAREMODE_EXCLUSIVE;
                break;
            }
        }

        if (!formatFound) {
            shareMode = AUDCLNT_SHAREMODE_SHARED;
            WAVEFORMATEXTENSIBLE testWfx = {};
            testWfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            testWfx.Format.nChannels = fileChannels;
            testWfx.Format.nSamplesPerSec = fileRate;
            testWfx.Format.wBitsPerSample = 32;
            testWfx.Format.nBlockAlign = (fileChannels * 32) / 8;
            testWfx.Format.nAvgBytesPerSec = fileRate * testWfx.Format.nBlockAlign;
            testWfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(::WAVEFORMATEX);
            testWfx.Samples.wValidBitsPerSample = 32;
            testWfx.dwChannelMask = (fileChannels == 2) ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : 0;
            testWfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

            ::WAVEFORMATEX * pClosestMatch = nullptr;
            HRESULT hrSupport = m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<::WAVEFORMATEX *>(&testWfx), &pClosestMatch);

            if (hrSupport == S_OK) {
                chosenWfx = testWfx;
            }
            else if (hrSupport == S_FALSE && pClosestMatch != nullptr) {
                if (pClosestMatch->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                    memcpy(&chosenWfx, pClosestMatch, sizeof(WAVEFORMATEXTENSIBLE));
                }
                else {
                    memset(&chosenWfx, 0, sizeof(WAVEFORMATEXTENSIBLE));
                    memcpy(&chosenWfx.Format, pClosestMatch, sizeof(::WAVEFORMATEX));
                }
                CoTaskMemFree(pClosestMatch);
            }
            else {
                if (pClosestMatch) CoTaskMemFree(pClosestMatch);
                if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                    memcpy(&chosenWfx, pwfx, sizeof(WAVEFORMATEXTENSIBLE));
                }
                else {
                    memset(&chosenWfx, 0, sizeof(WAVEFORMATEXTENSIBLE));
                    memcpy(&chosenWfx.Format, pwfx, sizeof(::WAVEFORMATEX));
                }
            }
        }

        if (shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
            REFERENCE_TIME hnsDefaultDevicePeriod = 0, hnsMin = 0;
            m_audioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMin);
            hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, hnsDefaultDevicePeriod, 0, reinterpret_cast<::WAVEFORMATEX *>(&chosenWfx), NULL);
            EXIT_ON_ERROR(hr, "Failed to initialize Audio Client in Exclusive Mode.");
        }
        else {
            hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, reinterpret_cast<::WAVEFORMATEX *>(&chosenWfx), NULL);
            EXIT_ON_ERROR(hr, "Failed to initialize Audio Client in Shared Mode.");
        }

        hr = m_audioClient->GetService(__uuidof(IAudioRenderClient), (void **)&pRenderClient);
        EXIT_ON_ERROR(hr, "Failed to get AudioRenderClient service.");

        UINT32 bufferFrameCount = 0;
        hr = m_audioClient->GetBufferSize(&bufferFrameCount);
        EXIT_ON_ERROR(hr, "Failed to get buffer size.");

        timeBeginPeriod(1);
        hr = m_audioClient->Start();
        EXIT_ON_ERROR(hr, "Failed to start audio playback.");

        DWORD sleepMs = 10;
        if (shareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) {
            REFERENCE_TIME hnsDefaultDevicePeriod = 0, hnsMin = 0;
            m_audioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &hnsMin);
            sleepMs = static_cast<DWORD>(hnsDefaultDevicePeriod / 30000);
            if (sleepMs < 1) sleepMs = 1;
        }

        {
            bool isFloatOutput = (chosenWfx.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
            size_t outChannels = chosenWfx.Format.nChannels;
            double deviceSampleRate = static_cast<double>(chosenWfx.Format.nSamplesPerSec);

            auto loadAndPrepareAsync = [outChannels, &gainMultipliers, &playlistFiles](size_t trackIdx) {
                CoInitializeEx(NULL, COINIT_MULTITHREADED);
                auto filePtr = std::make_shared<CPlaybackFile>();
                int res = LoadFile(playlistFiles[trackIdx].c_str(), *filePtr);
                std::vector<double> preparedBuf;
                if (res == APE_ERROR_SUCCESS && filePtr->m_spData.GetPtr()) {
                    size_t frames = static_cast<size_t>(filePtr->m_nBlocks);
                    size_t inCh = static_cast<size_t>(filePtr->m_nChannels);
                    double gain = gainMultipliers[trackIdx];
                    preparedBuf.resize(frames * outChannels, 0.0);
                    for (size_t f = 0; f < frames; ++f) {
                        for (size_t ch = 0; ch < outChannels; ++ch) {
                            size_t srcCh = (ch < inCh) ? ch : 0;
                            preparedBuf[f * outChannels + ch] = filePtr->m_spData[f * inCh + srcCh] * gain;
                        }
                    }
                }
                CoUninitialize();
                return std::make_tuple(res, filePtr, std::move(preparedBuf));
                };

            auto streamBufferToAudioClient = [&](const std::vector<double> & sourceBuf, size_t startFrame, size_t numFrames) {
                size_t framesPlayed = 0;
                while (framesPlayed < numFrames && !m_bStop) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

                    UINT32 numPaddingFrames = 0;
                    HRESULT hrPad = m_audioClient->GetCurrentPadding(&numPaddingFrames);
                    if (FAILED(hrPad) || m_bStop) break;

                    UINT32 numFramesAvailable = bufferFrameCount - numPaddingFrames;
                    if (numFramesAvailable == 0) continue;

                    UINT32 framesRemaining = static_cast<UINT32>(numFrames - framesPlayed);
                    UINT32 framesToWrite = APE_MIN(numFramesAvailable, framesRemaining);
                    if (framesToWrite == 0) continue;

                    BYTE * pData = nullptr;
                    hrPad = pRenderClient->GetBuffer(framesToWrite, &pData);
                    if (FAILED(hrPad) || m_bStop) break;

                    size_t actualStart = startFrame + framesPlayed;
                    if (isFloatOutput) {
                        float * pDest = reinterpret_cast<float *>(pData);
                        size_t idx = 0;
                        for (size_t f = actualStart; f < actualStart + framesToWrite; ++f) {
                            for (size_t ch = 0; ch < outChannels; ++ch) {
                                double sample = sourceBuf[f * outChannels + ch];
                                if (sample > 1.0) sample = 1.0;
                                if (sample < -1.0) sample = -1.0;
                                pDest[idx++] = static_cast<float>(sample);
                            }
                        }
                    }
                    else if (chosenWfx.Format.wBitsPerSample == 16) {
                        int16_t * pDest = reinterpret_cast<int16_t *>(pData);
                        size_t idx = 0;
                        for (size_t f = actualStart; f < actualStart + framesToWrite; ++f) {
                            for (size_t ch = 0; ch < outChannels; ++ch) {
                                double sample = sourceBuf[f * outChannels + ch];
                                if (sample > 1.0) sample = 1.0;
                                if (sample < -1.0) sample = -1.0;
                                pDest[idx++] = static_cast<int16_t>(sample * 32767.0);
                            }
                        }
                    }
                    else {
                        int32_t * pDest = reinterpret_cast<int32_t *>(pData);
                        size_t idx = 0;
                        for (size_t f = actualStart; f < actualStart + framesToWrite; ++f) {
                            for (size_t ch = 0; ch < outChannels; ++ch) {
                                double sample = sourceBuf[f * outChannels + ch];
                                if (sample > 1.0) sample = 1.0;
                                if (sample < -1.0) sample = -1.0;
                                pDest[idx++] = static_cast<int32_t>(sample * 8388607.0) << 8;
                            }
                        }
                    }

                    hrPad = pRenderClient->ReleaseBuffer(framesToWrite, 0);
                    if (FAILED(hrPad)) break;

                    framesPlayed += framesToWrite;
                }
                };

            const double CROSSFADE_SECONDS = 10.0;
            size_t crossfadeFrames = static_cast<size_t>(CROSSFADE_SECONDS * deviceSampleRate);

            std::vector<double> currBuffer;
            {
                size_t frames = static_cast<size_t>(pCurrFile->m_nBlocks);
                size_t inCh = static_cast<size_t>(pCurrFile->m_nChannels);
                double gain = gainMultipliers[0];
                currBuffer.resize(frames * outChannels, 0.0);
                for (size_t f = 0; f < frames; ++f) {
                    for (size_t ch = 0; ch < outChannels; ++ch) {
                        size_t srcCh = (ch < inCh) ? ch : 0;
                        currBuffer[f * outChannels + ch] = pCurrFile->m_spData[f * inCh + srcCh] * gain;
                    }
                }
            }

            size_t currFramePos = 0;
            size_t totalCurrFrames = currBuffer.size() / outChannels;
            size_t currentTrackIdx = 0;

            std::future<std::tuple<int, std::shared_ptr<CPlaybackFile>, std::vector<double>>> nextFileFuture;
            if (currentTrackIdx + 1 < playlistFiles.size()) {
                nextFileFuture = std::async(std::launch::async, loadAndPrepareAsync, currentTrackIdx + 1);
            }

            std::cout << "Playing track (" << currentTrackIdx + 1 << "/" << playlistFiles.size() << ")..." << std::endl;

            while (currentTrackIdx < playlistFiles.size() && !m_bStop) {
                bool hasNext = (currentTrackIdx + 1 < playlistFiles.size());
                size_t effectiveCrossfade = hasNext ? std::min(crossfadeFrames, totalCurrFrames) : 0;
                size_t normalPlayEnd = totalCurrFrames > effectiveCrossfade ? totalCurrFrames - effectiveCrossfade : 0;

                if (currFramePos < normalPlayEnd && !m_bStop) {
                    size_t framesToPlay = normalPlayEnd - currFramePos;
                    streamBufferToAudioClient(currBuffer, currFramePos, framesToPlay);
                    currFramePos = normalPlayEnd;
                }

                if (hasNext && currFramePos < totalCurrFrames && !m_bStop) {
                    std::cout << "Crossfading to track (" << currentTrackIdx + 2 << "/" << playlistFiles.size() << ")..." << std::endl;

                    auto nextLoadResult = nextFileFuture.get();
                    int nextRes = std::get<0>(nextLoadResult);
                    auto pNextFile = std::get<1>(nextLoadResult);
                    std::vector<double> nextBuffer = std::move(std::get<2>(nextLoadResult));

                    if (nextRes != APE_ERROR_SUCCESS || !pNextFile || nextBuffer.empty() || m_bStop) {
                        if (!m_bStop) std::cerr << "Failed to load next file. Finishing current track normally." << std::endl;
                        size_t framesToPlay = totalCurrFrames - currFramePos;
                        streamBufferToAudioClient(currBuffer, currFramePos, framesToPlay);
                        currFramePos = totalCurrFrames;
                    }
                    else {
                        size_t totalNextFrames = nextBuffer.size() / outChannels;
                        size_t cfCount = std::min({ effectiveCrossfade, totalCurrFrames - currFramePos, totalNextFrames });
                        std::vector<double> mixBuffer(cfCount * outChannels, 0.0);

                        for (size_t i = 0; i < cfCount; ++i) {
                            double t = static_cast<double>(i) / static_cast<double>(cfCount);
                            double outGain = 1.0 - t;
                            double inGain = t;

                            size_t currF = currFramePos + i;
                            size_t nextF = i;

                            for (size_t ch = 0; ch < outChannels; ++ch) {
                                double sCurr = currBuffer[currF * outChannels + ch] * outGain;
                                double sNext = nextBuffer[nextF * outChannels + ch] * inGain;
                                mixBuffer[i * outChannels + ch] = sCurr + sNext;
                            }
                        }

                        streamBufferToAudioClient(mixBuffer, 0, cfCount);

                        currentTrackIdx++;
                        pCurrFile = pNextFile;
                        currBuffer = std::move(nextBuffer);
                        totalCurrFrames = totalNextFrames;
                        currFramePos = cfCount;

                        if (currentTrackIdx + 1 < playlistFiles.size() && !m_bStop) {
                            nextFileFuture = std::async(std::launch::async, loadAndPrepareAsync, currentTrackIdx + 1);
                        }

                        std::cout << "Playing track (" << currentTrackIdx + 1 << "/" << playlistFiles.size() << ")..." << std::endl;
                        continue;
                    }
                }

                if (currFramePos < totalCurrFrames && !m_bStop) {
                    size_t framesToPlay = totalCurrFrames - currFramePos;
                    streamBufferToAudioClient(currBuffer, currFramePos, framesToPlay);
                    currFramePos = totalCurrFrames;
                }

                currentTrackIdx++;
                if (currentTrackIdx < playlistFiles.size() && !m_bStop) {
                    auto nextLoadResult = nextFileFuture.get();
                    int nextRes = std::get<0>(nextLoadResult);
                    auto pNextFile = std::get<1>(nextLoadResult);
                    std::vector<double> nextBuffer = std::move(std::get<2>(nextLoadResult));

                    if (nextRes != APE_ERROR_SUCCESS || !pNextFile || nextBuffer.empty()) {
                        std::cerr << "Failed to load file at index " << currentTrackIdx << std::endl;
                        break;
                    }
                    pCurrFile = pNextFile;
                    currBuffer = std::move(nextBuffer);
                    totalCurrFrames = currBuffer.size() / outChannels;
                    currFramePos = 0;

                    if (currentTrackIdx + 1 < playlistFiles.size() && !m_bStop) {
                        nextFileFuture = std::async(std::launch::async, loadAndPrepareAsync, currentTrackIdx + 1);
                    }
                    std::cout << "Playing track (" << currentTrackIdx + 1 << "/" << playlistFiles.size() << ")..." << std::endl;
                }
            }
        }

    Exit:
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Cleanup:
        if (m_audioClient) {
            m_audioClient->Stop();
        }
        timeEndPeriod(1);

        if (pwfx) CoTaskMemFree(pwfx);
        SafeRelease(&pRenderClient);
        SafeRelease(&m_audioClient);
        SafeRelease(&pDevice);
        SafeRelease(&pEnumerator);
        CoUninitialize();
    }
};