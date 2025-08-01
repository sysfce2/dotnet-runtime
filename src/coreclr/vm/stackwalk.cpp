// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// STACKWALK.CPP



#include "common.h"
#include "frames.h"
#include "threads.h"
#include "stackwalk.h"
#include "excep.h"
#include "eetwain.h"
#include "codeman.h"
#include "eeconfig.h"
#include "dbginterface.h"
#include "generics.h"
#ifdef FEATURE_INTERPRETER
#include "interpexec.h"
#endif // FEATURE_INTERPRETER

#include "gcinfodecoder.h"

#include "exinfo.h"

CrawlFrame::CrawlFrame()
{
    LIMITED_METHOD_DAC_CONTRACT;
    pCurGSCookie = NULL;
    pFirstGSCookie = NULL;
}

Assembly* CrawlFrame::GetAssembly()
{
    WRAPPER_NO_CONTRACT;

    Assembly *pAssembly = NULL;
    Frame *pF = GetFrame();

    if (pF != NULL)
        pAssembly = pF->GetAssembly();

    if (pAssembly == NULL && pFunc != NULL)
        pAssembly = pFunc->GetModule()->GetAssembly();

    return pAssembly;
}

BOOL CrawlFrame::IsInCalleesFrames(LPVOID stackPointer)
{
    LIMITED_METHOD_CONTRACT;
    return ::IsInCalleesFrames(GetRegisterSet(), stackPointer);
}

OBJECTREF CrawlFrame::GetThisPointer()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    if (!pFunc || pFunc->IsStatic() || pFunc->GetMethodTable()->IsValueType())
        return NULL;

    // As discussed in the specification comment at the declaration, the precondition, unfortunately,
    // differs by architecture.  @TODO: fix this.
#if defined(TARGET_X86)
    _ASSERTE_MSG((pFunc->IsSharedByGenericInstantiations() && pFunc->AcquiresInstMethodTableFromThis())
                 || pFunc->IsSynchronized(),
                 "Precondition");
#else
    _ASSERTE_MSG(pFunc->IsSharedByGenericInstantiations() && pFunc->AcquiresInstMethodTableFromThis(), "Precondition");
#endif

    if (isFrameless)
    {
        return GetCodeManager()->GetInstance(pRD,
                                            &codeInfo);
    }
    else
    {
        _ASSERTE(pFrame);
        _ASSERTE(pFunc);
        /*ISSUE: we already know that we have (at least) a method */
        /*       might need adjustment as soon as we solved the
                 jit-helper frame question
        */
        //<TODO>@TODO: What about other calling conventions?
//        _ASSERT(pFunc()->GetCallSig()->CALLING CONVENTION);</TODO>

#ifdef TARGET_AMD64
        // @TODO: PORT: we need to find the this pointer without triggering a GC
        //              or find a way to make this method GC_TRIGGERS
        return NULL;
#else
        return (dac_cast<PTR_FramedMethodFrame>(pFrame))->GetThis();
#endif // TARGET_AMD64
    }
}


//-----------------------------------------------------------------------------
// Get the "Ambient SP" from a  CrawlFrame.
// This will be null if there is no Ambient SP (eg, in the prolog / epilog,
// or on certain platforms),
//-----------------------------------------------------------------------------
TADDR CrawlFrame::GetAmbientSPFromCrawlFrame()
{
    SUPPORTS_DAC;
#if defined(TARGET_X86)
    // we set nesting level to zero because it won't be used for esp-framed methods,
    // and zero is at least valid for ebp based methods (where we won't use the ambient esp anyways)
    DWORD nestingLevel = 0;
    return GetCodeManager()->GetAmbientSP(
        GetRegisterSet(),
        GetCodeInfo(),
        GetRelOffset(),
        nestingLevel
        );

#elif defined(TARGET_ARM)
    return GetRegisterSet()->pCurrentContext->Sp;
#else
    return 0;
#endif
}


PTR_VOID CrawlFrame::GetParamTypeArg()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    if (isFrameless)
    {
        return GetCodeManager()->GetParamTypeArg(pRD,
                                                &codeInfo);
    }
    else
    {
        if (!pFunc || !pFunc->RequiresInstArg())
        {
            return NULL;
        }

#ifdef HOST_64BIT
        if (!pFunc->IsSharedByGenericInstantiations() ||
            !(pFunc->RequiresInstMethodTableArg() || pFunc->RequiresInstMethodDescArg()))
        {
            // win64 can only return the param type arg if the method is shared code
            // and actually has a param type arg
            return NULL;
        }
#endif // HOST_64BIT

        _ASSERTE(pFrame);
        _ASSERTE(pFunc);
        return (dac_cast<PTR_FramedMethodFrame>(pFrame))->GetParamTypeArg();
    }
}

PTR_VOID CrawlFrame::GetExactGenericArgsToken()
{

    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    MethodDesc* pFunc = GetFunction();

    if (!pFunc || !pFunc->IsSharedByGenericInstantiations())
        return NULL;

    if (pFunc->AcquiresInstMethodTableFromThis())
    {
        OBJECTREF obj = GetThisPointer();
        if (obj == NULL)
            return NULL;
        return obj->GetMethodTable();
    }
    else
    {
        _ASSERTE(pFunc->RequiresInstArg());
        return GetParamTypeArg();
    }
}

    /* Is this frame at a safe spot for GC?
     */
bool CrawlFrame::IsGcSafe()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    return GetCodeManager()->IsGcSafe(&codeInfo, GetRelOffset());
}

#if defined(TARGET_ARM) || defined(TARGET_ARM64) || defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
bool CrawlFrame::HasTailCalls()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    return GetCodeManager()->HasTailCalls(&codeInfo);
}
#endif // TARGET_ARM || TARGET_ARM64 || TARGET_LOONGARCH64 || TARGET_RISCV64

inline void CrawlFrame::GotoNextFrame()
{
    CONTRACTL {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    //
    // Update app domain if this frame caused a transition
    //

    pFrame = pFrame->Next();
}

//******************************************************************************

// For asynchronous stackwalks, the thread being walked may not be suspended.
// It could cause a buffer-overrun while the stack-walk is in progress.
// To detect this, we can only use data that is guarded by a GSCookie
// that has been recently checked.
// This function should be called after doing any time-consuming activity
// during stack-walking to reduce the window in which a buffer-overrun
// could cause an problems.
//
// To keep things simple, we do this checking even for synchronous stack-walks.
void CrawlFrame::CheckGSCookies()
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

#if !defined(DACCESS_COMPILE)
    if (pFirstGSCookie == NULL)
        return;

    if (*pFirstGSCookie != GetProcessGSCookie())
        DoJITFailFast();

    if(*pCurGSCookie   != GetProcessGSCookie())
        DoJITFailFast();
#endif // !DACCESS_COMPILE
}

void CrawlFrame::SetCurGSCookie(GSCookie * pGSCookie)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

#if !defined(DACCESS_COMPILE)
    if (pGSCookie == NULL)
        DoJITFailFast();

    pCurGSCookie = pGSCookie;
    if (pFirstGSCookie == NULL)
        pFirstGSCookie = pGSCookie;

    CheckGSCookies();
#endif // !DACCESS_COMPILE
}

#if defined(FEATURE_EH_FUNCLETS)
bool CrawlFrame::IsFilterFunclet()
{
    WRAPPER_NO_CONTRACT;

    if (!IsFrameless())
    {
        return false;
    }

    if (!isFilterFuncletCached)
    {
        isFilterFunclet = GetJitManager()->IsFilterFunclet(&codeInfo) != 0;
        isFilterFuncletCached = true;
    }

    return isFilterFunclet;
}

#endif // FEATURE_EH_FUNCLETS

//******************************************************************************
#if defined(ELIMINATE_FEF)
//******************************************************************************
// Advance to the next ExInfo.  Typically done when an ExInfo has been used and
//  should not be used again.
//******************************************************************************
void ExInfoWalker::WalkOne()
{
    LIMITED_METHOD_CONTRACT;
    SUPPORTS_DAC;

    if (m_pExInfo)
    {
        LOG((LF_EH, LL_INFO10000, "ExInfoWalker::WalkOne: advancing ExInfo chain: pExInfo:%p, pContext:%p; prev:%p, pContext:%p\n",
              m_pExInfo, m_pExInfo->m_pContext, m_pExInfo->m_pPrevNestedInfo, m_pExInfo->m_pPrevNestedInfo?m_pExInfo->m_pPrevNestedInfo->m_pContext:0));
        m_pExInfo = m_pExInfo->m_pPrevNestedInfo;
    }
} // void ExInfoWalker::WalkOne()

//******************************************************************************
// Attempt to find an ExInfo with a pContext that is higher (older) than
//  a given minimum location.  (It is the pContext's SP that is relevant.)
//******************************************************************************
void ExInfoWalker::WalkToPosition(
    TADDR       taMinimum,                  // Starting point of stack walk.
    BOOL        bPopFrames)                 // If true, ResetUseExInfoForStackwalk on each exinfo.
{
    LIMITED_METHOD_CONTRACT;
    SUPPORTS_DAC;

    while (m_pExInfo &&
           ((GetSPFromContext() < taMinimum) ||
            (GetSPFromContext() == NULL)) )
    {
        // Try the next ExInfo, if there is one.
        LOG((LF_EH, LL_INFO10000,
             "ExInfoWalker::WalkToPosition: searching ExInfo chain: m_pExInfo:%p, pContext:%p; \
              prev:%p, pContext:%p; pStartFrame:%p\n",
              m_pExInfo,
              m_pExInfo->m_pContext,
              m_pExInfo->m_pPrevNestedInfo,
              (m_pExInfo->m_pPrevNestedInfo ? m_pExInfo->m_pPrevNestedInfo->m_pContext : 0),
              taMinimum));

        if (bPopFrames)
        {   // If caller asked for it, reset the bit which indicates that this ExInfo marks a fault from managed code.
            //  This is done so that the fault can be effectively "unwound" from the stack, similarly to how Frames
            //  are unlinked from the Frame chain.
            m_pExInfo->m_ExceptionFlags.ResetUseExInfoForStackwalk();
        }
        m_pExInfo = m_pExInfo->m_pPrevNestedInfo;
    }
    // At this point, m_pExInfo is NULL, or points to a pContext that is greater than taMinimum.
} // void ExInfoWalker::WalkToPosition()

//******************************************************************************
// Attempt to find an ExInfo with a pContext that has an IP in managed code.
//******************************************************************************
void ExInfoWalker::WalkToManaged()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACTL_END;


    while (m_pExInfo)
    {
        // See if the current ExInfo has a CONTEXT that "returns" to managed code, and, if so, exit the loop.
        if (m_pExInfo->m_ExceptionFlags.UseExInfoForStackwalk() &&
            GetContext() &&
            ExecutionManager::IsManagedCode(GetIP(GetContext())))
        {
                break;
        }
        // No, so skip to next, if any.
        LOG((LF_EH, LL_INFO1000, "ExInfoWalker::WalkToManaged: searching for ExInfo->managed: m_pExInfo:%p, pContext:%p, sp:%p; prev:%p, pContext:%p\n",
              m_pExInfo,
              GetContext(),
              GetSPFromContext(),
              m_pExInfo->m_pPrevNestedInfo,
              m_pExInfo->m_pPrevNestedInfo?m_pExInfo->m_pPrevNestedInfo->m_pContext:0));
        m_pExInfo = m_pExInfo->m_pPrevNestedInfo;
    }
    // At this point, m_pExInfo is NULL, or points to a pContext that has an IP in managed code.
} // void ExInfoWalker::WalkToManaged()
#endif // defined(ELIMINATE_FEF)

#ifdef FEATURE_EH_FUNCLETS

// static
UINT_PTR Thread::VirtualUnwindCallFrame(PREGDISPLAY pRD, EECodeInfo* pCodeInfo /*= NULL*/)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;

        PRECONDITION(GetControlPC(pRD) == GetIP(pRD->pCurrentContext));
    }
    CONTRACTL_END;

#ifdef TARGET_X86
    EECodeInfo tempCodeInfo;
    if (pCodeInfo == NULL)
    {
        tempCodeInfo.Init(GetControlPC(pRD));
        pCodeInfo = &tempCodeInfo;
    }
#endif

    if (pRD->IsCallerContextValid)
    {
        // We already have the caller's frame context
        // We just switch the pointers
        PT_CONTEXT temp      = pRD->pCurrentContext;
        pRD->pCurrentContext = pRD->pCallerContext;
        pRD->pCallerContext  = temp;

        PT_KNONVOLATILE_CONTEXT_POINTERS tempPtrs = pRD->pCurrentContextPointers;
        pRD->pCurrentContextPointers            = pRD->pCallerContextPointers;
        pRD->pCallerContextPointers             = tempPtrs;

#ifdef TARGET_X86
        pRD->PCTAddr = pRD->pCurrentContext->Esp - pCodeInfo->GetCodeManager()->GetStackParameterSize(pCodeInfo) - sizeof(DWORD);
#endif
    }
    else
    {
#ifdef TARGET_X86
        hdrInfo *hdrInfoBody;
        PTR_CBYTE table = pCodeInfo->DecodeGCHdrInfo(&hdrInfoBody);

        ::UnwindStackFrameX86(pRD,
                            PTR_CBYTE(pCodeInfo->GetSavedMethodCode()),
                            pCodeInfo->GetRelOffset(),
                            hdrInfoBody,
                            table,
                            PTR_CBYTE(pCodeInfo->GetJitManager()->GetFuncletStartAddress(pCodeInfo)),
                            pCodeInfo->IsFunclet(),
                            true);

        pRD->pCurrentContext->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        pRD->pCurrentContext->Esp = pRD->SP;
        pRD->pCurrentContext->Eip = pRD->ControlPC;
#else
        VirtualUnwindCallFrame(pRD->pCurrentContext, pRD->pCurrentContextPointers, pCodeInfo);
#endif
    }

#if defined(TARGET_AMD64) && defined(TARGET_WINDOWS)
    if (pRD->SSP != 0)
    {
        pRD->SSP += 8;
    }
#endif // TARGET_AMD64 && TARGET_WINDOWS
    SyncRegDisplayToCurrentContext(pRD);
    pRD->IsCallerContextValid = FALSE;
    pRD->IsCallerSPValid      = FALSE;        // Don't add usage of this field.  This is only temporary.

    return pRD->ControlPC;
}


// static
PCODE Thread::VirtualUnwindCallFrame(T_CONTEXT* pContext,
                                        T_KNONVOLATILE_CONTEXT_POINTERS* pContextPointers /*= NULL*/,
                                        EECodeInfo * pCodeInfo /*= NULL*/)
{
#ifdef TARGET_WASM
    _ASSERTE("VirtualUnwindCallFrame is not supported on WebAssembly");
    return 0;
#else
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        PRECONDITION(CheckPointer(pContext, NULL_NOT_OK));
        PRECONDITION(CheckPointer(pContextPointers, NULL_OK));
        SUPPORTS_DAC;
    }
    CONTRACTL_END;

    PCODE           uControlPc = GetIP(pContext);

#if !defined(DACCESS_COMPILE)
#ifdef TARGET_X86
    EECodeInfo tempCodeInfo;
    if (pCodeInfo == NULL)
    {
        tempCodeInfo.Init(uControlPc);
        pCodeInfo = &tempCodeInfo;
    }

    REGDISPLAY rd;

    rd.SP = (DWORD)GetSP(pContext);
    rd.ControlPC = (DWORD)GetIP(pContext);
    rd.pCurrentContext = pContext;
    rd.pCurrentContextPointers = pContextPointers != NULL ? pContextPointers : &rd.ctxPtrsOne;

    hdrInfo *hdrInfoBody;
    PTR_CBYTE table = pCodeInfo->DecodeGCHdrInfo(&hdrInfoBody);

    ::UnwindStackFrameX86(&rd,
                          PTR_CBYTE(pCodeInfo->GetSavedMethodCode()),
                          pCodeInfo->GetRelOffset(),
                          hdrInfoBody,
                          table,
                          PTR_CBYTE(pCodeInfo->GetJitManager()->GetFuncletStartAddress(pCodeInfo)),
                          pCodeInfo->IsFunclet(),
                          true);

    pContext->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    pContext->Esp = rd.SP;
    pContext->Eip = rd.ControlPC;
    uControlPc = rd.ControlPC;
