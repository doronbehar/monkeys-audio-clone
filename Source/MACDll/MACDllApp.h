#pragma once

class CWinampSettingsDlg;

class CMACDllApp : public CWinApp
{
public:
    CMACDllApp();
    virtual ~CMACDllApp() APE_OVERRIDE;

    virtual BOOL InitInstance() APE_OVERRIDE;

protected:
    DECLARE_MESSAGE_MAP();
};

extern CMACDllApp g_Application;
