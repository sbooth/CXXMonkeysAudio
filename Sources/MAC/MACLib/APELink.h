#pragma once

#include "IO.h"
#include "APEInfo.h"

namespace APE
{

class CAPELink
{
public:
    CAPELink(const str_utfn * pFilename);
    CAPELink(const char * pData, const str_utfn * pFilename);
    virtual ~CAPELink();

    bool GetIsLinkFile();
    int GetStartBlock();
    int GetFinishBlock();
    const wchar_t * GetImageFilename();

protected:
    int m_nStartBlock;
    int m_nFinishBlock;
    str_utfn m_cImageFilename[MAX_PATH];
    bool m_bIsLinkFile;

    void ParseData(const char * pData, const str_utfn * pFilename);
};

}
