#include "All.h"
#include "IO.h"

namespace APE
{

CIO::CIO()
{
    m_nSeekPosition = 0;
    m_nSeekMethod = APE_FILE_BEGIN;
}

void CIO::SetSeekPosition(int64 nPosition)
{
    m_nSeekPosition = nPosition;
}

void CIO::SetSeekMethod(unsigned int nMethod)
{
    m_nSeekMethod = nMethod;
}

}
