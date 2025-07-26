#pragma once

namespace APE
{

/**************************************************************************************************
CRollBuffer
**************************************************************************************************/
template <class TYPE, int WINDOW_ELEMENTS> class CRollBuffer
{
public:
    CRollBuffer(int nHistoryElements)
    : m_nHistoryElements(nHistoryElements),
      m_nTotalElements(WINDOW_ELEMENTS + m_nHistoryElements)
    {
        m_pData = new TYPE [static_cast<size_t>(m_nTotalElements)];
        Flush();
    }

    ~CRollBuffer()
    {
        APE_SAFE_ARRAY_DELETE(m_pData)
    }

    void Flush()
    {
        ZeroMemory(m_pData, (static_cast<size_t>(m_nHistoryElements) + 1) * sizeof(TYPE));
        m_pCurrent = &m_pData[m_nHistoryElements];
    }

    void Roll()
    {
        memmove(&m_pData[0], &m_pCurrent[-m_nHistoryElements], static_cast<size_t>(m_nHistoryElements) * sizeof(TYPE));
        m_pCurrent = &m_pData[m_nHistoryElements];
    }

    __forceinline void IncrementSafe()
    {
        m_pCurrent++;
        if (m_pCurrent == &m_pData[m_nTotalElements])
            Roll();
    }

    __forceinline void IncrementFast()
    {
        m_pCurrent++;
    }

    __forceinline TYPE & operator[](const int nIndex) const
    {
        return m_pCurrent[nIndex];
    }

protected:
    TYPE * m_pData;
    TYPE * m_pCurrent;
    const int m_nHistoryElements;
    const int m_nTotalElements;

private:
    // silence warning about implicitly deleted assignment operator
    CRollBuffer<TYPE, WINDOW_ELEMENTS> & operator=(const CRollBuffer<TYPE, WINDOW_ELEMENTS> & Copy) { }
};

template <class TYPE, int WINDOW_ELEMENTS, int HISTORY_ELEMENTS> class CRollBufferFast
{
public:
    CRollBufferFast()
    {
        Flush();
    }

    void Flush()
    {
        ZeroMemory(m_aryData, (HISTORY_ELEMENTS + 1) * sizeof(TYPE));
        m_pCurrent = &m_aryData[HISTORY_ELEMENTS];
    }

    void Roll()
    {
        memmove(&m_aryData[0], &m_pCurrent[-HISTORY_ELEMENTS], HISTORY_ELEMENTS * sizeof(TYPE));
        m_pCurrent = &m_aryData[HISTORY_ELEMENTS];
    }

    __forceinline void IncrementSafe()
    {
        m_pCurrent++;
        if (m_pCurrent == &m_aryData[WINDOW_ELEMENTS + HISTORY_ELEMENTS])
            Roll();
    }

    __forceinline void IncrementFast()
    {
        m_pCurrent++;
    }

    __forceinline TYPE & operator[](const int nIndex) const
    {
        return m_pCurrent[nIndex];
    }

protected:
    TYPE * m_pCurrent;
    TYPE m_aryData[WINDOW_ELEMENTS + HISTORY_ELEMENTS];
};

}