#else  // !TARGET_X86
    UINT_PTR            uImageBase;
    PT_RUNTIME_FUNCTION pFunctionEntry;

#if !defined(TARGET_UNIX) && defined(TARGET_ARM64)
    // We don't adjust the control PC when we have a code info, as the code info is always created from an unadjusted one
    // and the debug sanity check below would fail in case when a managed method was represented by multiple
    // RUNTIME_FUNCTION entries and the control PC and adjusted control PC happened to be represented by different
    // RUNTIME_FUNCTION entries.
    if ((pCodeInfo == NULL) && ((pContext->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0))
    {
        uControlPc -= STACKWALK_CONTROLPC_ADJUST_OFFSET;
    }
#endif // !TARGET_UNIX && TARGET_ARM64

    if (pCodeInfo == NULL)
    {
#ifndef TARGET_UNIX
        pFunctionEntry = RtlLookupFunctionEntry(uControlPc,
                                            ARM_ONLY((DWORD*))(&uImageBase),
                                            NULL);
#else // !TARGET_UNIX
        EECodeInfo codeInfo;

        codeInfo.Init(uControlPc);
        pFunctionEntry = codeInfo.GetFunctionEntry();
        uImageBase = (UINT_PTR)codeInfo.GetModuleBase();
#endif // !TARGET_UNIX
    }
    else
    {
        pFunctionEntry      = pCodeInfo->GetFunctionEntry();
        uImageBase          = (UINT_PTR)pCodeInfo->GetModuleBase();

        // RUNTIME_FUNCTION of cold code just points to the RUNTIME_FUNCTION of hot code. The unwinder
        // expects this indirection to be resolved, so we use RUNTIME_FUNCTION of the hot code even
        // if we are in cold code.

#if defined(_DEBUG) && !defined(TARGET_UNIX)
        UINT_PTR            uImageBaseFromOS;
        PT_RUNTIME_FUNCTION pFunctionEntryFromOS;

        pFunctionEntryFromOS  = RtlLookupFunctionEntry(uControlPc,
                                                       ARM_ONLY((DWORD*))(&uImageBaseFromOS),
                                                       NULL);

        // Note that he address returned from the OS is different from the one we have computed
        // when unwind info is registered using RtlAddGrowableFunctionTable. Compare RUNTIME_FUNCTION content.
        _ASSERTE( (uImageBase == uImageBaseFromOS) && (memcmp(pFunctionEntry, pFunctionEntryFromOS, sizeof(RUNTIME_FUNCTION)) == 0) );
#endif // _DEBUG && !TARGET_UNIX
    }

    if (pFunctionEntry)
    {
    #ifdef HOST_64BIT
        UINT64              EstablisherFrame;
    #else  // HOST_64BIT
        DWORD               EstablisherFrame;
    #endif // HOST_64BIT
        PVOID               HandlerData;

        RtlVirtualUnwind(0,
                         uImageBase,
                         uControlPc,
                         pFunctionEntry,
                         pContext,
                         &HandlerData,
                         &EstablisherFrame,
                         pContextPointers);

        uControlPc = GetIP(pContext);
    }
    else
    {
        uControlPc = VirtualUnwindLeafCallFrame(pContext);
    }
#endif // TARGET_X86
#else  // DACCESS_COMPILE
    // We can't use RtlVirtualUnwind() from out-of-process.  Instead, we call code:DacUnwindStackFrame,
    // which is similar to StackWalk64().
    if (DacUnwindStackFrame(pContext, pContextPointers) == TRUE)
    {
        uControlPc = GetIP(pContext);
    }
    else
    {
        ThrowHR(CORDBG_E_TARGET_INCONSISTENT);
    }
#endif // !DACCESS_COMPILE

    return uControlPc;
#endif // TARGET_WASM
}

#ifndef DACCESS_COMPILE

// static
PCODE Thread::VirtualUnwindLeafCallFrame(T_CONTEXT* pContext)
{
    PCODE uControlPc;

#if defined(_DEBUG) && defined(TARGET_WINDOWS) && !defined(TARGET_X86)
    UINT_PTR uImageBase;

    PT_RUNTIME_FUNCTION pFunctionEntry  = RtlLookupFunctionEntry((UINT_PTR)GetIP(pContext),
                                                                ARM_ONLY((DWORD*))(&uImageBase),
                                                                NULL);

    CONSISTENCY_CHECK(NULL == pFunctionEntry);
#endif // _DEBUG && TARGET_WINDOWS && !TARGET_X86

#if defined(TARGET_AMD64)

    uControlPc = *(ULONGLONG*)pContext->Rsp;
    pContext->Rsp += sizeof(ULONGLONG);
#ifdef TARGET_WINDOWS
    DWORD64 ssp = GetSSP(pContext);
    if (ssp != 0)
    {
        SetSSP(pContext, ssp + sizeof(ULONGLONG));
    }
#endif // TARGET_WINDOWS

#elif defined(TARGET_X86)

    uControlPc = *(TADDR*)pContext->Esp;
    pContext->Esp += sizeof(TADDR);

#elif defined(TARGET_ARM) || defined(TARGET_ARM64)

    uControlPc = TADDR(pContext->Lr);

#elif defined(TARGET_LOONGARCH64) || defined(TARGET_RISCV64)
    uControlPc = TADDR(pContext->Ra);

#else
    PORTABILITY_ASSERT("Thread::VirtualUnwindLeafCallFrame");
    uControlPc = NULL;
#endif

    SetIP(pContext, uControlPc);


    return uControlPc;
}

extern void* g_hostingApiReturnAddress;

// static
UINT_PTR Thread::VirtualUnwindToFirstManagedCallFrame(T_CONTEXT* pContext)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
    }
    CONTRACTL_END;

    PCODE uControlPc = GetIP(pContext);

    // unwind out of this function and out of our caller to
    // get our caller's PSP, or our caller's caller's SP.
    while (!ExecutionManager::IsManagedCode(uControlPc))
    {
        if (IsIPInWriteBarrierCodeCopy(uControlPc))
        {
            // Pretend we were executing the barrier function at its original location so that the unwinder can unwind the frame
            uControlPc = AdjustWriteBarrierIP(uControlPc);
            SetIP(pContext, uControlPc);
        }

#ifndef TARGET_UNIX
        uControlPc = VirtualUnwindCallFrame(pContext);
#else // !TARGET_UNIX

        if (AdjustContextForVirtualStub(NULL, pContext))
        {
            uControlPc = GetIP(pContext);
            break;
        }

        BOOL success = PAL_VirtualUnwind(pContext, NULL);
        if (!success)
        {
            _ASSERTE(!"Thread::VirtualUnwindToFirstManagedCallFrame: PAL_VirtualUnwind failed");
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
        }

        uControlPc = GetIP(pContext);

        if ((uControlPc == 0) || (uControlPc == (PCODE)g_hostingApiReturnAddress))
        {
            uControlPc = 0;
            break;
        }
#endif // !TARGET_UNIX
    }

    return uControlPc;
}

#endif // !DACCESS_COMPILE
#endif // FEATURE_EH_FUNCLETS

#ifdef _DEBUG
void Thread::DebugLogStackWalkInfo(CrawlFrame* pCF, _In_z_ LPCSTR pszTag, UINT32 uFramesProcessed)
{
    LIMITED_METHOD_CONTRACT;
    SUPPORTS_DAC;
    if (pCF->isFrameless)
    {
        LPCSTR pszType = "";

#ifdef FEATURE_EH_FUNCLETS
        if (pCF->IsFunclet())
        {
            pszType = "[funclet]";
        }
        else
#endif // FEATURE_EH_FUNCLETS
        if (pCF->pFunc->IsNoMetadata())
        {
            pszType = "[no metadata]";
        }

        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: [%03x] %s: FRAMELESS: PC=" FMT_ADDR " SP=" FMT_ADDR " method=%s %s\n",
                uFramesProcessed,
                pszTag,
                DBG_ADDR(GetControlPC(pCF->pRD)),
                DBG_ADDR(GetRegdisplaySP(pCF->pRD)),
                pCF->pFunc->m_pszDebugMethodName,
                pszType));
    }
    else if (pCF->isNativeMarker)
    {
        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: [%03x] %s: NATIVE   : PC=" FMT_ADDR " SP=" FMT_ADDR "\n",
                uFramesProcessed,
                pszTag,
                DBG_ADDR(GetControlPC(pCF->pRD)),
                DBG_ADDR(GetRegdisplaySP(pCF->pRD))));
    }
    else if (pCF->isNoFrameTransition)
    {
        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: [%03x] %s: NO_FRAME : PC=" FMT_ADDR " SP=" FMT_ADDR "\n",
                uFramesProcessed,
                pszTag,
                DBG_ADDR(GetControlPC(pCF->pRD)),
                DBG_ADDR(GetRegdisplaySP(pCF->pRD))));
    }
    else
    {
        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: [%03x] %s: EXPLICIT : PC=" FMT_ADDR " SP=" FMT_ADDR " Frame=" FMT_ADDR" FrameId=" FMT_ADDR "\n",
            uFramesProcessed,
            pszTag,
            DBG_ADDR(GetControlPC(pCF->pRD)),
            DBG_ADDR(GetRegdisplaySP(pCF->pRD)),
            DBG_ADDR(pCF->pFrame),
            DBG_ADDR((pCF->pFrame != FRAME_TOP) ? (TADDR)pCF->pFrame->GetFrameIdentifier() : (TADDR)NULL)));
    }
}
#endif // _DEBUG

StackWalkAction Thread::MakeStackwalkerCallback(
    CrawlFrame* pCF,
    PSTACKWALKFRAMESCALLBACK pCallback,
    VOID* pData
    DEBUG_ARG(UINT32 uFramesProcessed))
{
    INDEBUG(DebugLogStackWalkInfo(pCF, "CALLBACK", uFramesProcessed));

    // Since we may be asynchronously walking another thread's stack,
    // check (frequently) for stack-buffer-overrun corruptions
    pCF->CheckGSCookies();

    // Since the stackwalker callback may execute arbitrary managed code and possibly
    // not even return (in the case of exception unwinding), explicitly clear the
    // stackwalker thread state indicator around the callback.

    CLEAR_THREAD_TYPE_STACKWALKER();

    StackWalkAction swa = pCallback(pCF, (VOID*)pData);

    SET_THREAD_TYPE_STACKWALKER(this);

    pCF->CheckGSCookies();

#ifdef _DEBUG
    if (swa == SWA_ABORT)
    {
        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: SWA_ABORT: callback aborted the stackwalk\n"));
    }
#endif // _DEBUG

    return swa;
}


#if !defined(DACCESS_COMPILE) && defined(TARGET_X86) && !defined(FEATURE_EH_FUNCLETS)
#define STACKWALKER_MAY_POP_FRAMES
#endif


StackWalkAction Thread::StackWalkFramesEx(
                    PREGDISPLAY pRD,        // virtual register set at crawl start
                    PSTACKWALKFRAMESCALLBACK pCallback,
                    VOID *pData,
                    unsigned flags,
                    PTR_Frame pStartFrame
                )
{
    // Note: there are cases (i.e., exception handling) where we may never return from this function. This means
    // that any C++ destructors pushed in this function will never execute, and it means that this function can
    // never have a dynamic contract.
    STATIC_CONTRACT_WRAPPER;

    _ASSERTE(pRD);
    _ASSERTE(pCallback);

    // when POPFRAMES we don't want to allow GC trigger.
    // The only method that guarantees this now is COMPlusUnwindCallback
#ifdef STACKWALKER_MAY_POP_FRAMES
    ASSERT(!(flags & POPFRAMES) || pCallback == (PSTACKWALKFRAMESCALLBACK) COMPlusUnwindCallback);
    ASSERT(!(flags & POPFRAMES) || pRD->pContextForUnwind != NULL);
    ASSERT(!(flags & POPFRAMES) || (this == GetThread() && PreemptiveGCDisabled()));
#else // STACKWALKER_MAY_POP_FRAMES
    ASSERT(!(flags & POPFRAMES));
#endif // STACKWALKER_MAY_POP_FRAMES

    // We haven't set the stackwalker thread type flag yet, so it shouldn't be set. Only
    // exception to this is if the current call is made by a hijacking profiler which
    // redirected this thread while it was previously in the middle of another stack walk
#ifdef PROFILING_SUPPORTED
    _ASSERTE(CORProfilerStackSnapshotEnabled() || !IsStackWalkerThread());
#else
    _ASSERTE(!IsStackWalkerThread());
#endif

    StackWalkAction retVal = SWA_FAILED;

    {
        // SCOPE: Remember that we're walking the stack.
        //
        // Normally, we'd use a StackWalkerWalkingThreadHolder to temporarily set this
        // flag in the thread state, but we can't in this function, since C++ destructors
        // are forbidden when this is called for exception handling (which causes
        // MakeStackwalkerCallback() not to return). Note that in exception handling
        // cases, we will have already cleared the stack walker thread state indicator inside
        // MakeStackwalkerCallback(), so we will be properly cleaned up.
#if !defined(DACCESS_COMPILE)
        Thread* pStackWalkThreadOrig = t_pStackWalkerWalkingThread;
#endif
        SET_THREAD_TYPE_STACKWALKER(this);

        StackFrameIterator iter;
        if (iter.Init(this, pStartFrame, pRD, flags) == TRUE)
        {
            while (iter.IsValid())
            {
                retVal = MakeStackwalkerCallback(&iter.m_crawl, pCallback, pData DEBUG_ARG(iter.m_uFramesProcessed));
                if (retVal == SWA_ABORT)
                {
                    break;
                }

                retVal = iter.Next();
                if (retVal == SWA_FAILED)
                {
                    break;
                }
            }
        }

        SET_THREAD_TYPE_STACKWALKER(pStackWalkThreadOrig);
    }

    return retVal;
} // StackWalkAction Thread::StackWalkFramesEx()

StackWalkAction Thread::StackWalkFrames(PSTACKWALKFRAMESCALLBACK pCallback,
                               VOID *pData,
                               unsigned flags,
                               PTR_Frame pStartFrame)
{
    // Note: there are cases (i.e., exception handling) where we may never return from this function. This means
    // that any C++ destructors pushed in this function will never execute, and it means that this function can
    // never have a dynamic contract.
    STATIC_CONTRACT_WRAPPER;
    _ASSERTE((flags & THREAD_IS_SUSPENDED) == 0 || (flags & ALLOW_ASYNC_STACK_WALK));

    T_CONTEXT ctx;
    REGDISPLAY rd;
    bool fUseInitRegDisplay;

#ifndef DACCESS_COMPILE
    _ASSERTE(GetThreadNULLOk() == this || (flags & ALLOW_ASYNC_STACK_WALK));
    BOOL fDebuggerHasInitialContext = (GetFilterContext() != NULL);
    BOOL fProfilerHasInitialContext = (GetProfilerFilterContext() != NULL);

    // If this walk is seeded by a profiler, then the walk better be done by the profiler
    _ASSERTE(!fProfilerHasInitialContext || (flags & PROFILER_DO_STACK_SNAPSHOT));

    fUseInitRegDisplay              = fDebuggerHasInitialContext || fProfilerHasInitialContext;
#else
    fUseInitRegDisplay = true;
#endif

    if(fUseInitRegDisplay)
    {
        if (GetProfilerFilterContext() != NULL)
        {
            if (!InitRegDisplay(&rd, GetProfilerFilterContext(), TRUE))
            {
                LOG((LF_CORPROF, LL_INFO100, "**PROF: InitRegDisplay(&rd, GetProfilerFilterContext() failure leads to SWA_FAILED.\n"));
                return SWA_FAILED;
            }
        }
        else
        {
            if (!InitRegDisplay(&rd, &ctx, FALSE))
            {
                LOG((LF_CORPROF, LL_INFO100, "**PROF: InitRegDisplay(&rd, &ctx, FALSE) failure leads to SWA_FAILED.\n"));
                return SWA_FAILED;
            }
        }
    }
    else
    {
        // Initialize the context
        memset(&ctx, 0x00, sizeof(T_CONTEXT));
        LOG((LF_GCROOTS, LL_INFO100000, "STACKWALK    starting with partial context\n"));
        FillRegDisplay(&rd, &ctx, !!(flags & LIGHTUNWIND));
    }

#ifdef STACKWALKER_MAY_POP_FRAMES
    if (flags & POPFRAMES)
        rd.pContextForUnwind = &ctx;
#endif

    return StackWalkFramesEx(&rd, pCallback, pData, flags, pStartFrame);
}

