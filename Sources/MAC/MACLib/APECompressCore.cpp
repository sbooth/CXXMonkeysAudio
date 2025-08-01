#include "All.h"
#include "APECompressCore.h"
#include "BitArray.h"
#include "Prepare.h"
#include "NewPredictor.h"

#ifdef APE_SUPPORT_COMPRESS

namespace APE
{

CAPECompressCore::CAPECompressCore(const WAVEFORMATEX * pwfeInput, int nMaxFrameBlocks, int nCompressionLevel)
: m_semProcess(1), m_semReady(1)
{
    m_semProcess.Wait();

    APE_CLEAR(m_wfeInput);
    APE_CLEAR(m_aryBitArrayStates);
    m_nMaxFrameBlocks = nMaxFrameBlocks;
    int nDataSize = m_nMaxFrameBlocks * pwfeInput->nChannels * (pwfeInput->wBitsPerSample / 8);
    m_spInputData.Assign(new unsigned char [static_cast<size_t>(nDataSize)], true);
    m_spBitArray.Assign(new CBitArray(static_cast<uint32>(nDataSize / 4 * 3)));
    const intn nChannels = APE_MAX(pwfeInput->nChannels, 2);
    m_spData.Assign(new int [static_cast<size_t>(m_nMaxFrameBlocks * nChannels)], true);
    m_spPrepare.Assign(new CPrepare);
    APE_CLEAR(m_aryPredictors);
    for (int nChannel = 0; nChannel < nChannels; nChannel++)
    {
        if (pwfeInput->wBitsPerSample < 32)
            m_aryPredictors[nChannel] = new CPredictorCompressNormal<int, short>(nCompressionLevel, pwfeInput->wBitsPerSample);
        else
            m_aryPredictors[nChannel] = new CPredictorCompressNormal<int64, int>(nCompressionLevel, pwfeInput->wBitsPerSample);
    }
    memcpy(&m_wfeInput, pwfeInput, sizeof(WAVEFORMATEX));
    m_nInputBytes = 0;
    m_bExit = false;
}

CAPECompressCore::~CAPECompressCore()
{
    // stop any threading
    Exit();
    Wait();

    // delete the predictors
    for (int z = 0; z < APE_MAXIMUM_CHANNELS; z++)
    {
        if (m_aryPredictors[z] != APE_NULL)
            delete m_aryPredictors[z];
    }
}

void CAPECompressCore::Run()
{
    while (!m_bExit)
    {
        m_semProcess.Wait();

        if (m_bExit) break;

        Encode(m_spInputData, m_nInputBytes);

        m_semReady.Post();
    }
}

int CAPECompressCore::EncodeFrame(const void * pInputData, int nInputBytes)
{
    memcpy(m_spInputData, pInputData, static_cast<size_t>(nInputBytes));

    m_nInputBytes = nInputBytes;

    m_semProcess.Post();

    // return success
    return ERROR_SUCCESS;
}

int CAPECompressCore::Encode(const void * pInputData, int nInputBytes)
{
    // variables
    const int nInputBlocks = nInputBytes / m_wfeInput.nBlockAlign;
    int nSpecialCodes = 0;

    // start with an initial bit array
    m_spBitArray->ResetBitArray();

    // do the preparation stage
    RETURN_ON_ERROR(Prepare(pInputData, nInputBytes, &nSpecialCodes))

    for (int z = 0; z < APE_MAXIMUM_CHANNELS; z++)
    {
        if (m_aryPredictors[z] != APE_NULL)
            m_aryPredictors[z]->Flush();
        m_spBitArray->FlushState(m_aryBitArrayStates[z]);
    }

    m_spBitArray->FlushBitArray();

    // encode data
    if (m_wfeInput.nChannels == 2)
    {
        bool bEncodeX = true;
        bool bEncodeY = true;

        if ((nSpecialCodes & SPECIAL_FRAME_LEFT_SILENCE) &&
            (nSpecialCodes & SPECIAL_FRAME_RIGHT_SILENCE))
        {
            bEncodeX = false;
            bEncodeY = false;
        }

        if (nSpecialCodes & SPECIAL_FRAME_PSEUDO_STEREO)
        {
            bEncodeY = false;
        }

        if (bEncodeX && bEncodeY)
        {
            int nLastX = 0;
            for (int z = 0; z < nInputBlocks; z++)
            {
                m_spBitArray->EncodeValue(m_aryPredictors[1]->CompressValue(m_spData[m_nMaxFrameBlocks + z], nLastX), m_aryBitArrayStates[1]);
                m_spBitArray->EncodeValue(m_aryPredictors[0]->CompressValue(m_spData[z], m_spData[m_nMaxFrameBlocks + z]), m_aryBitArrayStates[0]);

                nLastX = m_spData[z];
            }
        }
        else if (bEncodeX)
        {
            for (int z = 0; z < nInputBlocks; z++)
            {
                RETURN_ON_ERROR(m_spBitArray->EncodeValue(m_aryPredictors[0]->CompressValue(m_spData[z]), m_aryBitArrayStates[0]))
            }
        }
        else if (bEncodeY)
        {
            for (int z = 0; z < nInputBlocks; z++)
            {
                RETURN_ON_ERROR(m_spBitArray->EncodeValue(m_aryPredictors[1]->CompressValue(m_spData[m_nMaxFrameBlocks + z]), m_aryBitArrayStates[1]))
            }
        }
    }
    else if (m_wfeInput.nChannels == 1)
    {
        if (!(nSpecialCodes & SPECIAL_FRAME_MONO_SILENCE))
        {
            for (int z = 0; z < nInputBlocks; z++)
            {
                RETURN_ON_ERROR(m_spBitArray->EncodeValue(m_aryPredictors[0]->CompressValue(m_spData[z]), m_aryBitArrayStates[0]))
            }
        }
    }
    else if (m_wfeInput.nChannels > 2)
    {
        for (int z = 0; z < nInputBlocks; z++)
        {
            for (int nChannel = 0; nChannel < m_wfeInput.nChannels; nChannel++)
            {
                m_spBitArray->EncodeValue(m_aryPredictors[nChannel]->CompressValue(m_spData[(nChannel * m_nMaxFrameBlocks) + z]), m_aryBitArrayStates[nChannel]);
            }
        }
    }

    m_spBitArray->Finalize();
    m_spBitArray->AdvanceToByteBoundary();

    // return success
    return ERROR_SUCCESS;
}

unsigned char * CAPECompressCore::GetFrameBuffer()
{
    return m_spBitArray->GetBitArray();
}

uint32 CAPECompressCore::GetFrameBytes() const
{
    return m_spBitArray->GetBitArrayBytes();
}

int CAPECompressCore::Prepare(const void * pInputData, int nInputBytes, int * pSpecialCodes)
{
    // variable declares
    *pSpecialCodes = 0;
    unsigned int nCRC = 0;

    // do the preparation
    RETURN_ON_ERROR(m_spPrepare->Prepare(static_cast<const unsigned char *>(pInputData), nInputBytes, &m_wfeInput, m_spData, m_nMaxFrameBlocks,
        &nCRC, pSpecialCodes))

    // store the CRC
    RETURN_ON_ERROR(m_spBitArray->EncodeUnsignedLong(nCRC))

    // store any special codes
    if (*pSpecialCodes != 0)
    {
        RETURN_ON_ERROR(m_spBitArray->EncodeUnsignedLong(static_cast<unsigned int>(*pSpecialCodes)))
    }

    return ERROR_SUCCESS;
}

void CAPECompressCore::WaitUntilReady()
{
    m_semReady.Wait();
}

void CAPECompressCore::Exit()
{
    m_bExit = true;

    m_semProcess.Post();
}

}

#endif
