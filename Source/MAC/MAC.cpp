// MAC.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "MAC.h"
#include "MACDlg.h"
#include "FormatArray.h"
#include "MACSettings.h"
#include "APEButtons.h"

using namespace APE;

CMACApp theApp;

BEGIN_MESSAGE_MAP(CMACApp, CWinApp)
    ON_COMMAND(ID_HELP, &CMACApp::OnHelp)
END_MESSAGE_MAP()

CMACApp::CMACApp()
{
    m_dScale = -1.0; // default to an impossible value so the first SetScale(...) call returns that it changed
    m_hSingleInstance = CreateMutex(NULL, FALSE, _T("Mokey's Audio"));
    DWORD dwLastError = GetLastError();
    m_bAnotherInstanceRunning = (dwLastError == ERROR_ALREADY_EXISTS);
    m_pMACDlg = NULL;
}

BOOL CMACApp::InitInstance()
{
    CWinApp::InitInstance();

    // don't run if we're already running
    if (m_bAnotherInstanceRunning)
    {
        HWND hwndMonkeysAudio = FindWindow(NULL, MAC_NAME);
        if (hwndMonkeysAudio != NULL)
        {
            ShowWindow(hwndMonkeysAudio, SW_RESTORE);
            SetForegroundWindow(hwndMonkeysAudio);
        }
        return FALSE;
    }

    // initialize
    AfxEnableControlContainer();
    AfxOleInit();
    InitCommonControls();
    AfxInitRichEdit();

    // parse command line
    CString strCommandLine = GetCommandLine();
    //AfxMessageBox(strCommandLine);
    strCommandLine = strCommandLine.Right(strCommandLine.GetLength() - (strCommandLine.Find(_T(".exe")) + 4));
    strCommandLine.TrimLeft(_T(" \"")); strCommandLine.TrimRight(_T(" \""));

    // files
    CStringArrayEx aryFiles;

    // uninstall if specified
    if (strCommandLine.CompareNoCase(_T("-uninstall")) == 0)
    {
        TCHAR cUninstall[MAX_PATH];
        APE_CLEAR(cUninstall);
        _tcscat_s(cUninstall, MAX_PATH, GetInstallPath());
        _tcscat_s(cUninstall, MAX_PATH, _T("uninstall.exe"));

        ShellExecute(NULL, NULL, cUninstall, NULL, NULL, SW_SHOW);

        return FALSE;
    }
    else
    {
        // assume we got a list of files
        aryFiles.InitFromList(strCommandLine, _T("|"));
    }

    // show program
    CMACDlg dlg(&aryFiles);
    m_pMACDlg = &dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResponse = dlg.DoModal();
    if (nResponse == IDOK)
    {
    }
    else if (nResponse == IDCANCEL)
    {
    }
    m_pMACDlg = NULL;

    // Since the dialog has been closed, return FALSE so that we exit the
    //  application, rather than start the application's message pump.
    return FALSE;
}

int CMACApp::ExitInstance()
{
    if (m_hSingleInstance != NULL)
    {
        CloseHandle(m_hSingleInstance);
        m_hSingleInstance = NULL;
    }

    m_sparyFormats.Delete();
    m_spSettings.Delete();

    return CWinApp::ExitInstance();
}

CFormatArray * CMACApp::GetFormatArray()
{
    if (m_sparyFormats == NULL)
        m_sparyFormats.Assign(new CFormatArray(m_pMACDlg));
    return m_sparyFormats.GetPtr();
}

CMACSettings * CMACApp::GetSettings()
{
    if (m_spSettings == NULL)
        m_spSettings.Assign(new CMACSettings);
    return m_spSettings.GetPtr();
}

void CMACApp::DeleteImageLists()
{
    m_ImageListToolbar.DeleteImageList();
    m_ImageListOptionsList.DeleteImageList();
    m_ImageListOptionsPages.DeleteImageList();
}