// ----------------------------------------------------------------------------
// StackFrameIterator::StackFrameIterator
//
// Description:
//    This constructor is for the usage pattern of creating an uninitialized StackFrameIterator and then
//    calling Init() on it.
//
// Assumptions:
//    * The caller needs to call Init() with the correct arguments before using the StackFrameIterator.
//

StackFrameIterator::StackFrameIterator()
{
    LIMITED_METHOD_CONTRACT;
    SUPPORTS_DAC;
    CommonCtor(NULL, NULL, 0xbaadf00d);
} // StackFrameIterator::StackFrameIterator()

// ----------------------------------------------------------------------------
// StackFrameIterator::StackFrameIterator
//
// Description:
//    This constructor is for the usage pattern of creating an initialized StackFrameIterator and then
//    calling ResetRegDisp() on it.
//
// Arguments:
//    * pThread - the thread to walk
//    * pFrame  - the starting explicit frame; NULL means use the top explicit frame from the frame chain
//    * flags   - the stackwalk flags
//
// Assumptions:
//    * The caller can call ResetRegDisp() to use the StackFrameIterator without calling Init() first.
//

StackFrameIterator::StackFrameIterator(Thread * pThread, PTR_Frame pFrame, ULONG32 flags)
{
    SUPPORTS_DAC;
    CommonCtor(pThread, pFrame, flags);
} // StackFrameIterator::StackFrameIterator()

// ----------------------------------------------------------------------------
// StackFrameIterator::CommonCtor
//
// Description:
//    This is a helper for the two constructors.
//
// Arguments:
//    * pThread - the thread to walk
//    * pFrame  - the starting explicit frame; NULL means use the top explicit frame from the frame chain
//    * flags   - the stackwalk flags
//

void StackFrameIterator::CommonCtor(Thread * pThread, PTR_Frame pFrame, ULONG32 flags)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    INDEBUG(m_uFramesProcessed = 0);

    m_frameState = SFITER_UNINITIALIZED;
    m_pThread    = pThread;

    m_pStartFrame = pFrame;
#if defined(_DEBUG)
    if (m_pStartFrame != NULL)
    {
        m_pRealStartFrame = m_pStartFrame;
    }
    else if (m_pThread != NULL)
    {
        m_pRealStartFrame = m_pThread->GetFrame();
    }
    else
    {
        m_pRealStartFrame = NULL;
    }
#endif // _DEBUG

    m_flags        = flags;
    m_codeManFlags = (ICodeManagerFlags)0;

    m_pCachedGSCookie = NULL;

#if defined(FEATURE_EH_FUNCLETS)
    m_sfParent = StackFrame();
    ResetGCRefReportingState();
    m_fDidFuncletReportGCReferences = true;
    m_isRuntimeWrappedExceptions = false;
#endif // FEATURE_EH_FUNCLETS
    m_forceReportingWhileSkipping = ForceGCReportingStage::Off;
    m_movedPastFirstExInfo = false;
    m_fFuncletNotSeen = false;
    m_fFoundFirstFunclet = false;
#if defined(RECORD_RESUMABLE_FRAME_SP)
    m_pvResumableFrameTargetSP = NULL;
#endif
} // StackFrameIterator::CommonCtor()

//---------------------------------------------------------------------------------------
//
// Initialize the iterator.  Note that the iterator has thread-affinity,
// and the stackwalk flags cannot be changed once the iterator is created.
// Depending on the flags, initialization may involve unwinding to a frame of interest.
// The unwinding could fail.
//
// Arguments:
//    pThread  - the thread to walk
//    pFrame   - the starting explicit frame; NULL means use the top explicit frame from
//               pThread->GetFrame()
//    pRegDisp - the initial REGDISPLAY
//    flags    - the stackwalk flags
//
// Return Value:
//    Returns true if the initialization is successful.  The initialization could fail because
//    we fail to unwind.
//
// Notes:
//    Do not do anything funky between initializing a StackFrameIterator and actually using it.
//    In particular, do not resume the thread.  We only unhijack the thread once in Init().
//    Refer to StackWalkFramesEx() for the typical usage pattern.
//

BOOL StackFrameIterator::Init(Thread *    pThread,
                              PTR_Frame   pFrame,
                              PREGDISPLAY pRegDisp,
                              ULONG32     flags)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    _ASSERTE(pThread  != NULL);
    _ASSERTE(pRegDisp != NULL);

#ifdef FEATURE_EH_FUNCLETS
    _ASSERTE(!(flags & POPFRAMES));
    _ASSERTE(pRegDisp->pCurrentContext);
#endif // FEATURE_EH_FUNCLETS

    BEGIN_FORBID_TYPELOAD();

#ifdef FEATURE_HIJACK
    // We can't crawl the stack of a thread that currently has a hijack pending
    // (since the hijack routine won't be recognized by any code manager). So we
    // undo any hijack, the EE will re-attempt it later.

#if !defined(DACCESS_COMPILE)
    // OOP stackwalks need to deal with hijacked threads in a special way.
    pThread->UnhijackThread();
#endif // !DACCESS_COMPILE

#endif // FEATURE_HIJACK

    // FRAME_TOP must not be 0/NULL.
    static_assert_no_msg(FRAME_TOP_VALUE != 0);

    m_frameState = SFITER_UNINITIALIZED;

    m_pThread = pThread;
    m_flags   = flags;

    ResetCrawlFrame();

    m_pStartFrame = pFrame;
    if (m_pStartFrame)
    {
        m_crawl.pFrame = m_pStartFrame;
    }
    else
    {
        m_crawl.pFrame = m_pThread->GetFrame();
        _ASSERTE(m_crawl.pFrame != NULL);
    }
    INDEBUG(m_pRealStartFrame = m_crawl.pFrame);

    m_crawl.pRD = pRegDisp;

    m_codeManFlags = (ICodeManagerFlags)
        (((flags & (QUICKUNWIND | LIGHTUNWIND)) ? 0 : UpdateAllRegs) | ((flags & LIGHTUNWIND) ? LightUnwind : 0));
    m_scanFlag = ExecutionManager::GetScanFlags();

#if defined(ELIMINATE_FEF)
    // Walk the ExInfo chain, past any specified starting frame.
    m_exInfoWalk.Init(&(pThread->GetExceptionState()->m_currentExInfo));
    // false means don't reset UseExInfoForStackwalk
    m_exInfoWalk.WalkToPosition(dac_cast<TADDR>(m_pStartFrame), false);
#endif // ELIMINATE_FEF

#ifdef FEATURE_EH_FUNCLETS

    m_pNextExInfo = (PTR_ExInfo)pThread->GetExceptionState()->GetCurrentExceptionTracker();
#endif // FEATURE_EH_FUNCLETS

    //
    // These fields are used in the iteration and will be updated on a per-frame basis:
    //
    // EECodeInfo     m_cachedCodeInfo;
    //
    // GSCookie *     m_pCachedGSCookie;
    //
    // StackFrame     m_sfParent;
    //
    // LPVOID         m_pvResumableFrameTargetSP;
    //

    // process the REGDISPLAY and stop at the first frame
    ProcessIp(GetControlPC(m_crawl.pRD));
#ifdef FEATURE_EH_FUNCLETS
    if (m_crawl.isFrameless && !!(m_crawl.pRD->pCurrentContext->ContextFlags & CONTEXT_EXCEPTION_ACTIVE))
    {
        m_crawl.hasFaulted = true;
    }
#endif // FEATURE_EH_FUNCLETS
    ProcessCurrentFrame();

    // advance to the next frame which matches the stackwalk flags
    StackWalkAction retVal = Filter();

    END_FORBID_TYPELOAD();

    return (retVal == SWA_CONTINUE);
} // StackFrameIterator::Init()

//---------------------------------------------------------------------------------------
//
// Reset the stackwalk iterator with the specified REGDISPLAY.
// The caller is responsible for making sure the REGDISPLAY is valid.
// This function is very similar to Init(), except that this function takes a REGDISPLAY
// to seed the stackwalk.  This function may also unwind depending on the flags, and the
// unwinding may fail.
//
// Arguments:
//    pRegDisp - new REGDISPLAY
//    bool     - whether the REGDISPLAY is for the leaf frame
//
// Return Value:
//    Returns true if the reset is successful.  The reset could fail because
//    we fail to unwind.
//
// Assumptions:
//    The REGDISPLAY is valid for the thread which the iterator has affinity to.
//

BOOL StackFrameIterator::ResetRegDisp(PREGDISPLAY pRegDisp,
                                      bool        fIsFirst)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    // It is invalid to reset a stackwalk if we are popping frames along the way.
    ASSERT(!(m_flags & POPFRAMES));

    BEGIN_FORBID_TYPELOAD();

    m_frameState = SFITER_UNINITIALIZED;

    // Make sure the StackFrameIterator has been initialized properly.
    _ASSERTE(m_pThread != NULL);
    _ASSERTE(m_flags != 0xbaadf00d);

    ResetCrawlFrame();

    m_crawl.isFirst = fIsFirst;

    if (m_pStartFrame)
    {
        m_crawl.pFrame = m_pStartFrame;
    }
    else
    {
        m_crawl.pFrame = m_pThread->GetFrame();
        _ASSERTE(m_crawl.pFrame != NULL);
    }

    m_crawl.pRD = pRegDisp;

    m_codeManFlags = (ICodeManagerFlags)
        (((m_flags & (QUICKUNWIND | LIGHTUNWIND)) ? 0 : UpdateAllRegs) | ((m_flags & LIGHTUNWIND) ? LightUnwind : 0));

    // make sure the REGDISPLAY is synchronized with the CONTEXT
    UpdateRegDisp();

    PCODE curPc = GetControlPC(pRegDisp);
    ProcessIp(curPc);

    // loop the frame chain to find the closet explicit frame which is lower than the specified REGDISPLAY
    // (stack grows up towards lower address)
    if (m_crawl.pFrame != FRAME_TOP)
    {
        TADDR curSP = GetRegdisplaySP(m_crawl.pRD);

#ifdef PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
        if (m_crawl.IsFrameless())
        {
            // On 64-bit and ARM, we stop at the explicit frames contained in a managed stack frame
            // before the managed stack frame itself.
            m_crawl.GetCodeManager()->EnsureCallerContextIsValid(m_crawl.pRD, NULL, m_codeManFlags);
            curSP = GetSP(m_crawl.pRD->pCallerContext);
        }
#endif // PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME

#if defined(TARGET_X86)
        // special processing on x86; see below for more information
        TADDR curEBP = GetRegdisplayFP(m_crawl.pRD);

        CONTEXT    tmpCtx;
        REGDISPLAY tmpRD;
        CopyRegDisplay(m_crawl.pRD, &tmpRD, &tmpCtx);
#endif // TARGET_X86

        //
        // The basic idea is to loop the frame chain until we find an explicit frame whose address is below
        // (close to the root) the SP in the specified REGDISPLAY.  This works well on WIN64 platforms.
        // However, on x86, in M2U transitions, the Windows debuggers will pass us an incorrect REGDISPLAY
        // for the managed stack frame at the M2U boundary.  The REGDISPLAY is obtained by unwinding the
        // marshaling stub, and it contains an SP which is actually higher (closer to the leaf) than the
        // address of the transition frame.  It is as if the explicit frame is not contained in the stack
        // frame of any method.  Here's an example:
        //
        // ChildEBP
        // 0012e884 ntdll32!DbgBreakPoint
        // 0012e89c CLRStub[StubLinkStub]@1f0ac1e
        // 0012e8a4     invalid ESP of Foo() according to the REGDISPLAY specified by the debuggers
        // 0012e8b4     address of transition frame (PInvokeMethodFrameStandalone)
        // 0012e8c8     real ESP of Foo() according to the transition frame
        // 0012e8d8 managed!Dummy.Foo()+0x20
        //
        // The original implementation of ResetRegDisp() compares the return address of the transition frame
        // and the IP in the specified REGDISPLAY to work around this problem.  However, even this comparison
        // is not enough because we may have recursive pinvoke calls on the stack (albeit an unlikely
        // scenario).  So in addition to the IP comparison, we also check EBP.  Note that this does not
        // require managed stack frames to be EBP-framed.
        //

        while (m_crawl.pFrame != FRAME_TOP)
        {
            // this check is sufficient on WIN64
            if (dac_cast<TADDR>(m_crawl.pFrame) >= curSP)
            {
#if defined(TARGET_X86)
                // check the IP
                if (m_crawl.pFrame->GetReturnAddress() != curPc)
                {
                    break;
                }
                else
                {
                    // unwind the REGDISPLAY using the transition frame and check the EBP
                    m_crawl.pFrame->UpdateRegDisplay(&tmpRD, m_flags & UNWIND_FLOATS);
                    if (GetRegdisplayFP(&tmpRD) != curEBP)
                    {
                        break;
                    }
                }
#else  // !TARGET_X86
                break;
#endif // !TARGET_X86
            }

            // if the REGDISPLAY represents the managed stack frame at a M2U transition boundary,
            // update the flags on the CrawlFrame and the REGDISPLAY
            PCODE frameRetAddr = m_crawl.pFrame->GetReturnAddress();
            if (frameRetAddr == curPc)
            {
                unsigned uFrameAttribs = m_crawl.pFrame->GetFrameAttribs();

                m_crawl.isFirst       = ((uFrameAttribs & Frame::FRAME_ATTR_RESUMABLE) != 0);
                m_crawl.isInterrupted = ((uFrameAttribs & Frame::FRAME_ATTR_EXCEPTION) != 0);

                if (m_crawl.isInterrupted)
                {
                    m_crawl.hasFaulted   = ((uFrameAttribs & Frame::FRAME_ATTR_FAULTED) != 0);
                    m_crawl.isIPadjusted = false;
                }

                m_crawl.pFrame->UpdateRegDisplay(m_crawl.pRD, m_flags & UNWIND_FLOATS);
                _ASSERTE(curPc == GetControlPC(m_crawl.pRD));
            }

            m_crawl.GotoNextFrame();
        }
    }

#if defined(ELIMINATE_FEF)
    // Similarly, we need to walk the ExInfos.
    m_exInfoWalk.Init(&(m_crawl.pThread->GetExceptionState()->m_currentExInfo));
    // false means don't reset UseExInfoForStackwalk
    m_exInfoWalk.WalkToPosition(GetRegdisplaySP(m_crawl.pRD), false);
#endif // ELIMINATE_FEF

    // now that everything is at where it should be, update the CrawlFrame
    ProcessCurrentFrame();

    // advance to the next frame which matches the stackwalk flags
    StackWalkAction retVal = Filter();

    END_FORBID_TYPELOAD();

    return (retVal == SWA_CONTINUE);
} // StackFrameIterator::ResetRegDisp()


