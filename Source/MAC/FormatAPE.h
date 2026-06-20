#pragma once

#include "Format.h"

class CFormatAPE : public IFormat
{
public:
    CFormatAPE(int nIndex);
    virtual ~CFormatAPE() APE_OVERRIDE;

    virtual bool GetValid() APE_OVERRIDE { return true; }
    virtual CString GetName() APE_OVERRIDE;

    virtual int Process(MAC_FILE * pInfo) APE_OVERRIDE;

    virtual bool BuildMenu(CMenu * pMenu, int nBaseID) APE_OVERRIDE;
    virtual bool ProcessMenuCommand(int nCommand) APE_OVERRIDE;

    virtual CString GetInputExtensions(APE::APE_MODES Mode) APE_OVERRIDE;
    virtual CString GetOutputExtension(APE::APE_MODES Mode, const CString & strInputFilename, int nLevel) APE_OVERRIDE;

protected:
    int m_nIndex;
};
