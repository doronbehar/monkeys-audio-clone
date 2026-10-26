#pragma once

/**************************************************************************************************
CFilename - a filename wrapper to provide easy path splitting and other tools
**************************************************************************************************/
class CFilename
{
public:
    /**************************************************************************************************
    Construction / Destruction
    **************************************************************************************************/
    CFilename()
    {
        SetFilename(_T(""));
    }

    CFilename(LPCTSTR lpszFilename)
    {
        SetFilename(lpszFilename);
    }

    virtual ~CFilename()
    {
    }

    /**************************************************************************************************
    Operators
    **************************************************************************************************/
    const CFilename & operator=(LPCTSTR lpszFilename)
    {
        SetFilename(lpszFilename);
        return *this;
    }

    /**************************************************************************************************
    Set
    **************************************************************************************************/
    void SetFilename(LPCTSTR lpszFilename)
    {
        m_strFilename = lpszFilename;

        // measure the length of the filename
        int nFilenameCharacters = static_cast<int>(_tcslen(lpszFilename));

        // allocate the length of the filename plus a little squish
        const int nBufferPieceCharacters = nFilenameCharacters + 64;

        // we used to use _tsplitpath_s but it fails on long paths using _MAX_DRIVE, _MAX_DIR, _MAX_FNAME, etc.
        // so we're trying to just use bigger buffers
        // allocate buffers on the heap (because otherwise Clang warns)
        APE::CSmartPtr<TCHAR> spDrive(nBufferPieceCharacters, true);
        APE::CSmartPtr<TCHAR> spDir(nBufferPieceCharacters, true);
        APE::CSmartPtr<TCHAR> spName(nBufferPieceCharacters, true);
        APE::CSmartPtr<TCHAR> spExt(nBufferPieceCharacters, true);
        _tsplitpath_s(m_strFilename, spDrive.GetPtr(), nBufferPieceCharacters, spDir.GetPtr(), nBufferPieceCharacters, spName.GetPtr(), nBufferPieceCharacters, spExt.GetPtr(), nBufferPieceCharacters);

        m_strDrive = spDrive.GetPtr();
        m_strDirectory = spDir.GetPtr();
        m_strName = spName.GetPtr();
        m_strExtension = spExt.GetPtr();
    }

    /**************************************************************************************************
    Simple queries
    **************************************************************************************************/
    inline const CString & GetFilename() { return m_strFilename; }
    inline const CString & GetDrive() { return m_strDrive; }
    inline const CString & GetDirectory() { return m_strDirectory; }
    inline const CString & GetName() { return m_strName; }
    inline const CString & GetExtension() { return m_strExtension; }
    inline CString GetPath() const { return m_strDrive + m_strDirectory; }
    inline CString GetNameAndExtension() const { return m_strName + m_strExtension; }

    /**************************************************************************************************
    Advanced queries
    **************************************************************************************************/
    CString BuildFilename(LPCTSTR lpszDrive = APE_NULL, LPCTSTR lpszDirectory = APE_NULL, LPCTSTR lpszName = APE_NULL, LPCTSTR lpszExtension = APE_NULL)
    {
        if (lpszDrive == APE_NULL) lpszDrive = m_strDrive;
        if (lpszDirectory == APE_NULL) lpszDirectory = m_strDirectory;
        if (lpszName == APE_NULL) lpszName = m_strName;
        if (lpszExtension == APE_NULL) lpszExtension = m_strExtension;

        return CString(lpszDrive) + lpszDirectory + lpszName + lpszExtension;
    }

protected:
    /**************************************************************************************************
    Data members
    **************************************************************************************************/
    CString m_strFilename;

    CString m_strDrive;
    CString m_strDirectory;
    CString m_strName;
    CString m_strExtension;
};