CImageList * CMACApp::GetImageList(EImageList Image)
{
    // suppressing the Clang analyze warning (NOLINT) was needed on 11/20/2022 and nothing could be found that works around it
    // switching instead of doing an if condition didn't matter
    // making a seperate function to call and ask for the image list didn't matter
    // so we'll just build with NOLINT for now
    CImageList * pImageList = (Image == Image_Toolbar) ? &m_ImageListToolbar : (Image == Image_OptionsList) ? &m_ImageListOptionsList : (Image == Image_OptionsPages) ? &m_ImageListOptionsPages : NULL; // NOLINT
    if ((pImageList != NULL) && (pImageList->GetSafeHandle() == NULL))
    {
        // start GDI plus
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

        // load the image
        CString strImage = GetProgramPath() + _T("APE_Buttons.png");
        if (FileExists(strImage) == false)
            strImage = GetInstallPath() + _T("APE_Buttons.png");
        CSmartPtr<Gdiplus::Bitmap> spBitmap(Gdiplus::Bitmap::FromFile(strImage));
        if ((spBitmap == NULL) || (spBitmap->GetLastStatus() != Gdiplus::Ok))
            return NULL;

        // build the image list
        CSize sizeImageList = GetSize(32, 32);
        pImageList->Create(sizeImageList.cx, sizeImageList.cy, ILC_COLOR32, TBB_COUNT + 1, 10);

        // setup drawing
        CSize sizeBitmap = theApp.GetSize(32 * static_cast<int>(spBitmap->GetWidth() / spBitmap->GetHeight()), 32);
        Gdiplus::Bitmap newBmp(sizeBitmap.cx, sizeBitmap.cy, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&newBmp);
        graphics.SetInterpolationMode(Gdiplus::InterpolationMode::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // fill with the background color of the toolbar
        if ((Image == Image_Toolbar) || (Image == Image_OptionsPages))
        {
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 240, 240, 240));
            graphics.FillRectangle(&b, 0, 0, TBB_COUNT * sizeImageList.cx, sizeImageList.cy);
        }
        else if (Image == Image_OptionsList)
        {
            Gdiplus::SolidBrush b(Gdiplus::Color(255, 255, 255, 255));
            graphics.FillRectangle(&b, 0, 0, TBB_COUNT * sizeImageList.cx, sizeImageList.cy);
        }

        // draw the image over the top
        int nSizeBitmap = static_cast<int>(spBitmap->GetHeight());
        for (int z = 0; z < TBB_COUNT; z++)
        {
            // copy the individual image to the temporary buffer
            // we do this because there were artifacts drawing from an image that had all the images next to each other
            // the artifacts were found 12/3/2021 and was a line next to the Make APL icon
            CSmartPtr<Gdiplus::Bitmap> spTemporary(spBitmap->Clone(nSizeBitmap * z, 0, nSizeBitmap, nSizeBitmap, PixelFormat32bppPARGB));

            // draw onto the graphics object
            Gdiplus::Rect rectSource(0, 0, nSizeBitmap, nSizeBitmap);
            Gdiplus::Rect rectDestination(z * sizeImageList.cy, 0, sizeImageList.cx, sizeImageList.cy);
            graphics.DrawImage(spTemporary, rectDestination, rectSource.X, rectSource.Y, rectSource.Width, rectSource.Height, Gdiplus::UnitPixel);

            // delete
            spTemporary.Delete();
        }

        // get the bitmap
        HBITMAP hBitmap;
        newBmp.GetHBITMAP(Gdiplus::Color::Transparent, &hBitmap);

        // add to the image list
        ImageList_AddMasked(pImageList->GetSafeHandle(), hBitmap, 0);
    }

    return pImageList;
}

CSize CMACApp::GetSize(int x, int y, double dAdditional)
{
    CSize sizeFinished(x, y);
    sizeFinished.cx = static_cast<int>(m_dScale * dAdditional * static_cast<double>(sizeFinished.cx));
    sizeFinished.cy = static_cast<int>(m_dScale * dAdditional * static_cast<double>(sizeFinished.cy));
    return sizeFinished;
}

int CMACApp::GetSizeReverse(int nSize)
{
    int nNewSize = static_cast<int>(static_cast<double>(nSize) / m_dScale);
    return nNewSize;
}

bool CMACApp::SetScale(double dScale)
{
    bool bResult = false;
    if (fabs(dScale - m_dScale) > 0.01)
    {
        m_dScale = dScale;
        bResult = true;
    }
    return bResult;
}