//---------------------------------------------------------------------------------------
//
// Reset the CrawlFrame owned by the iterator.  Used by both Init() and ResetRegDisp().
//
// Assumptions:
//    this->m_pThread and this->m_flags have been initialized.
//
// Notes:
//    In addition, the following fields are not reset.  The caller must update them:
//    pFrame, pFunc, pAppDomain, pRD
//
//    Fields updated by ProcessIp():
//    isFrameless, and codeInfo
//

void StackFrameIterator::ResetCrawlFrame()
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    INDEBUG(memset(&(m_crawl.pFunc), 0xCC, sizeof(m_crawl.pFunc)));

    m_crawl.isFirst = true;
    m_crawl.isInterrupted = false;
    m_crawl.hasFaulted = false;
    m_crawl.isIPadjusted = false;

    m_crawl.isNativeMarker = false;
    m_crawl.isProfilerDoStackSnapshot = !!(this->m_flags & PROFILER_DO_STACK_SNAPSHOT);
    m_crawl.isNoFrameTransition = false;

    m_crawl.taNoFrameTransitionMarker = (TADDR)NULL;

#if defined(FEATURE_EH_FUNCLETS)
    m_crawl.isFilterFunclet       = false;
    m_crawl.isFilterFuncletCached = false;
    m_crawl.fShouldParentToFuncletSkipReportingGCReferences = false;
    m_crawl.fShouldParentFrameUseUnwindTargetPCforGCReporting = false;
    m_crawl.fShouldSaveFuncletInfo = false;
    m_crawl.fShouldParentToFuncletReportSavedFuncletSlots = false;
#endif // FEATURE_EH_FUNCLETS

    m_crawl.pThread = this->m_pThread;

    m_crawl.pCurGSCookie   = NULL;
    m_crawl.pFirstGSCookie = NULL;
}

//---------------------------------------------------------------------------------------
//
// This function represents whether the iterator has reached the root of the stack or not.
// It can be used as the loop-terminating condition for the iterator.
//
// Return Value:
//    Returns true if there is more frames on the stack to walk.
//

BOOL StackFrameIterator::IsValid(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    // There is more to iterate if the stackwalk is currently in managed code,
    //  or if there are frames left.
    // If there is an ExInfo with a pContext, it may substitute for a Frame,
    //  if the ExInfo is due to an exception in managed code.
    if (!m_crawl.isFrameless && m_crawl.pFrame == FRAME_TOP)
    {
        // if we are stopped at a native marker frame, we can still advance at least once more
        if (m_frameState == SFITER_NATIVE_MARKER_FRAME)
        {
            _ASSERTE(m_crawl.isNativeMarker);
            return TRUE;
        }

#if defined(ELIMINATE_FEF)
        // Not in managed code, and no frames left -- check for an ExInfo.
        // @todo: check for exception?
        m_exInfoWalk.WalkToManaged();
        if (m_exInfoWalk.GetContext())
            return TRUE;
#endif // ELIMINATE_FEF

#ifdef _DEBUG
        // Try to ensure that the frame chain did not change underneath us.
        // In particular, is thread's starting frame the same as it was when
        // we started?
        BOOL bIsRealStartFrameUnchanged =
            (m_pStartFrame != NULL)
            || (m_flags & POPFRAMES)
            || (m_pRealStartFrame == m_pThread->GetFrame());

#ifdef FEATURE_HIJACK
        // In GCStress >= 4 two threads could race on triggering GC;
        // if the one that just made p/invoke call is second and hits the trap instruction
        // before call to synchronize with GC, it will push a frame [ResumableFrame on Unix
        // and RedirectedThreadFrame on Windows] concurrently with GC stackwalking.
        // In normal case (no GCStress), after p/invoke, IL_STUB will check if GC is in progress and synchronize.
        // NOTE: This condition needs to be evaluated after the previous one to prevent a subtle race condition
        // (https://github.com/dotnet/runtime/issues/11678)
        if (bIsRealStartFrameUnchanged == FALSE)
        {
            _ASSERTE(GCStress<cfg_instr>::IsEnabled());
            _ASSERTE(m_pRealStartFrame != NULL);
            _ASSERTE(m_pRealStartFrame != FRAME_TOP);
            _ASSERTE(m_pRealStartFrame->GetFrameIdentifier() == FrameIdentifier::InlinedCallFrame);
            _ASSERTE(m_pThread->GetFrame() != NULL);
            _ASSERTE(m_pThread->GetFrame() != FRAME_TOP);
            bIsRealStartFrameUnchanged = (m_pThread->GetFrame()->GetFrameIdentifier() == FrameIdentifier::ResumableFrame)
                || (m_pThread->GetFrame()->GetFrameIdentifier() == FrameIdentifier::RedirectedThreadFrame);
        }
#endif // FEATURE_HIJACK

        _ASSERTE(bIsRealStartFrameUnchanged);

#endif //_DEBUG

        return FALSE;
    }

    return TRUE;
} // StackFrameIterator::IsValid()

#ifndef DACCESS_COMPILE
#ifdef FEATURE_EH_FUNCLETS
//---------------------------------------------------------------------------------------
//
// Advance to the position that the other iterator is currently at.
//
void StackFrameIterator::SkipTo(StackFrameIterator *pOtherStackFrameIterator)
{
    // We copy the other stack frame iterator over the current one, but we need to
    // keep a couple of members untouched. So we save them here and restore them
    // after the copy.
    ExInfo* pPrevExInfo = GetNextExInfo();
    REGDISPLAY *pRD = m_crawl.GetRegisterSet();
    Frame *pStartFrame = m_pStartFrame;
#ifdef _DEBUG
    Frame *pRealStartFrame = m_pRealStartFrame;
#endif

    *this = *pOtherStackFrameIterator;

    m_pNextExInfo = pPrevExInfo;
    m_crawl.pRD = pRD;
    m_pStartFrame = pStartFrame;
#ifdef _DEBUG
    m_pRealStartFrame = pRealStartFrame;
#endif

    REGDISPLAY *pOtherRD = pOtherStackFrameIterator->m_crawl.GetRegisterSet();
    *pRD->pCurrentContextPointers = *pOtherRD->pCurrentContextPointers;
    SetIP(pRD->pCurrentContext, GetIP(pOtherRD->pCurrentContext));
    SetSP(pRD->pCurrentContext, GetSP(pOtherRD->pCurrentContext));
#if defined(TARGET_AMD64) && defined(TARGET_WINDOWS)
    pRD->SSP = pOtherRD->SSP;
#endif

#define CALLEE_SAVED_REGISTER(regname) pRD->pCurrentContext->regname = (pRD->pCurrentContextPointers->regname == NULL) ? pOtherRD->pCurrentContext->regname : *pRD->pCurrentContextPointers->regname;
    ENUM_CALLEE_SAVED_REGISTERS();
#undef CALLEE_SAVED_REGISTER

#define CALLEE_SAVED_REGISTER(regname) pRD->pCurrentContext->regname = pOtherRD->pCurrentContext->regname;
    ENUM_FP_CALLEE_SAVED_REGISTERS();
#undef CALLEE_SAVED_REGISTER

    pRD->IsCallerContextValid = pOtherRD->IsCallerContextValid;
    if (pRD->IsCallerContextValid)
    {
        *pRD->pCallerContextPointers = *pOtherRD->pCallerContextPointers;
        SetIP(pRD->pCallerContext, GetIP(pOtherRD->pCallerContext));
        SetSP(pRD->pCallerContext, GetSP(pOtherRD->pCallerContext));

#define CALLEE_SAVED_REGISTER(regname) pRD->pCallerContext->regname = (pRD->pCallerContextPointers->regname == NULL) ? pOtherRD->pCallerContext->regname : *pRD->pCallerContextPointers->regname;
        ENUM_CALLEE_SAVED_REGISTERS();
#undef CALLEE_SAVED_REGISTER

#define CALLEE_SAVED_REGISTER(regname) pRD->pCallerContext->regname = pOtherRD->pCallerContext->regname;
        ENUM_FP_CALLEE_SAVED_REGISTERS();
#undef CALLEE_SAVED_REGISTER
    }
    SyncRegDisplayToCurrentContext(pRD);
}
#endif // FEATURE_EH_FUNCLETS
#endif // DACCESS_COMPILE

//---------------------------------------------------------------------------------------
//
// Advance to the next frame according to the stackwalk flags.  If the iterator is stopped
// at some place not specified by the stackwalk flags, this function will automatically advance
// to the next frame.
//
// Return Value:
//    SWA_CONTINUE (== SWA_DONE) if the iterator is successful in advancing to the next frame
//    SWA_FAILED if an operation performed by the iterator fails
//
// Notes:
//    This function returns SWA_DONE when advancing from the last frame to becoming invalid.
//    It returns SWA_FAILED if the iterator is invalid.
//

StackWalkAction StackFrameIterator::Next(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    if (!IsValid())
    {
        return SWA_FAILED;
    }

    BEGIN_FORBID_TYPELOAD();

    StackWalkAction retVal = NextRaw();
    if (retVal == SWA_CONTINUE)
    {
        retVal = Filter();
    }

    END_FORBID_TYPELOAD();
    return retVal;
}

//---------------------------------------------------------------------------------------
//
// Check whether we should stop at the current frame given the stackwalk flags.
// If not, continue advancing to the next frame.
//
// Return Value:
//    Returns SWA_CONTINUE (== SWA_DONE) if the iterator is invalid or if no automatic advancing is done.
//    Otherwise returns whatever the last call to NextRaw() returns.
//

