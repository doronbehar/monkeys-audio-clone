#include "All.h"
#include "MACProgressHelper.h"
#include "MACLib.h"

namespace APE
{

CMACProgressHelper::CMACProgressHelper(int64 nTotalSteps, CAPEProgressCallbackInfo * pProgressCallback)
{
    m_pProgressCallback = pProgressCallback;

    m_nTotalSteps = nTotalSteps;
    m_nCurrentStep = 0;
    m_nLastCallbackFiredPercentageDone = 0;
    m_bKill = false;

    UpdateProgress(0);
}

void CMACProgressHelper::UpdateProgress(int64 nCurrentStep, bool bForceUpdate)
{
    // update the step
     if (nCurrentStep == -1)
         m_nCurrentStep++;
     else
        m_nCurrentStep = nCurrentStep;

    // figure the percentage done
    const double dPercentageDone = static_cast<double>(m_nCurrentStep) / static_cast<double>(APE_MAX<int64>(m_nTotalSteps, 1));
    int nPercentageDone = static_cast<int>((dPercentageDone * 1000 * 100));
    if (nPercentageDone > 100000) nPercentageDone = 100000;

    // fire the callback
    if (m_pProgressCallback != APE_NULL)
    {
        if (bForceUpdate || (nPercentageDone - m_nLastCallbackFiredPercentageDone) >= 1000)
        {
            if (m_pProgressCallback->m_pCallback != APE_NULL)
                m_pProgressCallback->m_pCallback->Progress(nPercentageDone);
            if (m_pProgressCallback->m_Callback != APE_NULL)
                m_pProgressCallback->m_Callback(m_pProgressCallback->m_CallbackData, nPercentageDone, &m_bKill);
            m_nLastCallbackFiredPercentageDone = nPercentageDone;
        }
    }
}

void CMACProgressHelper::UpdateProgressComplete()
{
    UpdateProgress(m_nTotalSteps, true);
}

int CMACProgressHelper::ProcessKillFlag()
{
    if (m_pProgressCallback && m_pProgressCallback->m_pCallback)
    {
        while (m_pProgressCallback->m_pCallback->GetKillFlag() == APE_KILL_FLAG_PAUSE)
        {
            SLEEP(50);
        }

        if ((m_pProgressCallback->m_pCallback->GetKillFlag() != APE_KILL_FLAG_CONTINUE) && (m_pProgressCallback->m_pCallback->GetKillFlag() != APE_KILL_FLAG_PAUSE))
        {
            return ERROR_UNDEFINED;
        }
    }
    else if (m_pProgressCallback && m_pProgressCallback->m_Callback)
    {
        if (m_bKill)
            return ERROR_UNDEFINED;
    }

    return ERROR_SUCCESS;
}

}
