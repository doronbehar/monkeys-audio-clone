#pragma once

#include "Format.h"

/**************************************************************************************************
CFormatAPEProgressCallback
**************************************************************************************************/
class CFormatAPEProgressCallback : public APE::IAPEProgressCallback
{
public:
    CFormatAPEProgressCallback(MAC_FILE * pInfo);

    // IAPEProgressCallback
    void Progress(int nPercentageDone) APE_OVERRIDE;
    int GetKillFlag() APE_OVERRIDE;

protected:
    // data
    MAC_FILE * m_pInfo;
};

/**************************************************************************************************
CFormatAPE
**************************************************************************************************/
class CFormatAPE : public IFormat
{
public:
    CFormatAPE(int nIndex);
    APE_DESTRUCTOR(CFormatAPE);

    bool GetValid() APE_OVERRIDE { return true; }
    CString GetName() APE_OVERRIDE;

    int Process(MAC_FILE * pInfo) APE_OVERRIDE;

    bool BuildMenu(CMenu * pMenu, int nBaseID) APE_OVERRIDE;
    bool ProcessMenuCommand(int nCommand) APE_OVERRIDE;

    CString GetInputExtensions(APE::APE_MODES Mode) APE_OVERRIDE;
    CString GetOutputExtension(APE::APE_MODES Mode, const CString & strInputFilename, int nLevel) APE_OVERRIDE;

protected:
    int m_nIndex;
};