StackWalkAction StackFrameIterator::Filter(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    bool fStop            = false;
    bool fSkippingFunclet = false;

#if defined(FEATURE_EH_FUNCLETS)
    bool fRecheckCurrentFrame = false;
    bool fSkipFuncletCallback = true;
#endif // defined(FEATURE_EH_FUNCLETS)

    StackWalkAction retVal = SWA_CONTINUE;

    while (IsValid())
    {
        fStop = false;
        fSkippingFunclet = false;

#if defined(FEATURE_EH_FUNCLETS)
        ExInfo* pExInfo = NULL;

        pExInfo = (PTR_ExInfo)m_crawl.pThread->GetExceptionState()->GetCurrentExceptionTracker();

        fRecheckCurrentFrame = false;
        fSkipFuncletCallback = true;

        SIZE_T frameSP = (m_frameState == SFITER_FRAME_FUNCTION) ? (SIZE_T)dac_cast<TADDR>(m_crawl.pFrame) : m_crawl.GetRegisterSet()->SP;

        if ((m_flags & GC_FUNCLET_REFERENCE_REPORTING) && (pExInfo != NULL) && (frameSP > (SIZE_T)pExInfo))
        {
            if (!m_movedPastFirstExInfo)
            {
                if ((pExInfo->m_passNumber == 2) && !pExInfo->m_csfEnclosingClause.IsNull() && m_sfFuncletParent.IsNull() && pExInfo->m_lastReportedFunclet.IP != 0)
                {
                    // We are in the 2nd pass and we have already called an exceptionally called
                    // finally funclet and reported that to GC in a previous GC run. But we have
                    // not seen any funclet on the call stack yet.
                    // Simulate that we have actualy seen a finally funclet during this pass and
                    // that it didn't report GC references to ensure that the references will be
                    // reported by the parent correctly.
                    m_sfFuncletParent = (StackFrame)pExInfo->m_csfEnclosingClause;
                    m_sfParent = m_sfFuncletParent;
                    m_fProcessNonFilterFunclet = true;
                    m_fDidFuncletReportGCReferences = false;
                    m_fFuncletNotSeen = true;
                    STRESS_LOG3(LF_GCROOTS, LL_INFO100,
                                        "STACKWALK: Moved over first ExInfo @ %p in second pass, SP: %p, Enclosing clause: %p\n",
                                        pExInfo, (void*)m_crawl.GetRegisterSet()->SP, (void*)m_sfFuncletParent.SP);
                }
                m_movedPastFirstExInfo = true;
            }
        }

        m_crawl.fShouldParentToFuncletReportSavedFuncletSlots = false;

        // by default, there is no funclet for the current frame
        // that reported GC references
        m_crawl.fShouldParentToFuncletSkipReportingGCReferences = false;

        // By default, assume that we are going to report GC references for this
        // CrawlFrame
        m_crawl.fShouldCrawlframeReportGCReferences = true;

        m_crawl.fShouldSaveFuncletInfo = false;

        // By default, assume that parent frame is going to report GC references from
        // the actual location reported by the stack walk.
        m_crawl.fShouldParentFrameUseUnwindTargetPCforGCReporting = false;

        if (!m_sfParent.IsNull())
        {
            // we are now skipping frames to get to the funclet's parent
            fSkippingFunclet = true;
        }
#endif // FEATURE_EH_FUNCLETS

        switch (m_frameState)
        {
            case SFITER_FRAMELESS_METHOD:
#if defined(FEATURE_EH_FUNCLETS)
ProcessFuncletsForGCReporting:
                do
                {
                    // The funclet reports all references belonging to itself and its parent method.
                    //
                    // The GcStackCrawlCallBack is invoked with a new flag indicating that the stackwalk is being done
                    // for GC reporting purposes - this flag is GC_FUNCLET_REFERENCE_REPORTING.
                    // The presence of this flag influences how the stackwalker will enumerate frames; which frames will
                    // result in the callback being invoked; etc. The idea is that we want to report only the
                    // relevant frames via the callback that are active on the callstack. This removes the need to
                    // double report, reporting of dead frames, and makes the design of reference reporting more
                    // consistent (and easier to understand) across architectures.
                    //
                    // The algorithm is as follows (at a conceptual level):
                    //
                    // 1) For each enumerated managed (frameless) frame, check if it is a funclet or not.
                    //  1.1) If it is not a funclet, pass the frame to the callback and goto (2).
                    //  1.2) If it is a funclet, we preserve the callerSP of the parent frame where the funclet was invoked from.
                    //       Pass the funclet to the callback.
                    //  1.3) For filter funclets, we enumerate all frames until we reach the parent. Once the parent is reached,
                    //       pass it to the callback with a flag indicating that its corresponding funclet has already performed
                    //       the reporting.
                    //  1.4) For non-filter funclets, we skip all the frames until we reach the parent. Once the parent is reached,
                    //       pass it to the callback with a flag indicating that its corresponding funclet has already performed
                    //       the reporting.
                    //  1.5) If we see non-filter funclets while processing a filter funclet, then goto (1.4). Once we have reached the
                    //       parent of the non-filter funclet, resume filter funclet processing as described in (1.3).
                    // 2) If another frame is enumerated, goto (1). Otherwise, stackwalk is complete.
                    //
                    // Note: When a flag is passed to the callback indicating that the funclet for a parent frame has already
                    //       reported the references, RyuJIT will simply do nothing and return from the callback.
                    //
                    // Note: For non-filter funclets there is a small window during unwind where we have conceptually unwound past a
                    //       funclet but have not yet reached the parent/handling frame.  In this case we might need the parent to
                    //       report its GC roots.  See comments around use of m_fDidFuncletReportGCReferences for more details.
                    //
                    // Needless to say, all applicable (read: active) explicit frames are also processed.

                    // Check if we are in the mode of enumerating GC references (or not)
                    if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                    {
                        fRecheckCurrentFrame = false;
                        // Do we already have a reference to a funclet parent?
                        if (!m_sfFuncletParent.IsNull())
                        {
                            // Have we been processing a filter funclet without encountering any non-filter funclets?
                            if ((m_fProcessNonFilterFunclet == false) && (m_fProcessIntermediaryNonFilterFunclet == false))
                            {
                                // Yes, we have. Check the current frame and if it is the parent we are looking for,
                                // clear the flag indicating that its funclet has already reported the GC references (see
                                // below comment for Dev11 376329 explaining why we do this).
                                if (ExInfo::IsUnwoundToTargetParentFrame(&m_crawl, m_sfFuncletParent))
                                {
                                    STRESS_LOG2(LF_GCROOTS, LL_INFO100,
                                    "STACKWALK: Reached parent of filter funclet @ CallerSP: %p, m_crawl.pFunc = %p\n",
                                    m_sfFuncletParent.SP, m_crawl.pFunc);

                                    // Dev11 376329 - ARM: GC hole during filter funclet dispatch.
                                    // Filters are invoked during the first pass so we cannot skip
                                    // reporting the parent frame since it's still live.  Normally
                                    // this would cause double reporting, however for filters the JIT
                                    // will report all GC roots as pinned to alleviate this problem.
                                    // Note that JIT64 does not have this problem since it always
                                    // reports the parent frame (this flag is essentially ignored)
                                    // so it's safe to make this change for all (non-x86) architectures.
                                    m_crawl.fShouldParentToFuncletSkipReportingGCReferences = false;
                                    ResetGCRefReportingState();

                                    // We have reached the parent of the filter funclet.
                                    // It is possible this is another funclet (e.g. a catch/fault/finally),
                                    // so reexamine this frame and see if it needs any skipping.
                                    fRecheckCurrentFrame = true;
                                }
                                else
                                {
                                    // When processing filter funclets, until we reach the parent frame
                                    // we should be seeing only non--filter-funclet frames. This is because
                                    // exceptions cannot escape filter funclets. Thus, there can be no frameless frames
                                    // between the filter funclet and its parent.
                                    _ASSERTE(!m_crawl.IsFilterFunclet());
                                    if (m_crawl.IsFunclet())
                                    {
                                        // This is a non-filter funclet encountered when processing a filter funclet.
                                        // In such a case, we will deliver a callback for it and skip frames until we reach
                                        // its parent. Once there, we will resume frame enumeration for finding
                                        // parent of the filter funclet we were originally processing.
                                        m_sfIntermediaryFuncletParent = ExInfo::FindParentStackFrameForStackWalk(&m_crawl, true);
                                        _ASSERTE(!m_sfIntermediaryFuncletParent.IsNull());
                                        m_fProcessIntermediaryNonFilterFunclet = true;

                                        // Set the parent frame so that the funclet skipping logic (further below)
                                        // can use it.
                                        m_sfParent = m_sfIntermediaryFuncletParent;
                                        fSkipFuncletCallback = false;

                                        if (!ExecutionManager::IsManagedCode(GetIP(m_crawl.GetRegisterSet()->pCallerContext)))
                                        {
                                            // Initiate force reporting of references in the new managed exception handling code frames.
                                            // These frames are still alive when we are in a finally funclet.
                                            m_forceReportingWhileSkipping = ForceGCReportingStage::LookForManagedFrame;
                                            STRESS_LOG0(LF_GCROOTS, LL_INFO100, "STACKWALK: Setting m_forceReportingWhileSkipping = ForceGCReportingStage::LookForManagedFrame while processing filter funclet\n");
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            _ASSERTE(m_sfFuncletParent.IsNull());

                            // We don't have any funclet parent reference. Check if the current frame represents a funclet.
                            if (m_crawl.IsFunclet())
                            {
                                // Get a reference to the funclet's parent frame.
                                m_sfFuncletParent = ExInfo::FindParentStackFrameForStackWalk(&m_crawl, true);

                                bool fFrameWasUnwound = ExInfo::HasFrameBeenUnwoundByAnyActiveException(&m_crawl);
                                if (m_sfFuncletParent.IsNull())
                                {
                                    // This can only happen if the funclet (and its parent) have been unwound.
                                    _ASSERTE(fFrameWasUnwound);
                                }
                                else
                                {
                                    // We should have found the funclet's parent stackframe
                                    _ASSERTE(!m_sfFuncletParent.IsNull());

                                    bool fIsFilterFunclet = m_crawl.IsFilterFunclet();

                                    STRESS_LOG4(LF_GCROOTS, LL_INFO100,
                                    "STACKWALK: Found %sFilter funclet @ SP: %p, m_crawl.pFunc = %p; FuncletParentCallerSP: %p\n",
                                    (fIsFilterFunclet) ? "" : "Non-", GetRegdisplaySP(m_crawl.GetRegisterSet()), m_crawl.pFunc, m_sfFuncletParent.SP);

                                    if (!fIsFilterFunclet)
                                    {
                                        m_fProcessNonFilterFunclet = true;

                                        // Set the parent frame so that the funclet skipping logic (further below)
                                        // can use it.
                                        m_sfParent = m_sfFuncletParent;

                                        if (!m_fFoundFirstFunclet && (pExInfo > (void*)GetRegdisplaySP(m_crawl.GetRegisterSet())) && ((void*)m_sfParent.SP > pExInfo))
                                        {
                                            // For the first funclet we encounter below the topmost ExInfo that has a parent above that ExInfo
                                            // (so it is an exceptionally called funclet for the exception represented by the ExInfo),
                                            // we instruct the GC scanning of the frame
                                            // to save information on the funclet so that we can use it to report references in the parent frame if
                                            // no such funclet is found in future GC scans for the same exception.
                                            _ASSERTE(pExInfo != NULL);
                                            m_crawl.fShouldSaveFuncletInfo = true;
                                            m_fFoundFirstFunclet = true;
                                        }

                                        if (!fFrameWasUnwound && !ExecutionManager::IsManagedCode(GetIP(m_crawl.GetRegisterSet()->pCallerContext)))
                                        {
                                            // Initiate force reporting of references in the new managed exception handling code frames.
                                            // These frames are still alive when we are in a finally funclet.
                                            m_forceReportingWhileSkipping = ForceGCReportingStage::LookForManagedFrame;
                                            STRESS_LOG0(LF_GCROOTS, LL_INFO100, "STACKWALK: Setting m_forceReportingWhileSkipping = ForceGCReportingStage::LookForManagedFrame\n");
                                        }

                                        // For non-filter funclets, we will make the callback for the funclet
                                        // but skip all the frames until we reach the parent method. When we do,
                                        // we will make a callback for it as well and then continue to make callbacks
                                        // for all upstack frames, until we reach another funclet or the top of the stack
                                        // is reached.
                                        fSkipFuncletCallback = false;
                                    }
                                    else
                                    {
                                        _ASSERTE(fIsFilterFunclet);
                                        m_fProcessNonFilterFunclet = false;

                                        // Nothing more to do as we have come across a filter funclet. In this case, we will:
                                        //
                                        // 1) Get a reference to the parent frame
                                        // 2) Report the funclet
                                        // 3) Continue to report the parent frame, along with a flag that funclet has been reported (see above)
                                        // 4) Continue to report all upstack frames
                                    }
                                }
                            } // end if (m_crawl.IsFunclet())
                        }
                    } // end if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                }
                while(fRecheckCurrentFrame == true);

                if ((m_fProcessNonFilterFunclet == true) || (m_fProcessIntermediaryNonFilterFunclet == true) || (m_flags & (FUNCTIONSONLY | SKIPFUNCLETS)))
                {
                    bool fSkipFrameDueToUnwind = false;

                    if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                    {
                        // When a nested exception escapes, it will unwind past a funclet.  In addition, it will
                        // unwind the frame chain up to the funclet.  When that happens, we'll basically lose
                        // all the stack frames higher than and equal to the funclet.  We can't skip funclets in
                        // the usual way because the first frame we see won't be a funclet.  It will be something
                        // which has conceptually been unwound.  We need to use the information on the
                        // ExInfo to determine if a stack frame is in the unwound stack region.
                        //
                        // If we are enumerating frames for GC reporting and we determined that
                        // the current frame needs to be reported, ensure that it has not already
                        // been unwound by the active exception. If it has been, then we will set a flag
                        // indicating that its references need not be reported. The CrawlFrame, however,
                        // will still be passed to the GC stackwalk callback in case it represents a dynamic
                        // method, to allow the GC to keep that method alive.
                        if (ExInfo::HasFrameBeenUnwoundByAnyActiveException(&m_crawl))
                        {
                            // Invoke the GC callback for this crawlframe (to keep any dynamic methods alive) but do not report its references.
                            m_crawl.fShouldCrawlframeReportGCReferences = false;
                            fSkipFrameDueToUnwind = true;

                            if (m_crawl.IsFunclet() && !fSkippingFunclet)
                            {
                                // we have come across a funclet that has been unwound and we haven't yet started to
                                // look for its parent.  in such a case, the funclet will not have anything to report
                                // so set the corresponding flag to indicate so.

                                _ASSERTE(m_fDidFuncletReportGCReferences);
                                m_fDidFuncletReportGCReferences = false;

                                STRESS_LOG0(LF_GCROOTS, LL_INFO100, "Unwound funclet will skip reporting references\n");
                            }
                        }
                    }
                    else if (m_flags & (FUNCTIONSONLY | SKIPFUNCLETS))
                    {
                        if (ExInfo::IsInStackRegionUnwoundByCurrentException(&m_crawl))
                        {
                            // don't stop here
                            fSkipFrameDueToUnwind = true;
                        }
                    }

                    if (fSkipFrameDueToUnwind)
                    {
                        if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                        {
                            // Check if we are skipping frames.
                            if (!m_sfParent.IsNull())
                            {
                                // Check if our have reached our target method frame.
                                // IsMaxVal() is a special value to indicate that we should skip one frame.
                                if (m_sfParent.IsMaxVal() ||
                                    ExInfo::IsUnwoundToTargetParentFrame(&m_crawl, m_sfParent))
                                {
                                    // Reset flag as we have reached target method frame so no more skipping required
                                    fSkippingFunclet = false;

                                    // We've finished skipping as told.  Now check again.

                                    if ((m_fProcessIntermediaryNonFilterFunclet == true) || (m_fProcessNonFilterFunclet == true))
                                    {
                                        STRESS_LOG2(LF_GCROOTS, LL_INFO100,
                                        "STACKWALK: Reached parent of non-filter funclet @ CallerSP: %p, m_crawl.pFunc = %p\n",
                                        m_sfParent.SP, m_crawl.pFunc);

                                        // landing here indicates that the funclet's parent has been unwound so
                                        // this will always be true, no need to predicate on the state of the funclet
                                        m_crawl.fShouldParentToFuncletSkipReportingGCReferences = true;

                                        // we've reached the parent so reset our state
                                        m_fDidFuncletReportGCReferences = true;

                                        ResetGCRefReportingState(m_fProcessIntermediaryNonFilterFunclet);
                                    }

                                    m_sfParent.Clear();

                                    if (m_crawl.IsFunclet())
                                    {
                                        // We've hit a funclet.
                                        // Since we are in GC reference reporting mode,
                                        // then avoid code duplication and go to
                                        // funclet processing.
                                        fRecheckCurrentFrame = true;
                                        goto ProcessFuncletsForGCReporting;
                                    }
                                }
                            }
                        } // end if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)

                        if (m_crawl.fShouldCrawlframeReportGCReferences)
                        {
                            // Skip the callback for this frame - we don't do this for unwound frames encountered
                            // in GC stackwalk since they may represent dynamic methods whose resolver objects
                            // the GC may need to keep alive.
                            break;
                        }
                    }
                    else
                    {
                        _ASSERTE(!fSkipFrameDueToUnwind);

                        // Check if we are skipping frames.
                        if (!m_sfParent.IsNull())
                        {
                            // Check if we have reached our target method frame.
                            // IsMaxVal() is a special value to indicate that we should skip one frame.
                            if (m_sfParent.IsMaxVal() ||
                                ExInfo::IsUnwoundToTargetParentFrame(&m_crawl, m_sfParent))
                            {
                                // We've finished skipping as told.  Now check again.
                                if ((m_fProcessIntermediaryNonFilterFunclet == true) || (m_fProcessNonFilterFunclet == true))
                                {
                                    // If we are here, we should be in GC reference reporting mode.
                                    _ASSERTE(m_flags & GC_FUNCLET_REFERENCE_REPORTING);

                                    STRESS_LOG2(LF_GCROOTS, LL_INFO100,
                                    "STACKWALK: Reached parent of non-filter funclet @ CallerSP: %p, m_crawl.pFunc = %p\n",
                                    m_sfParent.SP, m_crawl.pFunc);

                                    // by default a funclet's parent won't report its GC roots since they would have already
                                    // been reported by the funclet.  however there is a small window during unwind before
                                    // control returns to the OS where we might require the parent to report.  more below.
                                    bool shouldSkipReporting = true;

                                    if (!m_fDidFuncletReportGCReferences)
                                    {
                                        // we have reached the parent frame of the funclet which didn't report roots since it was already unwound.
                                        // check if the parent frame of the funclet is also handling an exception. if it is, then we will need to
                                        // report roots for it since the catch handler may use references inside it.

                                        STRESS_LOG2(LF_GCROOTS, LL_INFO100,
                                            "STACKWALK: Reached parent of funclet which didn't report GC roots, since funclet is already unwound, pExInfo->m_sfCallerOfActualHandlerFrame=%p, m_sfFuncletParent=%p\n", (void*)pExInfo->m_sfCallerOfActualHandlerFrame.SP, (void*)m_sfFuncletParent.SP);

                                        _ASSERT(pExInfo != NULL);
                                        if (pExInfo && pExInfo->m_sfCallerOfActualHandlerFrame == m_sfFuncletParent)
                                        {
                                            // we should not skip reporting for this parent frame
                                            shouldSkipReporting = false;

                                            // now that we've found the parent that will report roots reset our state.
                                            m_fDidFuncletReportGCReferences = true;

                                            // After funclet gets unwound parent will begin to report gc references. Reporting GC references
                                            // using the IP of throw in parent method can crash application. Parent could have locals objects
                                            // which might not have been reported by funclet as live and would have already been collected
                                            // when funclet was on stack. Now if parent starts using IP of throw to report gc references it
                                            // would report garbage values as live objects. So instead parent can use the IP of the resume
                                            // address of catch funclet to report live GC references.
                                            m_crawl.fShouldParentFrameUseUnwindTargetPCforGCReporting = true;

                                            m_crawl.ehClauseForCatch = pExInfo->m_ClauseForCatch;
                                            STRESS_LOG2(LF_GCROOTS, LL_INFO100,
                                                "STACKWALK: Parent of funclet which didn't report GC roots is handling an exception"
                                                "(EH handler range [%x, %x) ), so we need to specially report roots to ensure variables alive"
                                                " in its handler stay live.\n",
                                                m_crawl.ehClauseForCatch.HandlerStartPC,
                                                m_crawl.ehClauseForCatch.HandlerEndPC);
                                        }
                                        else if (!m_crawl.IsFunclet())
                                        {
                                            if (m_fFuncletNotSeen)
                                            {
                                                // We have reached a real parent of a funclet that would be on the stack if GC didn't
                                                // kick in between the calls to funclets in the second pass. We instruct GC to report
                                                // roots using the info of the saved funclet we've seen during a previous GC.
                                                m_crawl.fShouldParentToFuncletReportSavedFuncletSlots = true;
                                                m_fFuncletNotSeen = false;
                                            }
                                            // we've reached the parent and it's not handling an exception, it's also not
                                            // a funclet so reset our state.  note that we cannot reset the state when the
                                            // parent is a funclet since the leaf funclet didn't report any references and
                                            // we might have a catch handler below us that might contain GC roots.
                                            m_fDidFuncletReportGCReferences = true;
                                            STRESS_LOG0(LF_GCROOTS, LL_INFO100,
                                                "STACKWALK: Reached parent of funclet which didn't report GC roots is not a funclet, resetting m_fDidFuncletReportGCReferences to true\n");
                                        }

                                        _ASSERTE(!ExInfo::HasFrameBeenUnwoundByAnyActiveException(&m_crawl));
                                    }
                                    m_crawl.fShouldParentToFuncletSkipReportingGCReferences = shouldSkipReporting;

                                    ResetGCRefReportingState(m_fProcessIntermediaryNonFilterFunclet);
                                }

                                m_sfParent.Clear();
                            }
                        } // end if (!m_sfParent.IsNull())

                        if (m_sfParent.IsNull() && m_crawl.IsFunclet())
                        {
                            // We've hit a funclet.
                            if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                            {
                                // If we are in GC reference reporting mode,
                                // then avoid code duplication and go to
                                // funclet processing.
                                fRecheckCurrentFrame = true;
                                goto ProcessFuncletsForGCReporting;
                            }
                            else
                            {
                                // Start skipping frames.
                                m_sfParent = ExInfo::FindParentStackFrameForStackWalk(&m_crawl);
                            }

                            // m_sfParent can be NULL if the current funclet is a filter,
                            // in which case we shouldn't skip the frames.
                        }

                        // If we're skipping frames due to a funclet on the stack
                        // or this is an IL stub (which don't get reported when
                        // FUNCTIONSONLY is set) we skip the callback.
                        //
                        // The only exception is the GC reference reporting mode -
                        // for it, we will callback for the funclet so that references
                        // are reported and then continue to skip all frames between the funclet
                        // and its parent, eventually making a callback for the parent as well.
                        if (m_flags & (FUNCTIONSONLY | SKIPFUNCLETS))
                        {
                            if (!m_sfParent.IsNull() || m_crawl.pFunc->IsILStub())
                            {
                                STRESS_LOG4(LF_GCROOTS, LL_INFO100,
                                    "STACKWALK: %s: not making callback for this frame, SPOfParent = %p, \
                                    isILStub = %d, m_crawl.pFunc = %pM\n",
                                    (!m_sfParent.IsNull() ? "SKIPPING_TO_FUNCLET_PARENT" : "IS_IL_STUB"),
                                    m_sfParent.SP,
                                    (m_crawl.pFunc->IsILStub() ? 1 : 0),
                                    m_crawl.pFunc);

                                // don't stop here
                                break;
                            }
                        }
                        else if (fSkipFuncletCallback && (m_flags & GC_FUNCLET_REFERENCE_REPORTING))
                        {
                            if (!m_sfParent.IsNull() && (m_forceReportingWhileSkipping == ForceGCReportingStage::Off))
                            {
                                STRESS_LOG4(LF_GCROOTS, LL_INFO100,
                                     "STACKWALK: %s: not making callback for this frame, SPOfParent = %p, \
                                     isILStub = %d, m_crawl.pFunc = %pM\n",
                                     (!m_sfParent.IsNull() ? "SKIPPING_TO_FUNCLET_PARENT" : "IS_IL_STUB"),
                                     m_sfParent.SP,
                                     (m_crawl.pFunc->IsILStub() ? 1 : 0),
                                     m_crawl.pFunc);

                                // don't stop here
                                break;
                            }

                            if (m_forceReportingWhileSkipping == ForceGCReportingStage::LookForManagedFrame)
                            {
                                // State indicating that the next marker frame should turn off the reporting again. That would be the caller of the managed RhThrowEx
                                m_forceReportingWhileSkipping = ForceGCReportingStage::LookForMarkerFrame;
                                STRESS_LOG0(LF_GCROOTS, LL_INFO100, "STACKWALK: Setting m_forceReportingWhileSkipping = ForceGCReportingStage::LookForMarkerFrame\n");
                            }

#ifdef _DEBUG
                            if (m_forceReportingWhileSkipping != ForceGCReportingStage::Off)
                            {
                                STRESS_LOG3(LF_GCROOTS, LL_INFO100,
                                    "STACKWALK: Force callback for skipped function m_crawl.pFunc = %pM (%s.%s)\n", m_crawl.pFunc, m_crawl.pFunc->m_pszDebugClassName, m_crawl.pFunc->m_pszDebugMethodName);
                                _ASSERTE((m_crawl.pFunc->GetMethodTable() == g_pEHClass) || (strcmp(m_crawl.pFunc->m_pszDebugClassName, "ILStubClass") == 0) || (strcmp(m_crawl.pFunc->m_pszDebugMethodName, "CallFinallyFunclet") == 0) || (m_crawl.pFunc->GetMethodTable() == g_pExceptionServicesInternalCallsClass));
                            }
#endif
                        }
                    }
                }
                else if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                {
                    // If we are enumerating frames for GC reporting and we determined that
                    // the current frame needs to be reported, ensure that it has not already
                    // been unwound by the active exception. If it has been, then we will
                    // simply skip it and not deliver a callback for it.
                    if (ExInfo::HasFrameBeenUnwoundByAnyActiveException(&m_crawl))
                    {
                        // Invoke the GC callback for this crawlframe (to keep any dynamic methods alive) but do not report its references.
                        m_crawl.fShouldCrawlframeReportGCReferences = false;
                    }
                }

#else // FEATURE_EH_FUNCLETS
                // Skip IL stubs
                if (m_flags & FUNCTIONSONLY)
                {
                    if (m_crawl.pFunc->IsILStub())
                    {
                        LOG((LF_GCROOTS, LL_INFO100000,
                             "STACKWALK: IS_IL_STUB: not making callback for this frame, m_crawl.pFunc = %s\n",
                             m_crawl.pFunc->m_pszDebugMethodName));

                        // don't stop here
                        break;
                    }
                }
#endif // FEATURE_EH_FUNCLETS

                fStop = true;
                break;

            case SFITER_FRAME_FUNCTION:
                //
                // fall through
                //

            case SFITER_SKIPPED_FRAME_FUNCTION:
                if (!fSkippingFunclet)
                {
#if defined(FEATURE_EH_FUNCLETS)
                    if (m_flags & GC_FUNCLET_REFERENCE_REPORTING)
                    {
                        // If we are enumerating frames for GC reporting and we determined that
                        // the current frame needs to be reported, ensure that it has not already
                        // been unwound by the active exception. If it has been, then we will
                        // simply skip it and not deliver a callback for it.
                        if (ExInfo::HasFrameBeenUnwoundByAnyActiveException(&m_crawl))
                        {
                            // Invoke the GC callback for this crawlframe (to keep any dynamic methods alive) but do not report its references.
                            m_crawl.fShouldCrawlframeReportGCReferences = false;
                        }
                    }
                    else if (m_flags & (FUNCTIONSONLY | SKIPFUNCLETS))
                    {
                        // See the comment above for IsInStackRegionUnwoundByCurrentException().
                        if (ExInfo::IsInStackRegionUnwoundByCurrentException(&m_crawl))
                        {
                            // don't stop here
                            break;
                        }
                    }
#endif // FEATURE_EH_FUNCLETS
                    if ( (m_crawl.pFunc != NULL) || !(m_flags & FUNCTIONSONLY) )
                    {
                        fStop = true;
                    }
                }
                break;

            case SFITER_NO_FRAME_TRANSITION:
                if (!fSkippingFunclet)
                {
                    if (m_flags & NOTIFY_ON_NO_FRAME_TRANSITIONS)
                    {
                        _ASSERTE(m_crawl.isNoFrameTransition == true);
                        fStop = true;
                    }
                }
                break;

            case SFITER_NATIVE_MARKER_FRAME:
                if (!fSkippingFunclet)
                {
                    if (m_flags & NOTIFY_ON_U2M_TRANSITIONS)
                    {
                        _ASSERTE(m_crawl.isNativeMarker == true);
                        fStop = true;
                    }
                }
                if (m_forceReportingWhileSkipping == ForceGCReportingStage::LookForMarkerFrame)
                {
                    m_forceReportingWhileSkipping = ForceGCReportingStage::Off;
                    STRESS_LOG0(LF_GCROOTS, LL_INFO100, "STACKWALK: Setting m_forceReportingWhileSkipping = ForceGCReportingStage::Off\n");
                }
                break;

            case SFITER_INITIAL_NATIVE_CONTEXT:
                if (!fSkippingFunclet)
                {
                    if (m_flags & NOTIFY_ON_INITIAL_NATIVE_CONTEXT)
                    {
                        fStop = true;
                    }
                }
                break;

            default:
                UNREACHABLE();
        }

        if (fStop)
        {
            break;
        }
        else
        {
            INDEBUG(m_crawl.pThread->DebugLogStackWalkInfo(&m_crawl, "FILTER  ", m_uFramesProcessed));
            retVal = NextRaw();
            if (retVal != SWA_CONTINUE)
            {
                break;
            }
        }
    }

    return retVal;
}

//---------------------------------------------------------------------------------------
//
// Advance to the next frame and stop, regardless of the stackwalk flags.
//
// Return Value:
//    SWA_CONTINUE (== SWA_DONE) if the iterator is successful in advancing to the next frame
//    SWA_FAILED if an operation performed by the iterator fails
//
// Assumptions:
//    The caller has checked that the iterator is valid.
//
// Notes:
//    This function returns SWA_DONE when advancing from the last frame to becoming invalid.
//

StackWalkAction StackFrameIterator::NextRaw(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    _ASSERTE(IsValid());

    INDEBUG(m_uFramesProcessed++);

    StackWalkAction retVal = SWA_CONTINUE;

    if (m_frameState == SFITER_SKIPPED_FRAME_FUNCTION)
    {
#if !defined(TARGET_X86) && defined(_DEBUG)
        // make sure we're not skipping a different transition
        if (m_crawl.pFrame->NeedsUpdateRegDisplay())
        {
            if (m_crawl.pFrame->GetFrameIdentifier() == FrameIdentifier::InlinedCallFrame)
            {
                // ControlPC may be different as the InlinedCallFrame stays active throughout
                // the STOP_FOR_GC callout but we can use the stack/frame pointer for the assert.
                PTR_InlinedCallFrame pICF = dac_cast<PTR_InlinedCallFrame>(m_crawl.pFrame);
                CONSISTENCY_CHECK((GetRegdisplaySP(m_crawl.pRD) == (TADDR)pICF->GetCallSiteSP())
                || (GetFP(m_crawl.pRD->pCurrentContext) == pICF->GetCalleeSavedFP()));
            }
            else
            {
                CONSISTENCY_CHECK(GetControlPC(m_crawl.pRD) == m_crawl.pFrame->GetReturnAddress());
            }
        }
#endif // !defined(TARGET_X86) && defined(_DEBUG)

#if defined(STACKWALKER_MAY_POP_FRAMES)
        if (m_flags & POPFRAMES)
        {
            _ASSERTE(m_crawl.pFrame == m_crawl.pThread->GetFrame());

            // If we got here, the current frame chose not to handle the
            // exception. Give it a chance to do any termination work
            // before we pop it off.

            CLEAR_THREAD_TYPE_STACKWALKER();
            END_FORBID_TYPELOAD();

            m_crawl.pFrame->ExceptionUnwind();

            BEGIN_FORBID_TYPELOAD();
            SET_THREAD_TYPE_STACKWALKER(m_pThread);

            // Pop off this frame and go on to the next one.
            m_crawl.GotoNextFrame();

            // When StackWalkFramesEx is originally called, we ensure
            // that if POPFRAMES is set that the thread is in COOP mode
            // and that running thread is walking itself. Thus, this
            // COOP assertion is safe.
            BEGIN_GCX_ASSERT_COOP;
            m_crawl.pThread->SetFrame(m_crawl.pFrame);
            END_GCX_ASSERT_COOP;
        }
        else
#endif // STACKWALKER_MAY_POP_FRAMES
        {
            // go to the next frame
            m_crawl.GotoNextFrame();
        }

        // check for skipped frames again
        if (CheckForSkippedFrames())
        {
            // there are more skipped explicit frames
            _ASSERTE(m_frameState == SFITER_SKIPPED_FRAME_FUNCTION);
            goto Cleanup;
        }
        else
        {
#ifndef PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
            // On x86, we process a managed stack frame before processing any explicit frames contained in it.
            // So when we are done with the skipped explicit frame, we have already processed the managed
            // stack frame, and it is time to move onto the next stack frame.
            PostProcessingForManagedFrames();
            if (m_frameState == SFITER_NATIVE_MARKER_FRAME)
            {
                goto Cleanup;
            }
#else // !PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
            // We are done handling the skipped explicit frame at this point.  So move on to the
            // managed stack frame.
            m_crawl.isFrameless = true;
            m_crawl.codeInfo    = m_cachedCodeInfo;
            m_crawl.pFunc       = m_crawl.codeInfo.GetMethodDesc();


            PreProcessingForManagedFrames();
            goto Cleanup;
#endif // PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
        }
    }
    else if (m_frameState == SFITER_FRAMELESS_METHOD)
    {
        // Now find out if we need to leave monitors

#ifdef TARGET_X86
        //
        // For non-x86 platforms, the JIT generates try/finally to leave monitors; for x86, the VM handles the monitor
        //
#if defined(STACKWALKER_MAY_POP_FRAMES)
        if (m_flags & POPFRAMES)
        {
            BEGIN_GCX_ASSERT_COOP;

            if (m_crawl.pFunc->IsSynchronized())
            {
                MethodDesc *   pMD = m_crawl.pFunc;
                OBJECTREF      orUnwind = NULL;

                if (m_crawl.GetCodeManager()->IsInSynchronizedRegion(m_crawl.GetRelOffset(),
                                                                    m_crawl.GetGCInfoToken(),
                                                                    m_crawl.GetCodeManagerFlags()))
                {
                    if (pMD->IsStatic())
                    {
                        MethodTable * pMT = pMD->GetMethodTable();
                        orUnwind = pMT->GetManagedClassObjectIfExists();

                        _ASSERTE(orUnwind != NULL);
                    }
                    else
                    {
                        orUnwind = m_crawl.GetCodeManager()->GetInstance(
                                                m_crawl.pRD,
                                                m_crawl.GetCodeInfo());
                    }

                    _ASSERTE(orUnwind != NULL);
                    VALIDATEOBJECTREF(orUnwind);

                    if (orUnwind != NULL)
                    {
                        orUnwind->LeaveObjMonitorAtException();
                    }
                }
            }

            END_GCX_ASSERT_COOP;
        }
#endif // STACKWALKER_MAY_POP_FRAMES
#endif // TARGET_X86

#if !defined(ELIMINATE_FEF)
        // FaultingExceptionFrame is special case where it gets
        // pushed on the stack after the frame is running
        _ASSERTE((m_crawl.pFrame == FRAME_TOP) ||
                 ((TADDR)GetRegdisplaySP(m_crawl.pRD) < dac_cast<TADDR>(m_crawl.pFrame)) ||
                 (m_crawl.pFrame->GetFrameIdentifier() == FrameIdentifier::FaultingExceptionFrame));
#endif // !defined(ELIMINATE_FEF)

        // Get rid of the frame (actually, it isn't really popped)

        LOG((LF_GCROOTS, LL_EVERYTHING, "STACKWALK: [%03x] about to unwind for '%s', SP:" FMT_ADDR ", IP:" FMT_ADDR "\n",
             m_uFramesProcessed,
             m_crawl.pFunc->m_pszDebugMethodName,
             DBG_ADDR(GetRegdisplaySP(m_crawl.pRD)),
             DBG_ADDR(GetControlPC(m_crawl.pRD))));

        if (!m_crawl.GetCodeManager()->UnwindStackFrame(
                            m_crawl.pRD,
                            &m_cachedCodeInfo,
                            m_codeManFlags
                                | m_crawl.GetCodeManagerFlags()
                                | ((m_flags & PROFILER_DO_STACK_SNAPSHOT) ?  SpeculativeStackwalk : 0)))
        {
            LOG((LF_CORPROF, LL_INFO100, "**PROF: m_crawl.GetCodeManager()->UnwindStackFrame failure leads to SWA_FAILED.\n"));
            retVal = SWA_FAILED;
            goto Cleanup;
        }

#define FAIL_IF_SPECULATIVE_WALK(condition)             \
        if (m_flags & PROFILER_DO_STACK_SNAPSHOT)       \
        {                                               \
            if (!(condition))                           \
            {                                           \
                LOG((LF_CORPROF, LL_INFO100, "**PROF: " #condition " failure leads to SWA_FAILED.\n")); \
                retVal = SWA_FAILED;                    \
                goto Cleanup;                           \
            }                                           \
        }                                               \
        else                                            \
        {                                               \
            _ASSERTE(condition);                        \
        }

        // When the stackwalk is seeded with a profiler context, the context
        // might be bogus.  Check the stack pointer and the program counter for validity here.
        // (Note that these checks are not strictly necessary since we are able
        // to recover from AVs during profiler stackwalk.)

        PTR_VOID newSP = PTR_VOID((TADDR)GetRegdisplaySP(m_crawl.pRD));
#ifndef NO_FIXED_STACK_LIMIT
        FAIL_IF_SPECULATIVE_WALK(m_crawl.pThread->IsExecutingOnAltStack() || newSP >= m_crawl.pThread->GetCachedStackLimit());
#endif // !NO_FIXED_STACK_LIMIT
        FAIL_IF_SPECULATIVE_WALK(m_crawl.pThread->IsExecutingOnAltStack() || newSP < m_crawl.pThread->GetCachedStackBase());

#undef FAIL_IF_SPECULATIVE_WALK

        LOG((LF_GCROOTS, LL_EVERYTHING, "STACKWALK: [%03x] finished unwind for '%s', SP:" FMT_ADDR \
             ", IP:" FMT_ADDR "\n",
             m_uFramesProcessed,
             m_crawl.pFunc->m_pszDebugMethodName,
             DBG_ADDR(GetRegdisplaySP(m_crawl.pRD)),
             DBG_ADDR(GetControlPC(m_crawl.pRD))));

        m_crawl.isFirst       = false;
        m_crawl.isInterrupted = false;
        m_crawl.hasFaulted    = false;
        m_crawl.isIPadjusted  = false;

#ifndef PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
        // remember, x86 handles the managed stack frame before the explicit frames contained in it
        if (CheckForSkippedFrames())
        {
            _ASSERTE(m_frameState == SFITER_SKIPPED_FRAME_FUNCTION);
            goto Cleanup;
        }
#endif // !PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME

        PostProcessingForManagedFrames();
        if (m_frameState == SFITER_NATIVE_MARKER_FRAME)
        {
            goto Cleanup;
        }
    }
    else if (m_frameState == SFITER_FRAME_FUNCTION)
    {
        Frame* pInlinedFrame = NULL;

        if (InlinedCallFrame::FrameHasActiveCall(m_crawl.pFrame))
        {
            pInlinedFrame = m_crawl.pFrame;
        }

        unsigned uFrameAttribs = m_crawl.pFrame->GetFrameAttribs();

        // Special resumable frames make believe they are on top of the stack.
        m_crawl.isFirst = (uFrameAttribs & Frame::FRAME_ATTR_RESUMABLE) != 0;

        // If the frame is a subclass of ExceptionFrame,
        // then we know this is interrupted.
        m_crawl.isInterrupted = (uFrameAttribs & Frame::FRAME_ATTR_EXCEPTION) != 0;

        if (m_crawl.isInterrupted)
        {
            m_crawl.hasFaulted = (uFrameAttribs & Frame::FRAME_ATTR_FAULTED) != 0;
            m_crawl.isIPadjusted = false;
        }

        PCODE adr = m_crawl.pFrame->GetReturnAddress();
        _ASSERTE(adr != (PCODE)POISONC);

        _ASSERTE(!pInlinedFrame || adr);

        if (adr)
        {
            ProcessIp(adr);

            _ASSERTE(m_crawl.GetCodeInfo()->IsValid() || !pInlinedFrame);

            if (m_crawl.isFrameless)
            {
                m_crawl.pFrame->UpdateRegDisplay(m_crawl.pRD, m_flags & UNWIND_FLOATS);

#if defined(RECORD_RESUMABLE_FRAME_SP)
                CONSISTENCY_CHECK(NULL == m_pvResumableFrameTargetSP);

                if (m_crawl.isFirst)
                {
                    if (m_flags & THREAD_IS_SUSPENDED)
                    {
                        _ASSERTE(m_crawl.isProfilerDoStackSnapshot);

                        // abort the stackwalk, we can't proceed without risking deadlock
                        retVal = SWA_FAILED;
                        goto Cleanup;
                    }

                    // we are about to unwind, which may take a lock, so the thread
                    // better not be suspended.
                    CONSISTENCY_CHECK(!(m_flags & THREAD_IS_SUSPENDED));

                    m_crawl.GetCodeManager()->EnsureCallerContextIsValid(m_crawl.pRD, NULL, m_codeManFlags);
                    m_pvResumableFrameTargetSP = (LPVOID)GetSP(m_crawl.pRD->pCallerContext);
                }
#endif // RECORD_RESUMABLE_FRAME_SP


#if defined(_DEBUG) && !defined(DACCESS_COMPILE) && !defined(FEATURE_EH_FUNCLETS)
                // We are transitioning from unmanaged code to managed code... lets do some validation of our
                // EH mechanism on platforms that we can.
                VerifyValidTransitionFromManagedCode(m_crawl.pThread, &m_crawl);
#endif // _DEBUG && !DACCESS_COMPILE &&  !FEATURE_EH_FUNCLETS
            }
        }

        if (!pInlinedFrame)
        {
#if defined(STACKWALKER_MAY_POP_FRAMES)
            if (m_flags & POPFRAMES)
            {
                // If we got here, the current frame chose not to handle the
                // exception. Give it a chance to do any termination work
                // before we pop it off.

                CLEAR_THREAD_TYPE_STACKWALKER();
                END_FORBID_TYPELOAD();

                m_crawl.pFrame->ExceptionUnwind();

                BEGIN_FORBID_TYPELOAD();
                SET_THREAD_TYPE_STACKWALKER(m_pThread);

                // Pop off this frame and go on to the next one.
                m_crawl.GotoNextFrame();

                // When StackWalkFramesEx is originally called, we ensure
                // that if POPFRAMES is set that the thread is in COOP mode
                // and that running thread is walking itself. Thus, this
                // COOP assertion is safe.
                BEGIN_GCX_ASSERT_COOP;
                m_crawl.pThread->SetFrame(m_crawl.pFrame);
                END_GCX_ASSERT_COOP;
            }
            else
#endif // STACKWALKER_MAY_POP_FRAMES
            {
                // Go to the next frame.
                m_crawl.GotoNextFrame();
            }
        }
    }
#if defined(ELIMINATE_FEF)
    else if (m_frameState == SFITER_NO_FRAME_TRANSITION)
    {
        PostProcessingForNoFrameTransition();
    }
#endif  // ELIMINATE_FEF
    else if (m_frameState == SFITER_NATIVE_MARKER_FRAME)
    {
        m_crawl.isNativeMarker = false;
    }
    else if (m_frameState == SFITER_INITIAL_NATIVE_CONTEXT)
    {
        // nothing to do here
    }
    else
    {
        _ASSERTE(m_frameState == SFITER_UNINITIALIZED);
        _ASSERTE(!"StackFrameIterator::NextRaw() called when the iterator is uninitialized.  \
                  Should never get here.");
        retVal = SWA_FAILED;
        goto Cleanup;
    }

    ProcessCurrentFrame();

Cleanup:
#if defined(_DEBUG)
    if (retVal == SWA_FAILED)
    {
        LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: SWA_FAILED: couldn't start stackwalk\n"));
    }
#endif // _DEBUG

    return retVal;
} // StackFrameIterator::NextRaw()

//---------------------------------------------------------------------------------------
//
// Synchronizing the REGDISPLAY to the current CONTEXT stored in the REGDISPLAY.
// This is an nop on non-WIN64 platforms.
//

void StackFrameIterator::UpdateRegDisp(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    BIT64_ONLY(SyncRegDisplayToCurrentContext(m_crawl.pRD));
} // StackFrameIterator::UpdateRegDisp()

//---------------------------------------------------------------------------------------
//
// Check whether the specified Ip is in managed code and update the CrawlFrame accordingly.
// This function updates isFrameless, JitManagerInstance.
//
// Arguments:
//    Ip - IP to be processed
//

void StackFrameIterator::ProcessIp(PCODE Ip)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        SUPPORTS_DAC;
    } CONTRACTL_END;

    // Re-initialize codeInfo with new IP
    m_crawl.codeInfo.Init(Ip, m_scanFlag);

    m_crawl.isFrameless = !!m_crawl.codeInfo.IsValid();

#ifdef TARGET_X86
    if (m_crawl.isFrameless)
    {
        // Optimization: Ensure that we decode GC info header early. We will reuse
        // it several times.
        hdrInfo *hdrInfoBody;
        m_crawl.codeInfo.DecodeGCHdrInfo(&hdrInfoBody);
    }
#endif
} // StackFrameIterator::ProcessIp()

//---------------------------------------------------------------------------------------
//
// Update the CrawlFrame to represent where we have stopped.  This is called after advancing
// to a new frame.
//
// Notes:
//    This function and everything it calls must not rely on m_frameState, which could have become invalid
//    when we advance the iterator before calling this function.
//

void StackFrameIterator::ProcessCurrentFrame(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    bool fDone = false;

    m_crawl.CheckGSCookies();

    // Since we have advanced the iterator, the frame state represents the previous frame state,
    // not the current one.  This is important to keep in mind.  Ideally we should just assert that
    // the frame state has been set to invalid upon entry to this function, but we need the previous frame
    // state to decide if we should stop at an native stack frame.

    // If we just do a simple check for native code here, we will loop forever.
    if (m_frameState == SFITER_UNINITIALIZED)
    {
        // "!IsFrameless()" normally implies that the CrawlFrame is at an explicit frame.  Here we are using it
        // to detect whether the CONTEXT is in managed code or not.  Ideally we should have a enum on the
        // CrawlFrame to indicate the various types of "frames" the CrawlFrame can stop at.
        //
        // If the CONTEXT is in native code and the StackFrameIterator is uninitialized, then it must be
        // an initial native CONTEXT passed to the StackFrameIterator when it is created or
        // when ResetRegDisp() is called.
        if (!m_crawl.IsFrameless())
        {
            m_frameState = SFITER_INITIAL_NATIVE_CONTEXT;
            fDone = true;
        }
    }
    else
    {
        // Clear the frame state.  It will be set before we return from this function.
        m_frameState = SFITER_UNINITIALIZED;
    }

    // Check for the case of an exception in managed code, and resync the stack walk
    //  from the exception context.
#if defined(ELIMINATE_FEF)
    if (!fDone && !m_crawl.IsFrameless() && m_exInfoWalk.GetExInfo())
    {
        // We are currently walking ("lost") in unmanaged code.  We can recover
        //  from a) the next Frame record, or b) an exception context.
        // Recover from the exception context if all of these are true:
        //  - it "returns" to managed code
        //  - if is lower (newer) than the next Frame record
        //  - the stack walk has not already passed by it
        //
        // The ExInfo walker is initialized to be higher than the pStartFrame, and
        //  as we unwind managed (frameless) functions, we keep eliminating any
        //  ExInfos that are passed in the stackwalk.
        //
        // So, here we need to find the next ExInfo that "returns" to managed code,
        //  and then choose the lower of that ExInfo and the next Frame.
        m_exInfoWalk.WalkToManaged();
        TADDR pContextSP = m_exInfoWalk.GetSPFromContext();

        //@todo: check the exception code for a fault?

        // If there was a pContext that is higher than the SP and starting frame...
        if (pContextSP)
        {
            PTR_CONTEXT pContext = m_exInfoWalk.GetContext();

            LOG((LF_EH, LL_INFO10000, "STACKWALK: considering resync from pContext(%p), fault(%08X), sp(%p); \
                 pStartFrame(%p); cf.pFrame(%p), cf.SP(%p)\n",
                 pContext, m_exInfoWalk.GetFault(), pContextSP,
                 m_pStartFrame, dac_cast<TADDR>(m_crawl.pFrame), GetRegdisplaySP(m_crawl.pRD)));

            // If the pContext is lower (newer) than the CrawlFrame's Frame*, try to use
            //  the pContext.
            // There are still a few cases in which a FaultingExceptionFrame is linked in.  If
            //  the next frame is one of them, we don't want to override it.  THIS IS PROBABLY BAD!!!
            if ( (pContextSP < dac_cast<TADDR>(m_crawl.pFrame)) &&
                 ((m_crawl.GetFrame() == FRAME_TOP) ||
                  (m_crawl.GetFrame()->GetFrameIdentifier() != FrameIdentifier::FaultingExceptionFrame ) ) )
            {
                //
                // If the REGDISPLAY represents an unmanaged stack frame above (closer to the leaf than) an
                // ExInfo without any intervening managed stack frame, then we will stop at the no-frame
                // transition protected by the ExInfo.  However, if the unmanaged stack frame is the one
                // immediately above the faulting managed stack frame, we want to continue the stackwalk
                // with the faulting managed stack frame.  So we do not stop in this case.
                //
                // However, just comparing EBP is not enough.  The OS exception handler
                // (KiUserExceptionDispatcher()) does not use an EBP frame.  So if we just compare the EBP
                // we will think that the OS exception handler is the one we want to claim.  Instead,
                // we should also check the current IP, which because of the way unwinding work and
                // how the OS exception handler behaves is actually going to be the stack limit of the
                // current thread.  This is of course a workaround and is dependent on the OS behaviour.
                //

                PCODE curPC = GetControlPC(m_crawl.pRD);
                if ((m_crawl.pRD->pEbp != NULL )                                               &&
                    (m_exInfoWalk.GetEBPFromContext() == GetRegdisplayFP(m_crawl.pRD)) &&
                    ((m_crawl.pThread->GetCachedStackLimit() <= PTR_VOID(curPC)) &&
                       (PTR_VOID(curPC) < m_crawl.pThread->GetCachedStackBase())))
                {
                    // restore the CONTEXT saved by the ExInfo and continue on to the faulting
                    // managed stack frame
                    PostProcessingForNoFrameTransition();
                }
                else
                {
                    // we stop stop at the no-frame transition
                    m_frameState = SFITER_NO_FRAME_TRANSITION;
                    m_crawl.isNoFrameTransition = true;
                    m_crawl.taNoFrameTransitionMarker = pContextSP;
                    fDone = true;
                }
            }
        }
    }
#endif // defined(ELIMINATE_FEF)

    if (!fDone)
    {
        // returns SWA_DONE if there is no more frames to walk
        if (!IsValid())
        {
            LOG((LF_GCROOTS, LL_INFO10000, "STACKWALK: SWA_DONE: reached the end of the stack\n"));
            m_frameState = SFITER_DONE;
            return;
        }

#ifdef FEATURE_INTERPRETER
        if (!m_crawl.isFrameless)
        {
            PREGDISPLAY pRD = m_crawl.GetRegisterSet();

            if (m_crawl.pFrame->GetFrameIdentifier() == FrameIdentifier::InterpreterFrame)
            {
                if (GetIP(pRD->pCurrentContext) != (PCODE)InterpreterFrame::DummyCallerIP)
                {
                    // We have hit the InterpreterFrame while we were not processing the interpreter frames.
                    // Switch to walking the underlying interpreted frames.
                    // Save the registers the interpreter frames walking reuses so that we can restore them
                    // after we are done with the interpreter frames.
                    m_interpExecMethodIP = GetIP(pRD->pCurrentContext);
                    m_interpExecMethodSP = GetSP(pRD->pCurrentContext);
                    m_interpExecMethodFP = GetFP(pRD->pCurrentContext);
                    m_interpExecMethodFirstArgReg = GetFirstArgReg(pRD->pCurrentContext);

                    ((PTR_InterpreterFrame)m_crawl.pFrame)->SetContextToInterpMethodContextFrame(pRD->pCurrentContext);
                    if (pRD->pCurrentContext->ContextFlags & CONTEXT_EXCEPTION_ACTIVE)
                    {
                        m_crawl.isInterrupted = true;
                        m_crawl.hasFaulted = true;
                    }

                    SyncRegDisplayToCurrentContext(pRD);
                    ProcessIp(GetControlPC(pRD));
                }
                else
                {
                    // We have finished walking the interpreted frames. Process the InterpreterFrame itself.
                    // Restore the registers to the values they had before we started walking the interpreter frames.
                    SetIP(pRD->pCurrentContext, m_interpExecMethodIP);
                    SetSP(pRD->pCurrentContext, m_interpExecMethodSP);
                    SetFP(pRD->pCurrentContext, m_interpExecMethodFP);
                    SetFirstArgReg(pRD->pCurrentContext, m_interpExecMethodFirstArgReg);
                    SyncRegDisplayToCurrentContext(pRD);
                }
            }
            else if (InlinedCallFrame::FrameHasActiveCall(m_crawl.pFrame) &&
                     (m_crawl.pFrame->PtrNextFrame() != FRAME_TOP) &&
                     (m_crawl.pFrame->PtrNextFrame()->GetFrameIdentifier() == FrameIdentifier::InterpreterFrame))
            {
                // There is an active inlined call frame and the next frame is the interpreter frame. This is a special case where we need to save the current context registers that the interpreter frames walking reuses.
                m_interpExecMethodIP = GetIP(pRD->pCurrentContext);
                m_interpExecMethodSP = GetSP(pRD->pCurrentContext);
                m_interpExecMethodFP = GetFP(pRD->pCurrentContext);
                m_interpExecMethodFirstArgReg = GetFirstArgReg(pRD->pCurrentContext);
            }
        }
#endif // FEATURE_INTERPRETER

        if (m_crawl.isFrameless)
        {
            //------------------------------------------------------------------------
            // This must be a JITed/managed native method. There is no explicit frame.
            //------------------------------------------------------------------------

#if defined(FEATURE_EH_FUNCLETS)
            m_crawl.isFilterFuncletCached = false;
#endif // FEATURE_EH_FUNCLETS

            m_crawl.pFunc = m_crawl.codeInfo.GetMethodDesc();

            // Cache values which may be updated by CheckForSkippedFrames()
            m_cachedCodeInfo = m_crawl.codeInfo;

#ifdef PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
            // On non-X86, we want to process the skipped explicit frames before the managed stack frame
            // containing them.
            if (CheckForSkippedFrames())
            {
                _ASSERTE(m_frameState == SFITER_SKIPPED_FRAME_FUNCTION);
            }
            else
#endif // PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
            {
                PreProcessingForManagedFrames();
                _ASSERTE(m_frameState == SFITER_FRAMELESS_METHOD);
            }
        }
        else
        {
            INDEBUG(m_crawl.pThread->DebugLogStackWalkInfo(&m_crawl, "CONSIDER", m_uFramesProcessed));

            _ASSERTE(m_crawl.pFrame != FRAME_TOP);

            m_crawl.pFunc = m_crawl.pFrame->GetFunction();

            m_frameState = SFITER_FRAME_FUNCTION;
        }
    }

    _ASSERTE(m_frameState != SFITER_UNINITIALIZED);
} // StackFrameIterator::ProcessCurrentFrame()

//---------------------------------------------------------------------------------------
//
// If an explicit frame is allocated in a managed stack frame (e.g. an inlined pinvoke call),
// we may have skipped an explicit frame.  This function checks for them.
//
// Return Value:
//    Returns true if there are skipped frames.
//
// Notes:
//    x86 wants to stop at the skipped stack frames after the containing managed stack frame, but
//    WIN64 wants to stop before.  I don't think x86 actually has any good reason for this, except
//    because it doesn't unwind one frame ahead of time like WIN64 does.  This means that we don't
//    have the caller SP on x86.
//

BOOL StackFrameIterator::CheckForSkippedFrames(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

    BOOL   fHandleSkippedFrames = FALSE;
    TADDR pvReferenceSP;

    // Can the caller handle skipped frames;
    fHandleSkippedFrames = (m_flags & HANDLESKIPPEDFRAMES);

#ifndef PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
    pvReferenceSP = GetRegdisplaySP(m_crawl.pRD);
#else // !PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME
    // Order the Frames relative to the caller SP of the methods
    // this makes it so that any Frame that is in a managed call
    // frame will be reported before its containing method.

    // This should always succeed!  If it doesn't, it's a bug somewhere else!
    ICodeManager *pCodeManager = (m_crawl.isFrameless) ? m_crawl.GetCodeManager() : ExecutionManager::GetDefaultCodeManager();
    pCodeManager->EnsureCallerContextIsValid(m_crawl.pRD, &m_cachedCodeInfo, m_codeManFlags);
    pvReferenceSP = GetSP(m_crawl.pRD->pCallerContext);
#endif // PROCESS_EXPLICIT_FRAME_BEFORE_MANAGED_FRAME

    if ( !( (m_crawl.pFrame != FRAME_TOP) &&
            (dac_cast<TADDR>(m_crawl.pFrame) < pvReferenceSP) )
       )
    {
        return FALSE;
    }

    LOG((LF_GCROOTS, LL_EVERYTHING, "STACKWALK: CheckForSkippedFrames\n"));

    // We might have skipped past some Frames.
    // This happens with InlinedCallFrames.
    while ( (m_crawl.pFrame != FRAME_TOP) &&
            (dac_cast<TADDR>(m_crawl.pFrame) < pvReferenceSP)
          )
    {
        BOOL fReportInteropMD =
        // If we see InlinedCallFrame in certain IL stubs, we should report the MD that
        // was passed to the stub as its secret argument. This is the true interop MD.
        // Note that code:InlinedCallFrame.GetFunction may return NULL in this case because
        // the call is made using the CALLI instruction.
            m_crawl.pFrame != FRAME_TOP &&
            m_crawl.pFrame->GetFrameIdentifier() == FrameIdentifier::InlinedCallFrame &&
            m_crawl.pFunc != NULL &&
            m_crawl.pFunc->IsILStub() &&
            m_crawl.pFunc->AsDynamicMethodDesc()->HasMDContextArg();

        if (fHandleSkippedFrames)
        {
            m_crawl.GotoNextFrame();
#ifdef STACKWALKER_MAY_POP_FRAMES
            if (m_flags & POPFRAMES)
            {
                // When StackWalkFramesEx is originally called, we ensure
                // that if POPFRAMES is set that the thread is in COOP mode
                // and that running thread is walking itself. Thus, this
                // COOP assertion is safe.
                BEGIN_GCX_ASSERT_COOP;
                m_crawl.pThread->SetFrame(m_crawl.pFrame);
                END_GCX_ASSERT_COOP;
            }
#endif // STACKWALKER_MAY_POP_FRAMES
        }
        else
        {
            m_crawl.isFrameless     = false;

            if (fReportInteropMD)
            {
                m_crawl.pFunc = ((PTR_InlinedCallFrame)m_crawl.pFrame)->GetActualInteropMethodDesc();
                _ASSERTE(m_crawl.pFunc != NULL);
                _ASSERTE(m_crawl.pFunc->SanityCheck());
            }
            else
            {
                m_crawl.pFunc = m_crawl.pFrame->GetFunction();
            }

            INDEBUG(m_crawl.pThread->DebugLogStackWalkInfo(&m_crawl, "CONSIDER", m_uFramesProcessed));

            m_frameState = SFITER_SKIPPED_FRAME_FUNCTION;
            return TRUE;
        }
    }

    return FALSE;
} // StackFrameIterator::CheckForSkippedFrames()

//---------------------------------------------------------------------------------------
//
// Perform the necessary tasks before stopping at a managed stack frame.  This is mostly validation work.
//

void StackFrameIterator::PreProcessingForManagedFrames(void)
{
    WRAPPER_NO_CONTRACT;
    SUPPORTS_DAC;

#if defined(RECORD_RESUMABLE_FRAME_SP)
    if (m_pvResumableFrameTargetSP)
    {
        // We expect that if we saw a resumable frame, the next managed
        // IP that we see will be the one the resumable frame took us to.

        // However, because we might visit intervening explicit Frames
        // that will clear the .isFirst flag, we need to set it back here.

        CONSISTENCY_CHECK(m_crawl.pRD->IsCallerContextValid);
        CONSISTENCY_CHECK((LPVOID)GetSP(m_crawl.pRD->pCallerContext) == m_pvResumableFrameTargetSP);
        m_pvResumableFrameTargetSP = NULL;
        m_crawl.isFirst = true;
    }
#endif // RECORD_RESUMABLE_FRAME_SP

#if !defined(DACCESS_COMPILE)
    m_pCachedGSCookie = (GSCookie*)m_crawl.GetCodeManager()->GetGSCookieAddr(
                                                        m_crawl.pRD,
                                                        &m_crawl.codeInfo,
                                                        m_codeManFlags);
#endif // !DACCESS_COMPILE

    if (!(m_flags & SKIP_GSCOOKIE_CHECK) && m_pCachedGSCookie)
    {
        m_crawl.SetCurGSCookie(m_pCachedGSCookie);
    }

    INDEBUG(m_crawl.pThread->DebugLogStackWalkInfo(&m_crawl, "CONSIDER", m_uFramesProcessed));

#if defined(_DEBUG) && !defined(FEATURE_EH_FUNCLETS) && !defined(DACCESS_COMPILE)
    //
    // VM is responsible for synchronization on non-funclet EH model.
    //
    // m_crawl.GetThisPointer() requires full unwind
    // In GC's relocate phase, objects is not verifiable
    if ( !(m_flags & (LIGHTUNWIND | QUICKUNWIND | ALLOW_INVALID_OBJECTS)) &&
         m_crawl.pFunc->IsSynchronized() &&
         !m_crawl.pFunc->IsStatic()      &&
         m_crawl.GetCodeManager()->IsInSynchronizedRegion(m_crawl.GetRelOffset(),
                                                         m_crawl.GetGCInfoToken(),
                                                         m_crawl.GetCodeManagerFlags()))
    {
        BEGIN_GCX_ASSERT_COOP;

        OBJECTREF obj = m_crawl.GetThisPointer();

        _ASSERTE(obj != NULL);
        VALIDATEOBJECTREF(obj);

        DWORD threadId = 0;
        DWORD acquisitionCount = 0;
        _ASSERTE(obj->GetThreadOwningMonitorLock(&threadId, &acquisitionCount) &&
                 (threadId == m_crawl.pThread->GetThreadId()));

        END_GCX_ASSERT_COOP;
    }
#endif // _DEBUG && !FEATURE_EH_FUNCLETS && !DACCESS_COMPILE

    m_frameState = SFITER_FRAMELESS_METHOD;
} // StackFrameIterator::PreProcessingForManagedFrames()

//---------------------------------------------------------------------------------------
//
// Perform the necessary tasks after stopping at a managed stack frame and unwinding to its caller.
// This includes advancing the ExInfo and checking whether the new IP is managed.
//

void StackFrameIterator::PostProcessingForManagedFrames(void)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACTL_END;


#if defined(ELIMINATE_FEF)
    // As with frames, we may have unwound past a ExInfo.pContext.  This
    //  can happen when unwinding from a handler that rethrew the exception.
    //  Skip any ExInfo.pContext records that may no longer be valid.
    // If Frames would be unlinked from the Frame chain, also reset the UseExInfoForStackwalk bit
    //  on the ExInfo.
    m_exInfoWalk.WalkToPosition(GetRegdisplaySP(m_crawl.pRD), (m_flags & POPFRAMES));
#endif // ELIMINATE_FEF

    ProcessIp(GetControlPC(m_crawl.pRD));

    // if we have unwound to a native stack frame, stop and set the frame state accordingly
    if (!m_crawl.isFrameless)
    {
        m_frameState = SFITER_NATIVE_MARKER_FRAME;
        m_crawl.isNativeMarker = true;
    }
} // StackFrameIterator::PostProcessingForManagedFrames()

//---------------------------------------------------------------------------------------
//
// Perform the necessary tasks after stopping at a no-frame transition.  This includes loading
// the CONTEXT stored in the ExInfo and updating the REGDISPLAY to the faulting managed stack frame.
//

void StackFrameIterator::PostProcessingForNoFrameTransition()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_ANY;
        SUPPORTS_DAC;
    }
    CONTRACTL_END;

#if defined(ELIMINATE_FEF)
    PTR_CONTEXT pContext = m_exInfoWalk.GetContext();

    // Get the JitManager for the managed address.
    m_crawl.codeInfo.Init(GetIP(pContext), m_scanFlag);
    _ASSERTE(m_crawl.codeInfo.IsValid());

    STRESS_LOG4(LF_EH, LL_INFO100, "STACKWALK: resync from pContext(%p); pStartFrame(%p), \
                cf.pFrame(%p), cf.SP(%p)\n",
                dac_cast<TADDR>(pContext), dac_cast<TADDR>(m_pStartFrame), dac_cast<TADDR>(m_crawl.pFrame),
                GetRegdisplaySP(m_crawl.pRD));

    // Update the RegDisplay from the context info.
    FillRegDisplay(m_crawl.pRD, pContext);

    // Now we know where we are, and it's "frameless", aka managed.
    m_crawl.isFrameless = true;

    // Flags the same as from a FaultingExceptionFrame.
    m_crawl.isInterrupted = true;
    m_crawl.hasFaulted = (pContext->ContextFlags & CONTEXT_EXCEPTION_ACTIVE) != 0;
    m_crawl.isIPadjusted = false;
    if (!m_crawl.hasFaulted)
    {
        // If the context is from a hardware exception that happened in a helper where we have unwound
        // the exception location to the caller of the helper, the frame needs to be marked as not
        // being the first one. The COMPlusThrowCallback uses this information to decide whether
        // the current IP should or should not be included in the try region range. The call to
        // the helper that has fired the exception may be the last instruction in the try region.
        m_crawl.isFirst = false;
    }

#if defined(STACKWALKER_MAY_POP_FRAMES)
    // If Frames would be unlinked from the Frame chain, also reset the UseExInfoForStackwalk bit
    //  on the ExInfo.
    if (m_flags & POPFRAMES)
    {
        m_exInfoWalk.GetExInfo()->m_ExceptionFlags.ResetUseExInfoForStackwalk();
    }
#endif // STACKWALKER_MAY_POP_FRAMES

    // Done with this ExInfo.
    m_exInfoWalk.WalkOne();

    m_crawl.isNoFrameTransition = false;
    m_crawl.taNoFrameTransitionMarker = NULL;
#endif // ELIMINATE_FEF
} // StackFrameIterator::PostProcessingForNoFrameTransition()

#ifdef FEATURE_EH_FUNCLETS
void StackFrameIterator::ResetNextExInfoForSP(TADDR SP)
{
    while (m_pNextExInfo && (SP > (TADDR)(m_pNextExInfo)))
    {
        m_pNextExInfo = (PTR_ExInfo)m_pNextExInfo->m_pPrevNestedInfo;
    }
}
#endif // FEATURE_EH_FUNCLETS

//----------------------------------------------------------------------------
//
// SetUpRegdisplayForStackWalk - set up Regdisplay for a stack walk
//
// Arguments:
//    pThread - pointer to the managed thread to be crawled
//    pContext - pointer to the context
//    pRegdisplay - pointer to the REGDISPLAY to be filled
//
// Return Value:
//    None
//
//----------------------------------------------------------------------------
void SetUpRegdisplayForStackWalk(Thread * pThread, T_CONTEXT * pContext, REGDISPLAY * pRegdisplay)
{
    CONTRACTL {
       NOTHROW;
       GC_NOTRIGGER;
       SUPPORTS_DAC;
    } CONTRACTL_END;

    // @dbgtodo  filter CONTEXT- The filter CONTEXT will be removed in V3.0.
    T_CONTEXT * pFilterContext = pThread->GetFilterContext();
    _ASSERTE(!(pFilterContext && ISREDIRECTEDTHREAD(pThread)));

    if (pFilterContext != NULL)
    {
        FillRegDisplay(pRegdisplay, pFilterContext);
    }
    else
    {
        ZeroMemory(pContext, sizeof(*pContext));
        FillRegDisplay(pRegdisplay, pContext);

        if (ISREDIRECTEDTHREAD(pThread))
        {
            pThread->GetFrame()->UpdateRegDisplay(pRegdisplay);
        }
    }
}
