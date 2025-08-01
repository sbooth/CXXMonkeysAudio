#include "All.h"
#include "IO.h"
#include "APECompressCreate.h"
#include "APECompressCore.h"
#include "GlobalFunctions.h"

#ifdef APE_SUPPORT_COMPRESS

namespace APE
{

CAPECompressCreate::CAPECompressCreate()
{
    m_nMaxFrames = 0;
    m_bTooMuchData = false;

    m_nThreads = 1;
    m_nNextWorker = 0;

    m_nFinalWord = 0;
    m_nFinalBytes = 0;

    // initialize to avoid warnings
    m_nCompressionLevel = 0;
    m_nBlocksPerFrame = 0;
    m_nFrameIndex = 0;
    m_nLastFrameBlocks = 0;
    APE_CLEAR(m_wfeInput); // fully replaced on Start(...) but we'll clear just for good form
}

CAPECompressCreate::~CAPECompressCreate()
{
    // without this the compiler warns that it can't inline the destuctor
}

int CAPECompressCreate::Start(CIO * pioOutput, int nThreads, const WAVEFORMATEX * pwfeInput, int64 nMaxAudioBytes, int nCompressionLevel, const void * pHeaderData, int64 nHeaderBytes, int32 nFlags)
{
    // verify the parameters
    if (pioOutput == APE_NULL || pwfeInput == APE_NULL)
        return ERROR_BAD_PARAMETER;

    // verify channels
    if ((pwfeInput->nChannels < APE_MINIMUM_CHANNELS) || (pwfeInput->nChannels > APE_MAXIMUM_CHANNELS))
    {
        return ERROR_INPUT_FILE_UNSUPPORTED_CHANNEL_COUNT;
    }

    // verify bitdepth
    if ((pwfeInput->wBitsPerSample != 8) && (pwfeInput->wBitsPerSample != 16) && (pwfeInput->wBitsPerSample != 24) && (pwfeInput->wBitsPerSample != 32))
    {
        return ERROR_INPUT_FILE_UNSUPPORTED_BIT_DEPTH;
    }

    // verify format tag
    if (pwfeInput->wFormatTag == WAVE_FORMAT_PCM)
    {
        // supported
    }
    else if (pwfeInput->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        // supported only if we have float compression enabled
        #ifndef APE_SUPPORT_FLOAT_COMPRESSION
            return ERROR_INVALID_INPUT_FILE;
        #endif
        nFlags |= APE_FORMAT_FLAG_FLOATING_POINT;
    }
    else if (pwfeInput->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        // supported (in most cases)
    }
    else
    {
        // unsupported file
        return ERROR_INVALID_INPUT_FILE;
    }

    // initialize (creates the base classes)
    m_nBlocksPerFrame = 73728;
    if (nCompressionLevel == APE_COMPRESSION_LEVEL_EXTRA_HIGH)
        m_nBlocksPerFrame *= 4;
    else if (nCompressionLevel == APE_COMPRESSION_LEVEL_INSANE)
        m_nBlocksPerFrame *= 16;

    m_spIO.Assign(pioOutput, false, false);

    // create and start threads
    m_nThreads = nThreads;

    for (int i = 0; i < m_nThreads; i++)
    {
        m_spAPECompressCore[i].Assign(new CAPECompressCore(pwfeInput, m_nBlocksPerFrame, nCompressionLevel));
        m_spAPECompressCore[i]->Start();
    }

    m_nFinalWord = 0;
    m_nFinalBytes = 0;

    // copy the format
    memcpy(&m_wfeInput, pwfeInput, sizeof(WAVEFORMATEX));

    // the compression level
    m_nCompressionLevel = nCompressionLevel;
    m_nFrameIndex = 0;
    m_nLastFrameBlocks = m_nBlocksPerFrame;

    // initialize the file
    const uint32 nMaxAudioBlocks = (nMaxAudioBytes == MAX_AUDIO_BYTES_UNKNOWN) ? 0xFFFFFFFF : static_cast<uint32>(nMaxAudioBytes / pwfeInput->nBlockAlign);
    int64 nMaxFrames = (static_cast<int64>(nMaxAudioBlocks) / static_cast<int64>(m_nBlocksPerFrame));
    if ((nMaxAudioBlocks % static_cast<uint32>(m_nBlocksPerFrame)) != 0) nMaxFrames++;

    return InitializeFile(m_spIO, &m_wfeInput, static_cast<intn>(nMaxFrames), m_nCompressionLevel, pHeaderData, nHeaderBytes, nFlags);
}

intn CAPECompressCreate::GetFullFrameBytes() const
{
    return static_cast<intn>(m_nBlocksPerFrame) * static_cast<intn>(m_wfeInput.nBlockAlign);
}

int CAPECompressCreate::EncodeFrame(const void * pInputData, int nInputBytes)
{
    const int nInputBlocks = nInputBytes / m_wfeInput.nBlockAlign;

    if ((nInputBlocks < m_nBlocksPerFrame) && (m_nLastFrameBlocks < m_nBlocksPerFrame))
    {
        return ERROR_UNDEFINED; // can only pass a smaller frame for the very last time
    }

    CAPECompressCore * pWorker = m_spAPECompressCore[m_nNextWorker];

    // get previously encoded frame
    pWorker->WaitUntilReady();

    if (pWorker->GetFrameBytes() > 0) WriteFrame(pWorker->GetFrameBuffer(), pWorker->GetFrameBytes());

    // encode next frame
    int nResult = pWorker->EncodeFrame(pInputData, nInputBytes);

    // update stats
    m_nLastFrameBlocks = nInputBlocks;
    m_nNextWorker = (m_nNextWorker + 1) % m_nThreads;

    return nResult;
}

int CAPECompressCreate::WriteFrame(unsigned char * pOutputData, uint32 nBytes)
{
    // update the seek table
    int nResult = SetSeekByte(m_nFrameIndex++, m_spIO->GetPosition() + m_nFinalBytes);
    if (nResult != ERROR_SUCCESS)
        return nResult;

    // fixup frame byte order
    FixupFrame(pOutputData, nBytes, m_nFinalWord, m_nFinalBytes);
    nBytes += m_nFinalBytes;

    // add to MD5
    m_MD5.AddData(pOutputData, static_cast<int64>(nBytes / 4 * 4));

    // write data
    unsigned int nBytesWritten = 0;
    m_spIO->Write(pOutputData, nBytes / 4 * 4, &nBytesWritten);

    // update final word/bytes
    m_nFinalWord = reinterpret_cast<uint32 *>(pOutputData)[nBytes / 4];
    m_nFinalBytes = nBytes % 4;

    return ERROR_SUCCESS;
}

void CAPECompressCreate::FixupFrame(unsigned char * pBuffer, uint32 nBytes, uint32 nFinalWord, uint32 nFinalBytes)
{
    if (nFinalBytes == 0)
        return;

    int nWords = static_cast<int>(nBytes / 4 + 1);

    SwitchBufferBytes(reinterpret_cast<uint32 *>(pBuffer), 4, nWords);

    memmove(pBuffer + nFinalBytes, pBuffer, nBytes);
    memcpy(pBuffer, &nFinalWord, nFinalBytes);

    SwitchBufferBytes(reinterpret_cast<uint32 *>(pBuffer), 4, nWords);
}

int CAPECompressCreate::Finish(const void * pTerminatingData, int64 nTerminatingBytes, int64 nWAVTerminatingBytes)
{
    // wait for worker threads to finish and write remaining frames
    for (int i = 0; i < m_nThreads; i++)
    {
        CAPECompressCore * pWorker = m_spAPECompressCore[m_nNextWorker];

        pWorker->WaitUntilReady();

        if (pWorker->GetFrameBytes() > 0) WriteFrame(pWorker->GetFrameBuffer(), pWorker->GetFrameBytes());

        pWorker->Exit();
        pWorker->Wait();

        m_nNextWorker = (m_nNextWorker + 1) % m_nThreads;
    }

    // write out final word
    if (m_nFinalBytes == 0) m_nFinalWord = 0;

    m_MD5.AddData(&m_nFinalWord, 4);

    unsigned int nBytesWritten = 0;
    m_spIO->Write(&m_nFinalWord, 4, &nBytesWritten);

    // finalize the file
    return FinalizeFile(m_spIO, m_nFrameIndex, m_nLastFrameBlocks, pTerminatingData, nTerminatingBytes, nWAVTerminatingBytes);
}

bool CAPECompressCreate::GetTooMuchData() const
{
    return m_bTooMuchData;
}

int CAPECompressCreate::SetSeekByte(int nFrame, int64 nByteOffset)
{
    if (nFrame >= m_nMaxFrames)
    {
        m_bTooMuchData = true;
        return ERROR_APE_COMPRESS_TOO_MUCH_DATA;
    }
    const uint32 nSeekEntry = static_cast<uint32>(nByteOffset); // we let this overflow then correct the overflows when we parse the table
    m_spSeekTable[nFrame] = ConvertU32LE(nSeekEntry);
    return ERROR_SUCCESS;
}

int CAPECompressCreate::InitializeFile(CIO * pIO, const WAVEFORMATEX * pwfeInput, intn nMaxFrames, intn nCompressionLevel, const void * pHeaderData, int64 nHeaderBytes, int32 nFlags)
{
    // error check the parameters
    if (pIO == APE_NULL || pwfeInput == APE_NULL || nMaxFrames <= 0)
        return ERROR_BAD_PARAMETER;

    APE_DESCRIPTOR APEDescriptor; APE_CLEAR(APEDescriptor);
    APE_HEADER APEHeader; APE_CLEAR(APEHeader);

    // don't allow header data that's too large
    if (nHeaderBytes > APE_WAV_HEADER_OR_FOOTER_MAXIMUM_BYTES)
        return ERROR_INPUT_FILE_TOO_LARGE;

    // create the descriptor (only fill what we know)
    APEDescriptor.cID[0] = 'M';
    APEDescriptor.cID[1] = 'A';
    APEDescriptor.cID[2] = 'C';
    APEDescriptor.cID[3] = (nFlags & APE_FORMAT_FLAG_FLOATING_POINT) ? 'F' : ' ';

    APEDescriptor.nVersion = ConvertU16LE(APE_FILE_VERSION_NUMBER);
    APEDescriptor.nPadding = 0; // set to zero even though we memset above to be clean

    APEDescriptor.nDescriptorBytes = ConvertU32LE(sizeof(APEDescriptor));
    APEDescriptor.nHeaderBytes = ConvertU32LE(sizeof(APEHeader));
    APEDescriptor.nSeekTableBytes = ConvertU32LE(static_cast<uint32>(nMaxFrames) * static_cast<uint32>(sizeof(unsigned int)));
    APEDescriptor.nHeaderDataBytes = ConvertU32LE(static_cast<uint32>((nHeaderBytes == CREATE_WAV_HEADER_ON_DECOMPRESSION) ? 0 : nHeaderBytes));

    // create the header (only fill what we know now)
    APEHeader.nBitsPerSample = ConvertU16LE(pwfeInput->wBitsPerSample);
    APEHeader.nChannels = ConvertU16LE(pwfeInput->nChannels);
    APEHeader.nSampleRate = ConvertU32LE(pwfeInput->nSamplesPerSec);

    APEHeader.nCompressionLevel = ConvertU16LE(static_cast<uint16>(nCompressionLevel));
    APEHeader.nFormatFlags = ConvertU16LE(static_cast<uint16>(nFlags));
    APEHeader.nFormatFlags |= ConvertU16LE(static_cast<uint16>((nHeaderBytes == CREATE_WAV_HEADER_ON_DECOMPRESSION) ? APE_FORMAT_FLAG_CREATE_WAV_HEADER : 0));

    APEHeader.nBlocksPerFrame = ConvertU32LE(static_cast<uint32>(m_nBlocksPerFrame));

    // write the data to the file
    unsigned int nBytesWritten = 0;
    RETURN_ON_ERROR(pIO->Write(&APEDescriptor, sizeof(APEDescriptor), &nBytesWritten))
    RETURN_ON_ERROR(pIO->Write(&APEHeader, sizeof(APEHeader), &nBytesWritten))

    // write an empty seek table
    m_spSeekTable.Assign(new uint32 [static_cast<size_t>(nMaxFrames)], true);
    if (m_spSeekTable == APE_NULL) { return ERROR_INSUFFICIENT_MEMORY; }
    ZeroMemory(m_spSeekTable, static_cast<size_t>(nMaxFrames * 4));
    RETURN_ON_ERROR(pIO->Write(m_spSeekTable, static_cast<unsigned int>(nMaxFrames * 4), &nBytesWritten))
    m_nMaxFrames = nMaxFrames;

    // write the WAV data
    if ((pHeaderData != APE_NULL) && (nHeaderBytes > 0) && (nHeaderBytes != CREATE_WAV_HEADER_ON_DECOMPRESSION))
    {
        // MD5 and write data
        m_MD5.AddData(pHeaderData, nHeaderBytes);
        RETURN_ON_ERROR(pIO->Write(pHeaderData, static_cast<unsigned int>(nHeaderBytes), &nBytesWritten))
    }

    return ERROR_SUCCESS;
}

int CAPECompressCreate::FinalizeFile(CIO * pIO, int nNumberOfFrames, int nFinalFrameBlocks, const void * pTerminatingData, int64 nTerminatingBytes, int64 nWAVTerminatingBytes)
{
    // store the tail position
    const int64 nTailPosition = pIO->GetPosition();

    // append the terminating data
    unsigned int nBytesWritten = 0;
    unsigned int nBytesRead = 0;
    int64 nResult = 0;

    if ((pTerminatingData != APE_NULL) && (nTerminatingBytes > 0))
    {
        // don't allow terminating data that's too large
        if (nTerminatingBytes > APE_WAV_HEADER_OR_FOOTER_MAXIMUM_BYTES)
            return ERROR_INPUT_FILE_TOO_LARGE;

        // update the MD5 sum to include the WAV terminating bytes
        m_MD5.AddData(pTerminatingData, nWAVTerminatingBytes);

        // get the write size
        const unsigned int nWriteSize = static_cast<unsigned int>(nTerminatingBytes);

        // write the entire chunk to the new file
        if ((pIO->Write(pTerminatingData, nWriteSize, &nBytesWritten) != 0) ||
            (nBytesWritten != nWriteSize))
        {
            return ERROR_IO_WRITE;
        }
    }

    // go to the beginning and update the information
    pIO->Seek(0, SeekFileBegin);

    // get the descriptor
    APE_DESCRIPTOR APEDescriptor;
    nResult = pIO->Read(&APEDescriptor, sizeof(APEDescriptor), &nBytesRead);
    if ((nResult != 0) || (nBytesRead != sizeof(APEDescriptor))) { return ERROR_IO_READ; }

    // get the header
    APE_HEADER APEHeader;
    nResult = pIO->Read(&APEHeader, sizeof(APEHeader), &nBytesRead);
    if (nResult != 0 || nBytesRead != sizeof(APEHeader)) { return ERROR_IO_READ; }

    // update the header
    APEHeader.nFinalFrameBlocks = ConvertU32LE(static_cast<uint32>(nFinalFrameBlocks));
    APEHeader.nTotalFrames = ConvertU32LE(static_cast<uint32>(nNumberOfFrames));

    // update the descriptor
    const int64 nFrameDataBytes = nTailPosition - (static_cast<int64>(ConvertU32LE(APEDescriptor.nDescriptorBytes)) + static_cast<int64>(ConvertU32LE(APEDescriptor.nHeaderBytes)) + static_cast<int64>(ConvertU32LE(APEDescriptor.nSeekTableBytes)) + static_cast<int64>(ConvertU32LE(APEDescriptor.nHeaderDataBytes)));
    APEDescriptor.nAPEFrameDataBytes = ConvertU32LE(static_cast<uint32>(nFrameDataBytes & 0xFFFFFFFF));
    APEDescriptor.nAPEFrameDataBytesHigh = ConvertU32LE(static_cast<uint32>(nFrameDataBytes >> 32));
    APEDescriptor.nTerminatingDataBytes = ConvertU32LE(static_cast<uint32>(nWAVTerminatingBytes));

    // update the MD5
    m_MD5.AddData(&APEHeader, sizeof(APEHeader));
    m_MD5.AddData(m_spSeekTable, static_cast<int64>(m_nMaxFrames) * 4);
    m_MD5.GetResult(APEDescriptor.cFileMD5);

    // set the pointer and re-write the updated header and peak level
    pIO->Seek(0, SeekFileBegin);
    if (pIO->Write(&APEDescriptor, sizeof(APEDescriptor), &nBytesWritten) != 0) { return ERROR_IO_WRITE; }
    if (pIO->Write(&APEHeader, sizeof(APEHeader), &nBytesWritten) != 0) { return ERROR_IO_WRITE; }

    // write the updated seek table
    if (pIO->Write(m_spSeekTable, static_cast<unsigned int>(m_nMaxFrames * 4), &nBytesWritten) != 0) { return ERROR_IO_WRITE; }

    return ERROR_SUCCESS;
}

}

#endif
