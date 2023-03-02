#include "All.h"
#include "WholeFileIO.h"

namespace APE
{

CWholeFileIO::CWholeFileIO(CIO * pSource)
{
    // store source
    m_spSource.Assign(pSource);

    // get size
    const int64 nSize = m_spSource->GetSize();

    // make sure we're at the head of the file
    m_spSource->SetSeekPosition(0);
    m_spSource->SetSeekMethod(APE_FILE_BEGIN);
    m_spSource->PerformSeek();

    // the the size as 32-bit
    const uint32 n32BitSize = static_cast<uint32> (nSize); // we established it will fit above

    // create a buffer
    m_spWholeFile.Assign(new unsigned char [n32BitSize], true);

    // read
    unsigned int nBytesRead = 0;
    int nResult = m_spSource->Read(m_spWholeFile, n32BitSize, &nBytesRead);
    if (nBytesRead < n32BitSize)
        nResult = ERROR_IO_READ;
    m_nWholeFilePointer = 0;
    m_nWholeFileSize = n32BitSize;

    // store the result
    m_nOpenResult = nResult;
}

CWholeFileIO::~CWholeFileIO()
{
    m_spSource->Close();
    m_spSource.Delete();
}

int CWholeFileIO::Open(const wchar_t *, bool)
{
    return m_nOpenResult;
}

int CWholeFileIO::Close()
{
    return m_spSource->Close();
}

int CWholeFileIO::Read(void * pBuffer, unsigned int nBytesToRead, unsigned int * pBytesRead)
{
    bool bRetVal = true;

    *pBytesRead = 0; // reset

    const int64 nBytesLeft = GetSize() - m_nWholeFilePointer;
    nBytesToRead = ape_min(static_cast<unsigned int> (nBytesLeft), nBytesToRead);
    memcpy(pBuffer, &m_spWholeFile[m_nWholeFilePointer], nBytesToRead);
    m_nWholeFilePointer += nBytesToRead;
    *pBytesRead = nBytesToRead;

    // succeed if we actually read data then fail next call
    if ((bRetVal == false) && (*pBytesRead > 0))
        bRetVal = true;

    return bRetVal ? ERROR_SUCCESS : ERROR_IO_READ;
}

int CWholeFileIO::Write(const void * , unsigned int , unsigned int *)
{
    return ERROR_IO_WRITE;
}

int64 CWholeFileIO::PerformSeek()
{
    if (m_nSeekMethod == APE_FILE_BEGIN)
    {
        m_nWholeFilePointer = m_nSeekPosition;
    }
    else if (m_nSeekMethod == APE_FILE_CURRENT)
    {
        m_nWholeFilePointer += m_nSeekPosition;
    }
    else if (m_nSeekMethod == APE_FILE_END)
    {
        m_nWholeFilePointer = GetSize() - abs(m_nSeekPosition);
    }

    return ERROR_SUCCESS;
}

int CWholeFileIO::SetEOF()
{
    // handle memory buffers
    m_nWholeFileSize = m_nWholeFilePointer;

    // seek in the actual file to where we're at
    m_spSource->SetSeekMethod(APE_FILE_BEGIN);
    m_spSource->SetSeekPosition(m_nWholeFilePointer);
    m_spSource->PerformSeek();

    // set the EOF in the actual file
    return m_spSource->SetEOF();
}

int64 CWholeFileIO::GetPosition()
{
    return m_nWholeFilePointer;
}

int64 CWholeFileIO::GetSize()
{
    return m_nWholeFileSize;
}

int CWholeFileIO::GetName(wchar_t *)
{
    return ERROR_UNDEFINED;
}

int CWholeFileIO::Create(const wchar_t *)
{
    return ERROR_UNDEFINED;
}

int CWholeFileIO::Delete()
{
    return ERROR_UNDEFINED;
}

}
