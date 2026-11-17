#pragma once

class CWinampSettingsDlg;

class CMACDllApp : public CWinApp
{
public:
    CMACDllApp() APE_NO_EXCEPT;
    APE_DESTRUCTOR(CMACDllApp);

    BOOL InitInstance() APE_OVERRIDE;

protected:
    DECLARE_MESSAGE_MAP();
};

extern CMACDllApp g_Application;
