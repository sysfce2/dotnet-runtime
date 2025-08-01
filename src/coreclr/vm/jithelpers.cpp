// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "jitinterface.h"
#include "codeman.h"
#include "method.hpp"
#include "class.h"
#include "object.h"
#include "field.h"
#include "stublink.h"
#include "virtualcallstub.h"
#include "corjit.h"
#include "eeconfig.h"
#include "excep.h"
#include "log.h"
#include "excep.h"
#include "float.h"      // for isnan
#include "dbginterface.h"
#include "dllimport.h"
#include "gcheaputilities.h"
#include "comdelegate.h"
#include "corprof.h"
#include "eeprofinterfaces.h"
#include "dynamicinterfacecastable.h"
#include "comsynchronizable.h"

#ifndef TARGET_UNIX
// Included for referencing __report_gsfailure
#include "process.h"
#endif // !TARGET_UNIX

#ifdef PROFILING_SUPPORTED
#include "proftoeeinterfaceimpl.h"
#endif
#include "ecall.h"
#include "generics.h"
#include "typestring.h"
#include "typedesc.h"
#include "genericdict.h"
#include "array.h"
#include "debuginfostore.h"
#include "safemath.h"
#include "threadstatics.h"

#ifdef HAVE_GCCOVER
#include "gccover.h"
#endif // HAVE_GCCOVER

#include "runtimehandles.h"
#include "castcache.h"
#include "onstackreplacement.h"
#include "pgo.h"
#include "pgo_formatprocessing.h"
#include "patchpointinfo.h"

#ifndef FEATURE_EH_FUNCLETS
#include "excep.h"
#endif
#include "exinfo.h"
#include "arraynative.inl"

using std::isfinite;
using std::isnan;

//========================================================================
//
// This file contains implementation of all JIT helpers. The helpers are
// divided into following categories:
//
//      INTEGER ARITHMETIC HELPERS
//      FLOATING POINT HELPERS
//      INSTANCE FIELD HELPERS
//      STATIC FIELD HELPERS
//      SHARED STATIC FIELD HELPERS
//      CASTING HELPERS
//      ALLOCATION HELPERS
//      STRING HELPERS
//      ARRAY HELPERS
//      VALUETYPE/BYREF HELPERS
//      GENERICS HELPERS
//      EXCEPTION HELPERS
//      DEBUGGER/PROFILER HELPERS
//      GC HELPERS
//      INTEROP HELPERS
//
//========================================================================



//========================================================================
//
//      INTEGER ARITHMETIC HELPERS
//
//========================================================================

#include <optsmallperfcritical.h>

//
// helper macro to get high 32-bit of 64-bit int
//
#define Hi32Bits(a)         ((UINT32)((UINT64)(a) >> 32))

//
// helper macro to check whether 64-bit signed int fits into 32-bit signed (compiles into one 32-bit compare)
//
#define Is32BitSigned(a)    (Hi32Bits(a) == Hi32Bits((INT64)(INT32)(a)))

#if !defined(TARGET_X86) || defined(TARGET_UNIX)
/*********************************************************************/
HCIMPL2_VV(INT64, JIT_LMul, INT64 val1, INT64 val2)
{
    FCALL_CONTRACT;

    return (val1 * val2);
}
HCIMPLEND
#endif // !TARGET_X86 || TARGET_UNIX

/*********************************************************************/
extern "C" FCDECL2(INT32, JIT_Div, INT32 dividend, INT32 divisor);
extern "C" FCDECL2(INT32, JIT_Mod, INT32 dividend, INT32 divisor);
extern "C" FCDECL2(UINT32, JIT_UDiv, UINT32 dividend, UINT32 divisor);
extern "C" FCDECL2(UINT32, JIT_UMod, UINT32 dividend, UINT32 divisor);
extern "C" FCDECL2_VV(INT64, JIT_LDiv, INT64 dividend, INT64 divisor);
extern "C" FCDECL2_VV(INT64, JIT_LMod, INT64 dividend, INT64 divisor);
extern "C" FCDECL2_VV(UINT64, JIT_ULDiv, UINT64 dividend, UINT64 divisor);
extern "C" FCDECL2_VV(UINT64, JIT_ULMod, UINT64 dividend, UINT64 divisor);

#if !defined(HOST_64BIT) && !defined(TARGET_X86)
/*********************************************************************/
HCIMPL2_VV(UINT64, JIT_LLsh, UINT64 num, int shift)
{
    FCALL_CONTRACT;
    return num << (shift & 0x3F);
}
HCIMPLEND

/*********************************************************************/
HCIMPL2_VV(INT64, JIT_LRsh, INT64 num, int shift)
{
    FCALL_CONTRACT;
    return num >> (shift & 0x3F);
}
HCIMPLEND

/*********************************************************************/
HCIMPL2_VV(UINT64, JIT_LRsz, UINT64 num, int shift)
{
    FCALL_CONTRACT;
    return num >> (shift & 0x3F);
}
HCIMPLEND
#endif // !HOST_64BIT && !TARGET_X86

#include <optdefault.h>


//========================================================================
//
//      FLOATING POINT HELPERS
//
//========================================================================

#include <optsmallperfcritical.h>

/*********************************************************************/
HCIMPL1_V(float, JIT_ULng2Flt, uint64_t val)
{
    FCALL_CONTRACT;
    return (float)val;
}
HCIMPLEND

/*********************************************************************/
HCIMPL1_V(double, JIT_ULng2Dbl, uint64_t val)
{
    FCALL_CONTRACT;
    return (double)val;
}
HCIMPLEND

/*********************************************************************/
HCIMPL1_V(float, JIT_Lng2Flt, int64_t val)
{
    FCALL_CONTRACT;
    return (float)val;
}
HCIMPLEND

/*********************************************************************/
HCIMPL1_V(double, JIT_Lng2Dbl, int64_t val)
{
    FCALL_CONTRACT;
    return (double)val;
}
HCIMPLEND

/*********************************************************************/
HCIMPL1_V(int64_t, JIT_Dbl2Lng, double val)
{
    FCALL_CONTRACT;

#if defined(TARGET_X86) || defined(TARGET_AMD64) || defined(TARGET_ARM)
    const double int64_min = -2147483648.0 * 4294967296.0;
    const double int64_max = 2147483648.0 * 4294967296.0;
    return (val != val) ? 0 : (val <= int64_min) ? INT64_MIN : (val >= int64_max) ? INT64_MAX : (int64_t)val;
#else
    return (int64_t)val;
#endif
}
HCIMPLEND

/*********************************************************************/
HCIMPL1_V(uint64_t, JIT_Dbl2ULng, double val)
{
    FCALL_CONTRACT;

#if defined(TARGET_X86) || defined(TARGET_AMD64)
    const double uint64_max_plus_1 = 4294967296.0 * 4294967296.0;
    // Note that this expression also works properly for val = NaN case
    return (val >= 0) ? ((val >= uint64_max_plus_1) ? UINT64_MAX : (uint64_t)val) : 0;
#else
    return (uint64_t)val;
#endif
}
HCIMPLEND

/*********************************************************************/
HCIMPL2_VV(float, JIT_FltRem, float dividend, float divisor)
{
    FCALL_CONTRACT;

    return fmodf(dividend, divisor);
}
HCIMPLEND

/*********************************************************************/
HCIMPL2_VV(double, JIT_DblRem, double dividend, double divisor)
{
    FCALL_CONTRACT;

    return fmod(dividend, divisor);
}
HCIMPLEND

#include <optdefault.h>

// Helper for the managed InitClass implementations
extern "C" void QCALLTYPE InitClassHelper(MethodTable* pMT)
{
    QCALL_CONTRACT;
    BEGIN_QCALL;

    _ASSERTE(pMT->IsFullyLoaded());
    pMT->EnsureInstanceActive();
    pMT->CheckRunClassInitThrowing();
    END_QCALL;
}

//========================================================================
//
//      SHARED STATIC FIELD HELPERS
//
//========================================================================

#include <optsmallperfcritical.h>

// No constructor version of JIT_GetSharedNonGCStaticBase.  Does not check if class has
// been initialized.
HCIMPL1(void*, JIT_GetNonGCStaticBaseNoCtor_Portable, MethodTable* pMT)
{
    FCALL_CONTRACT;

    return pMT->GetDynamicStaticsInfo()->GetNonGCStaticsPointerAssumeIsInited();
}
HCIMPLEND

// No constructor version of JIT_GetSharedNonGCStaticBase.  Does not check if class has
// been initialized.
HCIMPL1(void*, JIT_GetDynamicNonGCStaticBaseNoCtor_Portable, DynamicStaticsInfo* pDynamicStaticsInfo)
{
    FCALL_CONTRACT;

    return pDynamicStaticsInfo->GetNonGCStaticsPointerAssumeIsInited();
}
HCIMPLEND

// No constructor version of JIT_GetSharedGCStaticBase.  Does not check if class has been
// initialized.
HCIMPL1(void*, JIT_GetGCStaticBaseNoCtor_Portable, MethodTable* pMT)
{
    FCALL_CONTRACT;

    return pMT->GetDynamicStaticsInfo()->GetGCStaticsPointerAssumeIsInited();
}
HCIMPLEND

// No constructor version of JIT_GetSharedGCStaticBase.  Does not check if class has been
// initialized.
HCIMPL1(void*, JIT_GetDynamicGCStaticBaseNoCtor_Portable, DynamicStaticsInfo* pDynamicStaticsInfo)
{
    FCALL_CONTRACT;

    return pDynamicStaticsInfo->GetGCStaticsPointerAssumeIsInited();
}
HCIMPLEND

#include <optdefault.h>

//========================================================================
//
//      THREAD STATIC FIELD HELPERS
//
//========================================================================

// Using compiler specific thread local storage directives due to linkage issues.
#ifdef _MSC_VER
__declspec(selectany)
#endif // _MSC_VER
PLATFORM_THREAD_LOCAL ThreadLocalData t_ThreadStatics;

extern "C" void QCALLTYPE GetThreadStaticsByMethodTable(QCall::ByteRefOnStack refHandle, MethodTable* pMT, bool gcStatic)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    pMT->CheckRunClassInitThrowing();

    GCX_COOP();
    if (gcStatic)
    {
        refHandle.Set(pMT->GetGCThreadStaticsBasePointer());
    }
    else
    {
        refHandle.Set(pMT->GetNonGCThreadStaticsBasePointer());
    }

    END_QCALL;
}

extern "C" void QCALLTYPE GetThreadStaticsByIndex(QCall::ByteRefOnStack refHandle, uint32_t staticBlockIndex, bool gcStatic)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    TLSIndex tlsIndex(staticBlockIndex);
    // Check if the class constructor needs to be run
    MethodTable *pMT = LookupMethodTableForThreadStaticKnownToBeAllocated(tlsIndex);
    pMT->CheckRunClassInitThrowing();

    GCX_COOP();
    if (gcStatic)
    {
        refHandle.Set(pMT->GetGCThreadStaticsBasePointer());
    }
    else
    {
        refHandle.Set(pMT->GetNonGCThreadStaticsBasePointer());
    }

    END_QCALL;
}

// *** This helper corresponds CORINFO_HELP_GETSHARED_NONGCTHREADSTATIC_BASE_NOCTOR_OPTIMIZED2.
HCIMPL1(void*, JIT_GetNonGCThreadStaticBaseOptimized2, UINT32 staticBlockIndex)
{
    FCALL_CONTRACT;

    return ((BYTE*)&t_ThreadStatics) + staticBlockIndex;
}
HCIMPLEND

#include <optdefault.h>

//========================================================================
//
//      CASTING HELPERS
//
//========================================================================

static BOOL ObjIsInstanceOfCore(Object *pObject, TypeHandle toTypeHnd, BOOL throwCastException)
{
    CONTRACTL {
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
        PRECONDITION(CheckPointer(pObject));
    } CONTRACTL_END;

    BOOL fCast = FALSE;
    MethodTable* pMT = pObject->GetMethodTable();

    OBJECTREF obj = ObjectToOBJECTREF(pObject);
    GCPROTECT_BEGIN(obj);

    // we check nullable case first because it is not cacheable.
    // object castability and type castability disagree on T --> Nullable<T>,
    // so we can't put this in the cache
    if (Nullable::IsNullableForType(toTypeHnd, pMT))
    {
        // allow an object of type T to be cast to Nullable<T> (they have the same representation)
        fCast = TRUE;
    }
    else if (toTypeHnd.IsTypeDesc())
    {
        CastCache::TryAddToCache(pMT, toTypeHnd, FALSE);
        fCast = FALSE;
    }
    else if (pMT->CanCastTo(toTypeHnd.AsMethodTable(), /* pVisited */ NULL))
    {
        fCast = TRUE;
    }
    else if (toTypeHnd.IsInterface())
    {
#ifdef FEATURE_COMINTEROP
        // If we are casting a COM object from interface then we need to do a check to see
        // if it implements the interface.
        if (pMT->IsComObjectType())
        {
            fCast = ComObject::SupportsInterface(obj, toTypeHnd.AsMethodTable());
        }
        else
#endif // FEATURE_COMINTEROP
        if (pMT->IsIDynamicInterfaceCastable())
        {
            fCast = DynamicInterfaceCastable::IsInstanceOf(&obj, toTypeHnd, throwCastException);
        }
    }

    if (!fCast && throwCastException)
    {
        COMPlusThrowInvalidCastException(&obj, toTypeHnd);
    }

    GCPROTECT_END(); // obj

    return(fCast);
}

BOOL ObjIsInstanceOf(Object* pObject, TypeHandle toTypeHnd, BOOL throwCastException)
{
    CONTRACTL{
        THROWS;
        GC_TRIGGERS;
        MODE_COOPERATIVE;
        PRECONDITION(CheckPointer(pObject));
    } CONTRACTL_END;

    MethodTable* pMT = pObject->GetMethodTable();
    TypeHandle::CastResult result = CastCache::TryGetFromCache(pMT, toTypeHnd);

    if (result == TypeHandle::CanCast ||
        (result == TypeHandle::CannotCast && !throwCastException))
    {
        return (BOOL)result;
    }

    return ObjIsInstanceOfCore(pObject, toTypeHnd, throwCastException);
}

extern "C" BOOL QCALLTYPE IsInstanceOf_NoCacheLookup(EnregisteredTypeHandle type, BOOL throwCastException, QCall::ObjectHandleOnStack objOnStack)
{
    QCALL_CONTRACT;
    BOOL result = FALSE;

    BEGIN_QCALL;

    GCX_COOP();

    result = ObjIsInstanceOfCore(OBJECTREFToObject(objOnStack.Get()), TypeHandle::FromPtr(type), throwCastException);

    END_QCALL;

    return result;
}

//========================================================================
//
//      VALUETYPE/BYREF HELPERS
//
//========================================================================
/*************************************************************/
HCIMPL2(BOOL, JIT_IsInstanceOfException, EnregisteredTypeHandle type, Object* obj)
{
    FCALL_CONTRACT;
    return ExceptionIsOfRightType(TypeHandle::FromPtr(type), obj->GetTypeHandle());
}
HCIMPLEND

extern "C" void QCALLTYPE ThrowInvalidCastException(EnregisteredTypeHandle pSourceType, EnregisteredTypeHandle pTargetType)
{
    QCALL_CONTRACT;

    BEGIN_QCALL;

    COMPlusThrowInvalidCastException(TypeHandle::FromPtr(pSourceType), TypeHandle::FromPtr(pTargetType));

    END_QCALL;
}

//========================================================================
//
//      GENERICS HELPERS
//
//========================================================================

DictionaryEntry GenericHandleWorkerCore(MethodDesc * pMD, MethodTable * pMT, LPVOID signature, DWORD dictionaryIndexAndSlot, Module* pModule)
{
    STANDARD_VM_CONTRACT;

    DictionaryEntry result = NULL;

    _ASSERTE(pMT != NULL || pMD != NULL);
    _ASSERTE(pMT == NULL || pMD == NULL);

    uint32_t dictionaryIndex = 0;
    MethodTable * pDeclaringMT = NULL;

    if (pMT != NULL)
    {
        if (pModule != NULL)
        {
#ifdef _DEBUG
            // Only in R2R mode are the module, dictionary index and dictionary slot provided as an input
            _ASSERTE(dictionaryIndexAndSlot != (DWORD)-1);
            _ASSERT(ReadyToRunInfo::IsNativeImageSharedBy(pModule, ExecutionManager::FindReadyToRunModule(dac_cast<TADDR>(signature))));
#endif
            dictionaryIndex = (dictionaryIndexAndSlot >> 16);
        }
        else
        {
            SigPointer ptr((PCCOR_SIGNATURE)signature);

            uint32_t kind; // DictionaryEntryKind
            IfFailThrow(ptr.GetData(&kind));

            // We need to normalize the class passed in (if any) for reliability purposes. That's because preparation of a code region that
            // contains these handle lookups depends on being able to predict exactly which lookups are required (so we can pre-cache the
            // answers and remove any possibility of failure at runtime). This is hard to do if the lookup (in this case the lookup of the
            // dictionary overflow cache) is keyed off the somewhat arbitrary type of the instance on which the call is made (we'd need to
            // prepare for every possible derived type of the type containing the method). So instead we have to locate the exactly
            // instantiated (non-shared) super-type of the class passed in.

            _ASSERTE(dictionaryIndexAndSlot == (DWORD)-1);
            IfFailThrow(ptr.GetData(&dictionaryIndex));
        }

        pDeclaringMT = pMT;
        while (true)
        {
            MethodTable * pParentMT = pDeclaringMT->GetParentMethodTable();
            if (pParentMT->GetNumDicts() <= dictionaryIndex)
                break;
            pDeclaringMT = pParentMT;
        }
    }

    DictionaryEntry * pSlot;
    result = Dictionary::PopulateEntry(pMD, pDeclaringMT, signature, FALSE, &pSlot, dictionaryIndexAndSlot, pModule);

    if (pMT != NULL && pDeclaringMT != pMT)
    {
        // If the dictionary on the base type got expanded, update the current type's base type dictionary
        // pointer to use the new one on the base type.

        Dictionary* pMTDictionary = pMT->GetPerInstInfo()[dictionaryIndex];
        Dictionary* pDeclaringMTDictionary = pDeclaringMT->GetPerInstInfo()[dictionaryIndex];
        if (pMTDictionary != pDeclaringMTDictionary)
        {
            TypeHandle** pPerInstInfo = (TypeHandle**)pMT->GetPerInstInfo();
            InterlockedExchangeT(pPerInstInfo + dictionaryIndex, (TypeHandle*)pDeclaringMTDictionary);
        }
    }

    return result;
}

extern "C" void* QCALLTYPE GenericHandleWorker(MethodDesc * pMD, MethodTable * pMT, LPVOID signature, DWORD dictionaryIndexAndSlot, Module* pModule)
{
    QCALL_CONTRACT;

    void* result = NULL;

    BEGIN_QCALL;

    result = GenericHandleWorkerCore(pMD, pMT, signature, dictionaryIndexAndSlot, pModule);

    END_QCALL;

    return result;
} // GenericHandleWorker

FieldDesc* g_pVirtualFunctionPointerCache;

void FlushGenericCache(PTR_GenericCacheStruct genericCache)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    int32_t lastSize = genericCache->CacheElementCount();
    if (lastSize < genericCache->GetInitialCacheSize())
    {
        lastSize = genericCache->GetInitialCacheSize();
    }

    // store the last size to use when creating a new table
    // it is just a hint, not needed for correctness, so no synchronization
    // with the writing of the table
    genericCache->SetLastFlushSize(lastSize);
    // flushing is just replacing the table with a sentinel.
    genericCache->SetTable(genericCache->GetSentinelTable());
}

void FlushVirtualFunctionPointerCaches()
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
        MODE_COOPERATIVE;
    }
    CONTRACTL_END;

    FieldDesc *virtualCache = VolatileLoad(&g_pVirtualFunctionPointerCache);

    if (virtualCache != NULL)
    {
        // We can't use GetCurrentStaticAddress, as that may throw, since it will attempt to
        // allocate memory for statics if that hasn't happened yet. But, since we force the
        // statics memory to be allocated before initializing g_pVirtualFunctionPointerCache
        // we can safely use the combo of GetBase and GetStaticAddress here.
        FlushGenericCache((PTR_GenericCacheStruct)virtualCache->GetStaticAddress(virtualCache->GetBase()));
    }
}

/*********************************************************************/
// Resolve a virtual method at run-time, either because of
// aggressive backpatching or because the call is to a generic
// method which is itself virtual.
//
// classHnd is the actual run-time type for the call is made. (May be NULL for cases where methodHnd describes an interface)
// methodHnd is the exact (instantiated) method descriptor corresponding to the
// static method signature (i.e. might be for a superclass of classHnd)

// slow helper to call from the fast one
extern "C" PCODE QCALLTYPE ResolveVirtualFunctionPointer(QCall::ObjectHandleOnStack obj,
                                                       EnregisteredTypeHandle classHnd,
                                                       MethodDesc* pStaticMD)
{
    QCALL_CONTRACT;

    // The address of the method that's returned.
    PCODE addr = (PCODE)NULL;

    BEGIN_QCALL;

    if (VolatileLoadWithoutBarrier(&g_pVirtualFunctionPointerCache) == NULL)
    {
        {
            GCX_COOP();
            CoreLibBinder::GetClass(CLASS__VIRTUALDISPATCHHELPERS)->CheckRunClassInitThrowing();
        }

        VolatileStore(&g_pVirtualFunctionPointerCache, CoreLibBinder::GetField(FIELD__VIRTUALDISPATCHHELPERS__CACHE));
#ifdef DEBUG
        FieldDesc *virtualCache = VolatileLoad(&g_pVirtualFunctionPointerCache);
        GenericCacheStruct::ValidateLayout(virtualCache->GetApproxFieldTypeHandleThrowing().GetMethodTable());
#endif
    }

    GCX_COOP();
    OBJECTREF objRef = obj.Get();
    GCPROTECT_BEGIN(objRef);

    if (objRef == NULL)
        COMPlusThrow(kNullReferenceException);

    // This is the static method descriptor describing the call.
    // It is not the destination of the call, which we must compute.
    TypeHandle staticTH = TypeHandle::FromPtr(classHnd);

    if (staticTH.IsNull())
    {
        // This may be NULL on input for cases where the methodHnd is not an interface method, or if getting the method table from the
        // MethodDesc will return an exact type.
        if (pStaticMD->IsInterface())
        {
            staticTH = pStaticMD->GetMethodTable();
            _ASSERTE(!staticTH.IsCanonicalSubtype());
        }
    }

    pStaticMD->CheckRestore();

    // ReadyToRun: If the method was compiled using ldvirtftn to reference a non-virtual method
    // resolve without using the VirtualizedCode call path here.
    // This can happen if the method was converted from virtual to non-virtual after the R2R image was created.
    // While this is not a common scenario and is documented as a breaking change, we should still handle it
    // as we have no good scheme for reporting an actionable error here.
    if (!pStaticMD->IsVtableMethod())
    {
        addr = pStaticMD->GetMultiCallableAddrOfCode();
        _ASSERTE(addr);
    }
    else
    {
        // This is the new way of resolving a virtual call, including generic virtual methods.
        // The code is now also used by reflection, remoting etc.
        addr = pStaticMD->GetMultiCallableAddrOfVirtualizedCode(&objRef, staticTH);
        _ASSERTE(addr);
    }

    GCPROTECT_END();
    END_QCALL;

    return addr;
}

HCIMPL3(void, Jit_NativeMemSet, void* pDest, int value, size_t length)
{
    _ASSERTE(pDest != nullptr);
    FCALL_CONTRACT;
    memset(pDest, value, length);
}
HCIMPLEND

// Helper for synchronized static methods in shared generics code
#include <optsmallperfcritical.h>
HCIMPL1(EnregisteredTypeHandle, JIT_GetClassFromMethodParam, MethodDesc* pMD)
    CONTRACTL {
        FCALL_CHECK;
        PRECONDITION(pMD != NULL);
    } CONTRACTL_END;

    MethodTable * pMT = pMD->GetMethodTable();
    _ASSERTE(!pMT->IsSharedByGenericInstantiations());

    return pMT;
HCIMPLEND
#include <optdefault.h>




//========================================================================
//
//      EXCEPTION HELPERS
//
//========================================================================

// In general, we want to use COMPlusThrow to throw exceptions.  However,
// the IL_Throw helper is a special case.  Here, we're called from
// managed code.  We have a guarantee that the first FS:0 handler
// is our COMPlusFrameHandler.  We could call COMPlusThrow(), which pushes
// another handler, but there is a significant (10% on JGFExceptionBench)
// performance gain if we avoid this by calling RaiseTheException()
// directly.
//

/*************************************************************/

#if defined(TARGET_X86)
EXTERN_C FCDECL1(void, IL_Throw,  Object* obj);
EXTERN_C HCIMPL2(void, IL_Throw_x86,  Object* obj, TransitionBlock* transitionBlock)
#else
HCIMPL1(void, IL_Throw,  Object* obj)
#endif
{
    FCALL_CONTRACT;

    /* Make no assumptions about the current machine state */
    ResetCurrentContext();

    OBJECTREF oref = ObjectToOBJECTREF(obj);

    Thread *pThread = GetThread();

    SoftwareExceptionFrame exceptionFrame;
#ifdef TARGET_X86
    exceptionFrame.UpdateContextFromTransitionBlock(transitionBlock);
#else
    RtlCaptureContext(exceptionFrame.GetContext());
#endif
    exceptionFrame.InitAndLink(pThread);

    FC_CAN_TRIGGER_GC();

#ifdef FEATURE_EH_FUNCLETS
    if (oref == 0)
        DispatchManagedException(kNullReferenceException);
    else
    if (!IsException(oref->GetMethodTable()))
    {
        GCPROTECT_BEGIN(oref);

        WrapNonCompliantException(&oref);

        GCPROTECT_END();
    }
    else
    {   // We know that the object derives from System.Exception

        // If the flag indicating ForeignExceptionRaise has been set,
        // then do not clear the "_stackTrace" field of the exception object.
        if (pThread->GetExceptionState()->IsRaisingForeignException())
        {
            ((EXCEPTIONREF)oref)->SetStackTraceString(NULL);
        }
        else
        {
            ((EXCEPTIONREF)oref)->ClearStackTracePreservingRemoteStackTrace();
        }
    }

    DispatchManagedException(oref, exceptionFrame.GetContext());
#elif defined(TARGET_X86)
    INSTALL_MANAGED_EXCEPTION_DISPATCHER;
    INSTALL_UNWIND_AND_CONTINUE_HANDLER;

#if defined(_DEBUG) && defined(TARGET_X86)
    g_ExceptionEIP = (PVOID)transitionBlock->m_ReturnAddress;
#endif // defined(_DEBUG) && defined(TARGET_X86)

    if (oref == 0)
        COMPlusThrow(kNullReferenceException);
    else
    if (!IsException(oref->GetMethodTable()))
    {
        GCPROTECT_BEGIN(oref);

        WrapNonCompliantException(&oref);

        GCPROTECT_END();
    }
    else
    {   // We know that the object derives from System.Exception

        // If the flag indicating ForeignExceptionRaise has been set,
        // then do not clear the "_stackTrace" field of the exception object.
        if (GetThread()->GetExceptionState()->IsRaisingForeignException())
        {
            ((EXCEPTIONREF)oref)->SetStackTraceString(NULL);
        }
        else
        {
            ((EXCEPTIONREF)oref)->ClearStackTracePreservingRemoteStackTrace();
        }
    }

    RaiseTheExceptionInternalOnly(oref, FALSE);

    UNINSTALL_UNWIND_AND_CONTINUE_HANDLER;
    UNINSTALL_MANAGED_EXCEPTION_DISPATCHER;
#else // FEATURE_EH_FUNCLETS
    PORTABILITY_ASSERT("IL_Throw");
#endif // FEATURE_EH_FUNCLETS

    FC_CAN_TRIGGER_GC_END();
    UNREACHABLE();
}
HCIMPLEND

/*************************************************************/

#if defined(TARGET_X86)
EXTERN_C FCDECL0(void, IL_Rethrow);
EXTERN_C HCIMPL1(void, IL_Rethrow_x86, TransitionBlock* transitionBlock)
#else
HCIMPL0(void, IL_Rethrow)
#endif
{
    FCALL_CONTRACT;

    Thread *pThread = GetThread();

    SoftwareExceptionFrame exceptionFrame;
#ifdef TARGET_X86
    exceptionFrame.UpdateContextFromTransitionBlock(transitionBlock);
#else
    RtlCaptureContext(exceptionFrame.GetContext());
#endif
    exceptionFrame.InitAndLink(pThread);

    FC_CAN_TRIGGER_GC();

#ifdef FEATURE_EH_FUNCLETS
    DispatchRethrownManagedException(exceptionFrame.GetContext());
#elif defined(TARGET_X86)
    INSTALL_MANAGED_EXCEPTION_DISPATCHER;
    INSTALL_UNWIND_AND_CONTINUE_HANDLER;

    OBJECTREF throwable = GetThread()->GetThrowable();
    if (throwable != NULL)
    {
        RaiseTheExceptionInternalOnly(throwable, TRUE);
    }
    else
    {
        // This can only be the result of bad IL (or some internal EE failure).
        _ASSERTE(!"No throwable on rethrow");
        RealCOMPlusThrow(kInvalidProgramException, (UINT)IDS_EE_RETHROW_NOT_ALLOWED);
    }

    UNINSTALL_UNWIND_AND_CONTINUE_HANDLER;
    UNINSTALL_MANAGED_EXCEPTION_DISPATCHER;
#else // FEATURE_EH_FUNCLETS
    PORTABILITY_ASSERT("IL_Rethrow");
#endif // FEATURE_EH_FUNCLETS

    FC_CAN_TRIGGER_GC_END();
    UNREACHABLE();
}
HCIMPLEND

#if defined(TARGET_X86)
EXTERN_C FCDECL1(void, IL_ThrowExact,  Object* obj);
EXTERN_C HCIMPL2(void, IL_ThrowExact_x86,  Object* obj, TransitionBlock* transitionBlock)
#else
HCIMPL1(void, IL_ThrowExact, Object* obj)
#endif
{
    FCALL_CONTRACT;

    /* Make no assumptions about the current machine state */
    ResetCurrentContext();

    OBJECTREF oref = ObjectToOBJECTREF(obj);
    GetThread()->GetExceptionState()->SetRaisingForeignException();

    Thread *pThread = GetThread();

    SoftwareExceptionFrame exceptionFrame;
#ifdef TARGET_X86
    exceptionFrame.UpdateContextFromTransitionBlock(transitionBlock);
#else
    RtlCaptureContext(exceptionFrame.GetContext());
#endif
    exceptionFrame.InitAndLink(pThread);

    FC_CAN_TRIGGER_GC();

#ifdef FEATURE_EH_FUNCLETS
    DispatchManagedException(oref, exceptionFrame.GetContext());
#elif defined(TARGET_X86)
    INSTALL_MANAGED_EXCEPTION_DISPATCHER;
    INSTALL_UNWIND_AND_CONTINUE_HANDLER;

#if defined(_DEBUG) && defined(TARGET_X86)
    g_ExceptionEIP = (PVOID)transitionBlock->m_ReturnAddress;
#endif // defined(_DEBUG) && defined(TARGET_X86)

    RaiseTheExceptionInternalOnly(oref, FALSE);

    UNINSTALL_UNWIND_AND_CONTINUE_HANDLER;
    UNINSTALL_MANAGED_EXCEPTION_DISPATCHER;
#else // FEATURE_EH_FUNCLETS
    PORTABILITY_ASSERT("IL_ThrowExact");
#endif // FEATURE_EH_FUNCLETS

    FC_CAN_TRIGGER_GC_END();
    UNREACHABLE();
}
HCIMPLEND

#ifndef STATUS_STACK_BUFFER_OVERRUN  // Not defined yet in CESDK includes
# define STATUS_STACK_BUFFER_OVERRUN      ((NTSTATUS)0xC0000409L)
#endif

/*********************************************************************
 * Kill process without using any potentially corrupted data:
 *      o Do not throw an exception
 *      o Do not call any indirect/virtual functions
 *      o Do not depend on any global data
 *
 * This function is used by the security checks for unsafe buffers (VC's -GS checks)
 */

void DoJITFailFast ()
{
    CONTRACTL {
        MODE_ANY;
        WRAPPER(GC_TRIGGERS);
        WRAPPER(THROWS);
    } CONTRACTL_END;

    LOG((LF_ALWAYS, LL_FATALERROR, "Unsafe buffer security check failure: Buffer overrun detected"));

#ifdef _DEBUG
    if (g_pConfig->fAssertOnFailFast())
        _ASSERTE(!"About to FailFast. set DOTNET_AssertOnFailFast=0 if this is expected");
#endif

#ifndef TARGET_UNIX
    // Use the function provided by the C runtime.
    //
    // Ideally, this function is called directly from managed code so
    // that the address of the managed function will be included in the
    // error log. However, this function is also used by the stackwalker.
    // To keep things simple, we just call it from here.
#if defined(TARGET_X86)
    __report_gsfailure();
#else // !defined(TARGET_X86)
    // On AMD64/IA64/ARM, we need to pass a stack cookie, which will be saved in the context record
    // that is used to raise the buffer-overrun exception by __report_gsfailure.
    __report_gsfailure((ULONG_PTR)0);
#endif // defined(TARGET_X86)
#else // TARGET_UNIX
    if(ETW_EVENT_ENABLED(MICROSOFT_WINDOWS_DOTNETRUNTIME_PRIVATE_PROVIDER_DOTNET_Context, FailFast))
    {
        // Fire an ETW FailFast event
        FireEtwFailFast(W("Unsafe buffer security check failure: Buffer overrun detected"),
                       (const PVOID)GetThread()->GetFrame()->GetIP(),
                       STATUS_STACK_BUFFER_OVERRUN,
                       COR_E_EXECUTIONENGINE,
                       GetClrInstanceId());
    }

    CrashDumpAndTerminateProcess(STATUS_STACK_BUFFER_OVERRUN);
#endif // !TARGET_UNIX
}

HCIMPL0(void, JIT_FailFast)
{
    FCALL_CONTRACT;
    DoJITFailFast ();
}
HCIMPLEND

//========================================================================
//
//      DEBUGGER/PROFILER HELPERS
//
//========================================================================

#if defined(_MSC_VER)
// VC++ Compiler intrinsic.
extern "C" void * _ReturnAddress(void);
#endif

/*********************************************************************/
// Callback for Just-My-Code probe
// Probe looks like:
//  if (*pFlag != 0) call JIT_DbgIsJustMyCode
// So this is only called if the flag (obtained by GetJMCFlagAddr) is
// non-zero.
HCIMPL0(void, JIT_DbgIsJustMyCode)
{
    FCALL_CONTRACT;

    // We need to get both the ip of the managed function this probe is in
    // (which will be our return address) and the frame pointer for that
    // function (since we can't get it later because we're pushing unmanaged
    // frames on the stack).
    void * ip = NULL;

    // <NOTE>
    // In order for the return address to be correct, we must NOT call any
    // function before calling _ReturnAddress().
    // </NOTE>
    ip = _ReturnAddress();

    _ASSERTE(ip != NULL);

    // Call into debugger proper
    g_pDebugInterface->OnMethodEnter(ip);

    return;
}
HCIMPLEND

#ifdef PROFILING_SUPPORTED

//---------------------------------------------------------------------------------------
//
// Sets the profiler's enter/leave/tailcall hooks into the JIT's dynamic helper
// function table.
//
// Arguments:
//      pFuncEnter - Enter hook
//      pFuncLeave - Leave hook
//      pFuncTailcall - Tailcall hook
//
//      For each hook parameter, if NULL is passed in, that will cause the JIT
//      to insert calls to its default stub replacement for that hook, which
//      just does a ret.
//
// Return Value:
//      HRESULT indicating success or failure
//
// Notes:
//      On IA64, this will allocate space for stubs to update GP, and that
//      allocation may take locks and may throw on failure.  Callers be warned.
//

HRESULT EEToProfInterfaceImpl::SetEnterLeaveFunctionHooksForJit(FunctionEnter3 * pFuncEnter,
                                                                FunctionLeave3 * pFuncLeave,
                                                                FunctionTailcall3 * pFuncTailcall)
{
    CONTRACTL {
        THROWS;
        GC_NOTRIGGER;
    } CONTRACTL_END;

    SetJitHelperFunction(
        CORINFO_HELP_PROF_FCN_ENTER,
        (pFuncEnter == NULL) ?
            reinterpret_cast<void *>(JIT_ProfilerEnterLeaveTailcallStub) :
            reinterpret_cast<void *>(pFuncEnter));

    SetJitHelperFunction(
        CORINFO_HELP_PROF_FCN_LEAVE,
        (pFuncLeave == NULL) ?
            reinterpret_cast<void *>(JIT_ProfilerEnterLeaveTailcallStub) :
            reinterpret_cast<void *>(pFuncLeave));

    SetJitHelperFunction(
        CORINFO_HELP_PROF_FCN_TAILCALL,
        (pFuncTailcall == NULL) ?
            reinterpret_cast<void *>(JIT_ProfilerEnterLeaveTailcallStub) :
            reinterpret_cast<void *>(pFuncTailcall));

    return (S_OK);
}
#endif // PROFILING_SUPPORTED




//========================================================================
//
//      GC HELPERS
//
//========================================================================

/*************************************************************/
// This helper is similar to JIT_RareDisableHelper, but has more operations
// tailored to the post-pinvoke operations.
extern "C" VOID JIT_PInvokeEndRarePath();

void JIT_PInvokeEndRarePath()
{
    PreserveLastErrorHolder preserveLastError;

    Thread *thread = GetThread();

    // We execute RareDisablePreemptiveGC manually before checking any abort conditions
    // as that operation may run the allocator, etc, and we need to have handled any suspensions requested
    // by the GC before we reach that point.
    thread->RareDisablePreemptiveGC();

    if (thread->IsAbortRequested())
    {
        // This function is called after a pinvoke finishes, in the rare case that either a GC
        // or ThreadAbort is requested. This means that the pinvoke frame is still on the stack and
        // enabled, but the thread has been marked as returning to cooperative mode. Thus we can
        // use that frame to provide GC suspension safety, but we need to manually call EnablePreemptiveGC
        // and DisablePreemptiveGC to put the function in a state where the BEGIN_QCALL/END_QCALL macros
        // will work correctly.
        thread->EnablePreemptiveGC();
        BEGIN_QCALL;
        thread->HandleThreadAbort();
        END_QCALL;
        thread->DisablePreemptiveGC();
    }

    thread->m_pFrame->Pop(thread);
}

/*************************************************************/
// For an inlined PInvoke call (and possibly for other places that need this service)
// we have noticed that the returning thread should trap for one reason or another.
// ECall sets up the frame.

extern "C" VOID JIT_RareDisableHelper();

#if defined(TARGET_ARM) || defined(TARGET_AMD64)
// The JIT expects this helper to preserve the return value on AMD64 and ARM. We should eventually
// switch other platforms to the same convention since it produces smaller code.
extern "C" VOID JIT_RareDisableHelperWorker();

void JIT_RareDisableHelperWorker()
#else
void JIT_RareDisableHelper()
#endif
{
    // We do this here (before we enter the BEGIN_QCALL macro), because the following scenario
    // We are in the process of doing an inlined pinvoke.  Since we are in preemtive
    // mode, the thread is allowed to continue.  The thread continues and gets a context
    // switch just after it has cleared the preemptive mode bit but before it gets
    // to this helper.    When we do our stack crawl now, we think this thread is
    // in cooperative mode (and believed that it was suspended in the SuspendEE), so
    // we do a getthreadcontext (on the unsuspended thread!) and get an EIP in jitted code.
    // and proceed.   Assume the crawl of jitted frames is proceeding on the other thread
    // when this thread wakes up and sets up a frame.   Eventually the other thread
    // runs out of jitted frames and sees the frame we just established.  This causes
    // an assert in the stack crawling code.  If this assert is ignored, however, we
    // will end up scanning the jitted frames twice, which will lead to GC holes
    //
    // <TODO>TODO:  It would be MUCH more robust if we should remember which threads
    // we suspended in the SuspendEE, and only even consider using EIP if it was suspended
    // in the first phase.
    //      </TODO>

    PreserveLastErrorHolder preserveLastError;

    Thread *thread = GetThread();
    // We execute RareDisablePreemptiveGC manually before checking any abort conditions
    // as that operation may run the allocator, etc, and we need to be have have handled any suspensions requested
    // by the GC before we reach that point.
    thread->RareDisablePreemptiveGC();

    if (thread->IsAbortRequested())
    {
        // This function is called after a pinvoke finishes, in the rare case that either a GC
        // or ThreadAbort is requested. This means that the pinvoke frame is still on the stack and
        // enabled, but the thread has been marked as returning to cooperative mode. Thus we can
        // use that frame to provide GC suspension safety, but we need to manually call EnablePreemptiveGC
        // and DisablePreemptiveGC to put the function in a state where the BEGIN_QCALL/END_QCALL macros
        // will work correctly.
        thread->EnablePreemptiveGC();
        BEGIN_QCALL;
        thread->HandleThreadAbort();
        END_QCALL;
        thread->DisablePreemptiveGC();
    }
}

FCIMPL0(INT32, JIT_GetCurrentManagedThreadId)
{
    FCALL_CONTRACT;

    Thread * pThread = GetThread();
    return pThread->GetThreadId();
}
FCIMPLEND

/*********************************************************************/
/* we don't use HCIMPL macros because we don't want the overhead even in debug mode */

HCIMPL1_RAW(Object*, JIT_CheckObj, Object* obj)
{
    FCALL_CONTRACT;

    if (obj != 0) {
        MethodTable* pMT = obj->GetMethodTable();
        if (!pMT->ValidateWithPossibleAV()) {
            _ASSERTE_ALL_BUILDS(!"Bad Method Table");
        }
    }
    return obj;
}
HCIMPLEND_RAW

static int loopChoice = 0;

// This function supports a JIT mode in which we're debugging the mechanism for loop cloning.
// We want to clone loops, then make a semi-random choice, on each execution of the loop,
// whether to run the original loop or the cloned copy.  We do this by incrementing the contents
// of a memory location, and testing whether the result is odd or even.  The "loopChoice" variable
// above provides that memory location, and this JIT helper merely informs the JIT of the address of
// "loopChoice".
HCIMPL0(void*, JIT_LoopCloneChoiceAddr)
{
     CONTRACTL {
        FCALL_CHECK;
     } CONTRACTL_END;

     return &loopChoice;
}
HCIMPLEND

// Prints a message that loop cloning optimization has occurred.
HCIMPL0(void, JIT_DebugLogLoopCloning)
{
     CONTRACTL {
        FCALL_CHECK;
     } CONTRACTL_END;

#ifdef _DEBUG
     minipal_log_print_info(">> Logging loop cloning optimization\n");
#endif
}
HCIMPLEND

#ifdef FEATURE_ON_STACK_REPLACEMENT

// Helper method to jit the OSR version of a method.
//
// Returns the address of the jitted code.
// Returns NULL if osr method can't be created.
static PCODE JitPatchpointWorker(MethodDesc* pMD, const EECodeInfo& codeInfo, int ilOffset)
{
    STANDARD_VM_CONTRACT;
    PCODE osrVariant = (PCODE)NULL;

    // Fetch the patchpoint info for the current method
    EEJitManager* jitMgr = ExecutionManager::GetEEJitManager();
    CodeHeader* codeHdr = jitMgr->GetCodeHeaderFromStartAddress(codeInfo.GetStartAddress());
    PTR_BYTE debugInfo = codeHdr->GetDebugInfo();
    PatchpointInfo* patchpointInfo = CompressDebugInfo::RestorePatchpointInfo(debugInfo);

    if (patchpointInfo == NULL)
    {
        // Unexpected, but not fatal
        STRESS_LOG1(LF_TIEREDCOMPILATION, LL_WARNING, "JitPatchpointWorker: failed to restore patchpoint info for Method=0x%pM\n", pMD);
        return (PCODE)NULL;
    }

    // Set up a new native code version for the OSR variant of this method.
    NativeCodeVersion osrNativeCodeVersion;
    {
        CodeVersionManager::LockHolder codeVersioningLockHolder;

        NativeCodeVersion currentNativeCodeVersion = codeInfo.GetNativeCodeVersion();
        ILCodeVersion ilCodeVersion = currentNativeCodeVersion.GetILCodeVersion();
        HRESULT hr = ilCodeVersion.AddNativeCodeVersion(pMD, NativeCodeVersion::OptimizationTier1OSR, &osrNativeCodeVersion, patchpointInfo, ilOffset);
        if (FAILED(hr))
        {
            // Unexpected, but not fatal
            STRESS_LOG1(LF_TIEREDCOMPILATION, LL_WARNING, "JitPatchpointWorker: failed to add native code version for Method=0x%pM\n", pMD);
            return (PCODE)NULL;
        }
    }

    // Invoke the jit to compile the OSR version
    LOG((LF_TIEREDCOMPILATION, LL_INFO10, "JitPatchpointWorker: creating OSR version of Method=0x%pM (%s::%s) at offset %d\n",
        pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset));

    PrepareCodeConfigBuffer configBuffer(osrNativeCodeVersion);
    PrepareCodeConfig *config = configBuffer.GetConfig();
    osrVariant = pMD->PrepareCode(config);

    return osrVariant;
}

static PCODE PatchpointOptimizationPolicy(TransitionBlock* pTransitionBlock, int* counter, int ilOffset, PerPatchpointInfo * ppInfo, const EECodeInfo& codeInfo, bool *pIsNewMethod)
{
    STATIC_CONTRACT_NOTHROW;
    STATIC_CONTRACT_GC_TRIGGERS;
    STATIC_CONTRACT_MODE_COOPERATIVE;

    // See if we have an OSR method for this patchpoint.
    PCODE osrMethodCode = ppInfo->m_osrMethodCode;
    *pIsNewMethod = false;
    TADDR ip = codeInfo.GetCodeAddress();

    MethodDesc* pMD = codeInfo.GetMethodDesc();

    // In the current implementation, counter is shared by all patchpoints
    // in a method, so no matter what happens below, we don't want to
    // impair those other patchpoints.
    //
    // One might be tempted, for instance, to set the counter for
    // invalid or ignored patchpoints to some high value to reduce
    // the amount of back and forth with the runtime, but this would
    // lock out other patchpoints in the method.
    //
    // So we always reset the counter to the bump value.
    //
    // In the implementation, counter is a location in a stack frame,
    // so we can update it without worrying about other threads.
    const int counterBump = g_pConfig->OSR_CounterBump();
    *counter = counterBump;

#ifdef _DEBUG
    const int ppId = ppInfo->m_patchpointId;
#endif

    if ((ppInfo->m_flags & PerPatchpointInfo::patchpoint_invalid) == PerPatchpointInfo::patchpoint_invalid)
    {
        LOG((LF_TIEREDCOMPILATION, LL_INFO1000, "PatchpointOptimizationPolicy: invalid patchpoint [%d] (0x%p) in Method=0x%pM (%s::%s) at offset %d\n",
                ppId, ip, pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset));

        goto DONE;
    }

    if (osrMethodCode == (PCODE)NULL)
    {
        // No OSR method yet, let's see if we should create one.
        //
        // First, optionally ignore some patchpoints to increase
        // coverage (stress mode).
        //
        // Because there are multiple patchpoints in a method, and
        // each OSR method covers the remainder of the method from
        // that point until the method returns, if we trigger on an
        // early patchpoint in a method, we may never see triggers on
        // a later one.

#ifdef _DEBUG
        const int lowId = g_pConfig->OSR_LowId();
        const int highId = g_pConfig->OSR_HighId();

        if ((ppId < lowId) || (ppId > highId))
        {
            LOG((LF_TIEREDCOMPILATION, LL_INFO10, "PatchpointOptimizationPolicy: ignoring patchpoint [%d] (0x%p) in Method=0x%pM (%s::%s) at offset %d\n",
                    ppId, ip, pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset));
            goto DONE;
        }
#endif

        // Second, only request the OSR method if this patchpoint has
        // been hit often enough.
        //
        // Note the initial invocation of the helper depends on the
        // initial counter value baked into jitted code (call this J);
        // subsequent invocations depend on the counter bump (call
        // this B).
        //
        // J and B may differ, so the total number of loop iterations
        // before an OSR method is created is:
        //
        // J, if hitLimit <= 1;
        // J + (hitLimit-1)* B, if hitLimit > 1;
        //
        // Current thinking is:
        //
        // J should be in the range of tens to hundreds, so that newly
        // called Tier0 methods that already have OSR methods
        // available can transition to OSR methods quickly, but
        // methods called only a few times do not invoke this
        // helper and so create PerPatchpoint runtime state.
        //
        // B should be in the range of hundreds to thousands, so that
        // we're not too eager to create OSR methods (since there is
        // some jit cost), but are eager enough to transition before
        // we run too much Tier0 code.
        //
        const int hitLimit = g_pConfig->OSR_HitLimit();
        const int hitCount = InterlockedIncrement(&ppInfo->m_patchpointCount);
        const int hitLogLevel = (hitCount == 1) ? LL_INFO10 : LL_INFO1000;

        LOG((LF_TIEREDCOMPILATION, hitLogLevel, "PatchpointOptimizationPolicy: patchpoint [%d] (0x%p) hit %d in Method=0x%pM (%s::%s) [il offset %d] (limit %d)\n",
            ppId, ip, hitCount, pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset, hitLimit));

        // Defer, if we haven't yet reached the limit
        if (hitCount < hitLimit)
        {
            goto DONE;
        }

        // Third, make sure no other thread is trying to create the OSR method.
        LONG oldFlags = ppInfo->m_flags;
        if ((oldFlags & PerPatchpointInfo::patchpoint_triggered) == PerPatchpointInfo::patchpoint_triggered)
        {
            LOG((LF_TIEREDCOMPILATION, LL_INFO1000, "PatchpointOptimizationPolicy: AWAITING OSR method for patchpoint [%d] (0x%p)\n", ppId, ip));
            goto DONE;
        }

        LONG newFlags = oldFlags | PerPatchpointInfo::patchpoint_triggered;
        BOOL triggerTransition = InterlockedCompareExchange(&ppInfo->m_flags, newFlags, oldFlags) == oldFlags;

        if (!triggerTransition)
        {
            LOG((LF_TIEREDCOMPILATION, LL_INFO1000, "PatchpointOptimizationPolicy: (lost race) AWAITING OSR method for patchpoint [%d] (0x%p)\n", ppId, ip));
            goto DONE;
        }

        MAKE_CURRENT_THREAD_AVAILABLE();

    #ifdef _DEBUG
        Thread::ObjectRefFlush(CURRENT_THREAD);
    #endif

        DynamicHelperFrame frame(pTransitionBlock, 0);
        DynamicHelperFrame * pFrame = &frame;

        pFrame->Push(CURRENT_THREAD);

        INSTALL_MANAGED_EXCEPTION_DISPATCHER;
        INSTALL_UNWIND_AND_CONTINUE_HANDLER;

        GCX_PREEMP();

        osrMethodCode = ppInfo->m_osrMethodCode;
        if (osrMethodCode == (PCODE)NULL)
        {
            // Time to create the OSR method.
            //
            // We currently do this synchronously. We could instead queue
            // up a request on some worker thread, like we do for
            // rejitting, and return control to the Tier0 method. It may
            // eventually return here, if the patchpoint is hit often
            // enough.
            //
            // There is a chance the async version will create methods
            // that are never used (just like there is a chance that Tier1
            // methods are ever called).
            //
            // We want to expose bugs in the jitted code
            // for OSR methods, so we stick with synchronous creation.
            LOG((LF_TIEREDCOMPILATION, LL_INFO10, "PatchpointOptimizationPolicy: patchpoint [%d] (0x%p) TRIGGER at count %d\n", ppId, ip, hitCount));

            // Invoke the helper to build the OSR method
            osrMethodCode = JitPatchpointWorker(pMD, codeInfo, ilOffset);

            // If that failed, mark the patchpoint as invalid.
            if (osrMethodCode == (PCODE)NULL)
            {
                // Unexpected, but not fatal
                STRESS_LOG3(LF_TIEREDCOMPILATION, LL_WARNING, "PatchpointOptimizationPolicy: patchpoint (0x%p) OSR method creation failed,"
                    " marking patchpoint invalid for Method=0x%pM il offset %d\n", ip, pMD, ilOffset);

                InterlockedOr(&ppInfo->m_flags, (LONG)PerPatchpointInfo::patchpoint_invalid);
            }
            else
            {
                *pIsNewMethod = true;
                ppInfo->m_osrMethodCode = osrMethodCode;
            }
        }

        UNINSTALL_UNWIND_AND_CONTINUE_HANDLER;
        UNINSTALL_MANAGED_EXCEPTION_DISPATCHER;

        pFrame->Pop(CURRENT_THREAD);
    }
    return osrMethodCode;

DONE:
    return (PCODE)NULL;
}

static PCODE PatchpointRequiredPolicy(TransitionBlock* pTransitionBlock, int* counter, int ilOffset, PerPatchpointInfo * ppInfo, const EECodeInfo& codeInfo, bool *pIsNewMethod)
{
    STATIC_CONTRACT_NOTHROW;
    STATIC_CONTRACT_GC_TRIGGERS;
    STATIC_CONTRACT_MODE_COOPERATIVE;

    *pIsNewMethod = false;
    MethodDesc* pMD = codeInfo.GetMethodDesc();
    TADDR ip = codeInfo.GetCodeAddress();

#ifdef _DEBUG
    const int ppId = ppInfo->m_patchpointId;
#endif

    if ((ppInfo->m_flags & PerPatchpointInfo::patchpoint_invalid) == PerPatchpointInfo::patchpoint_invalid)
    {
        LOG((LF_TIEREDCOMPILATION, LL_FATALERROR, "PatchpointRequiredPolicy: invalid patchpoint [%d] (0x%p) in Method=0x%pM (%s::%s) at offset %d\n",
                ppId, ip, pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset));
        EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
    }

    MAKE_CURRENT_THREAD_AVAILABLE();

#ifdef _DEBUG
    Thread::ObjectRefFlush(CURRENT_THREAD);
#endif

    DynamicHelperFrame frame(pTransitionBlock, 0);
    DynamicHelperFrame * pFrame = &frame;

    pFrame->Push(CURRENT_THREAD);

    INSTALL_MANAGED_EXCEPTION_DISPATCHER;
    INSTALL_UNWIND_AND_CONTINUE_HANDLER;

    {
        GCX_PREEMP();

        DWORD backoffs = 0;
        while (ppInfo->m_osrMethodCode == (PCODE)NULL)
        {
            // Invalid patchpoints are fatal, for partial compilation patchpoints
            //
            if ((ppInfo->m_flags & PerPatchpointInfo::patchpoint_invalid) == PerPatchpointInfo::patchpoint_invalid)
            {
                LOG((LF_TIEREDCOMPILATION, LL_FATALERROR, "PatchpointRequiredPolicy: invalid patchpoint [%d] (0x%p) in Method=0x%pM (%s::%s) at offset %d\n",
                        ppId, ip, pMD, pMD->m_pszDebugClassName, pMD->m_pszDebugMethodName, ilOffset));
                EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
            }

            // Make sure no other thread is trying to create the OSR method.
            //
            LONG oldFlags = ppInfo->m_flags;
            if ((oldFlags & PerPatchpointInfo::patchpoint_triggered) == PerPatchpointInfo::patchpoint_triggered)
            {
                LOG((LF_TIEREDCOMPILATION, LL_INFO1000, "PatchpointRequiredPolicy: AWAITING OSR method for patchpoint [%d] (0x%p)\n", ppId, ip));
                __SwitchToThread(0, backoffs++);
                continue;
            }

            // Make sure we win the race to create the OSR method
            //
            LONG newFlags = ppInfo->m_flags | PerPatchpointInfo::patchpoint_triggered;
            BOOL triggerTransition = InterlockedCompareExchange(&ppInfo->m_flags, newFlags, oldFlags) == oldFlags;

            if (!triggerTransition)
            {
                LOG((LF_TIEREDCOMPILATION, LL_INFO1000, "PatchpointRequiredPolicy: (lost race) AWAITING OSR method for patchpoint [%d] (0x%p)\n", ppId, ip));
                __SwitchToThread(0, backoffs++);
                continue;
            }

            // Invoke the helper to build the OSR method
            //
            // TODO: may not want to optimize this part of the method, if it's truly partial compilation
            // and can't possibly rejoin into the main flow.
            //
            // (but consider: throw path in method with try/catch, OSR method will contain more than just the throw?)
            //
            LOG((LF_TIEREDCOMPILATION, LL_INFO10, "PatchpointRequiredPolicy: patchpoint [%d] (0x%p) TRIGGER\n", ppId, ip));
            PCODE newMethodCode = JitPatchpointWorker(pMD, codeInfo, ilOffset);

            // If that failed, mark the patchpoint as invalid.
            // This is fatal, for partial compilation patchpoints
            //
            if (newMethodCode == (PCODE)NULL)
            {
                STRESS_LOG3(LF_TIEREDCOMPILATION, LL_WARNING, "PatchpointRequiredPolicy: patchpoint (0x%p) OSR method creation failed,"
                    " marking patchpoint invalid for Method=0x%pM il offset %d\n", ip, pMD, ilOffset);
                InterlockedOr(&ppInfo->m_flags, (LONG)PerPatchpointInfo::patchpoint_invalid);
                EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
                break;
            }

            // We've successfully created the osr method; make it available.
            _ASSERTE(ppInfo->m_osrMethodCode == (PCODE)NULL);
            ppInfo->m_osrMethodCode = newMethodCode;
            *pIsNewMethod = true;
        }
    }

    UNINSTALL_UNWIND_AND_CONTINUE_HANDLER;
    UNINSTALL_MANAGED_EXCEPTION_DISPATCHER;

    pFrame->Pop(CURRENT_THREAD);

    // If we get here, we have code to transition to...
    PCODE osrMethodCode = ppInfo->m_osrMethodCode;
    _ASSERTE(osrMethodCode != (PCODE)NULL);

    return osrMethodCode;
}

// Jit helper invoked at a patchpoint.
//
// Checks to see if this is a known patchpoint, if not,
// an entry is added to the patchpoint table.
//
// When the patchpoint has been hit often enough to trigger
// a transition, create an OSR method. OR if the first argument
// is NULL, always create an OSR method and transition to it.
//
// Currently, counter(the first argument) is a pointer into the Tier0 method stack
// frame if it exists so we have exclusive access.

extern "C" void JIT_PatchpointWorkerWorkerWithPolicy(TransitionBlock * pTransitionBlock)
{
    // Manually preserve the last error as we may not return normally from this method.
    DWORD dwLastError = ::GetLastError();

    // This method may not return normally
    STATIC_CONTRACT_NOTHROW;
    STATIC_CONTRACT_GC_TRIGGERS;
    STATIC_CONTRACT_MODE_COOPERATIVE;

    PTR_PCODE pReturnAddress = (PTR_PCODE)(((BYTE*)pTransitionBlock) + TransitionBlock::GetOffsetOfReturnAddress());
    PCODE ip = *pReturnAddress;
    int* counter = *(int**)GetFirstArgumentRegisterValuePtr(pTransitionBlock);
    int ilOffset = *(int*)GetSecondArgumentRegisterValuePtr(pTransitionBlock);
    int hitCount = 1; // This will stay at 1 for forced transition scenarios, but will be updated to the actual hit count for normal patch points

    // Patchpoint identity is the helper return address

    // Fetch or setup patchpoint info for this patchpoint.
    EECodeInfo codeInfo(ip);
    MethodDesc* pMD = codeInfo.GetMethodDesc();
    LoaderAllocator* allocator = pMD->GetLoaderAllocator();
    OnStackReplacementManager* manager = allocator->GetOnStackReplacementManager();
    PerPatchpointInfo * ppInfo = manager->GetPerPatchpointInfo(codeInfo.GetStartAddress(), ilOffset);

#ifdef _DEBUG
    const int ppId = ppInfo->m_patchpointId;
#endif

    bool isNewMethod = false;
    PCODE osrMethodCode = (PCODE)NULL;


    bool patchpointMustFindOptimizedCode = counter == NULL;

    if (patchpointMustFindOptimizedCode)
    {
        osrMethodCode = PatchpointRequiredPolicy(pTransitionBlock, counter, ilOffset, ppInfo, codeInfo, &isNewMethod);
    }
    else
    {
        osrMethodCode = PatchpointOptimizationPolicy(pTransitionBlock, counter, ilOffset, ppInfo, codeInfo, &isNewMethod);
    }

    if (osrMethodCode == (PCODE)NULL)
    {
        _ASSERTE(!patchpointMustFindOptimizedCode);
        goto DONE;
    }

    // If we get here, we have code to transition to...

    {
        Thread *pThread = GetThread();

#ifdef FEATURE_HIJACK
        // We can't crawl the stack of a thread that currently has a hijack pending
        // (since the hijack routine won't be recognized by any code manager). So we
        // Undo any hijack, the EE will re-attempt it later.
        pThread->UnhijackThread();
#endif

        // Find context for the original method
        CONTEXT *pFrameContext = NULL;
#if defined(TARGET_WINDOWS) && defined(TARGET_AMD64)
        DWORD contextSize = 0;
        ULONG64 xStateCompactionMask = 0;
        DWORD contextFlags = CONTEXT_FULL;
        if (Thread::AreShadowStacksEnabled())
        {
            xStateCompactionMask = XSTATE_MASK_CET_U;
            contextFlags |= CONTEXT_XSTATE;
        }

        // The initialize call should fail but return contextSize
        BOOL success = g_pfnInitializeContext2 ?
            g_pfnInitializeContext2(NULL, contextFlags, NULL, &contextSize, xStateCompactionMask) :
            InitializeContext(NULL, contextFlags, NULL, &contextSize);

        _ASSERTE(!success && (GetLastError() == ERROR_INSUFFICIENT_BUFFER));

        PVOID pBuffer = _alloca(contextSize);
        success = g_pfnInitializeContext2 ?
            g_pfnInitializeContext2(pBuffer, contextFlags, &pFrameContext, &contextSize, xStateCompactionMask) :
            InitializeContext(pBuffer, contextFlags, &pFrameContext, &contextSize);
        _ASSERTE(success);
#else // TARGET_WINDOWS && TARGET_AMD64
        CONTEXT frameContext;
        frameContext.ContextFlags = CONTEXT_FULL;
        pFrameContext = &frameContext;
#endif // TARGET_WINDOWS && TARGET_AMD64

        // Find context for the original method
        RtlCaptureContext(pFrameContext);

#if defined(TARGET_WINDOWS) && defined(TARGET_AMD64)
        if (Thread::AreShadowStacksEnabled())
        {
            pFrameContext->ContextFlags |= CONTEXT_XSTATE;
            SetXStateFeaturesMask(pFrameContext, xStateCompactionMask);
            SetSSP(pFrameContext, _rdsspq());
        }
#endif // TARGET_WINDOWS && TARGET_AMD64

        // Walk back to the original method frame
        pThread->VirtualUnwindToFirstManagedCallFrame(pFrameContext);

        // Remember original method FP and SP because new method will inherit them.
        UINT_PTR currentSP = GetSP(pFrameContext);
        UINT_PTR currentFP = GetFP(pFrameContext);

        // We expect to be back at the right IP
        if ((UINT_PTR)ip != GetIP(pFrameContext))
        {
            // Should be fatal
            STRESS_LOG2(LF_TIEREDCOMPILATION, LL_FATALERROR, "Jit_Patchpoint: patchpoint (0x%p) TRANSITION"
                " unexpected context IP 0x%p\n", ip, GetIP(pFrameContext));
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_EXECUTIONENGINE);
        }

        // Now unwind back to the original method caller frame.
        EECodeInfo callerCodeInfo(GetIP(pFrameContext));
        ULONG_PTR establisherFrame = 0;
        PVOID handlerData = NULL;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, callerCodeInfo.GetModuleBase(), GetIP(pFrameContext), callerCodeInfo.GetFunctionEntry(),
            pFrameContext, &handlerData, &establisherFrame, NULL);

        // Now, set FP and SP back to the values they had just before this helper was called,
        // since the new method must have access to the original method frame.
        //
        // TODO: if we access the patchpointInfo here, we can read out the FP-SP delta from there and
        // use that to adjust the stack, likely saving some stack space.

#if defined(TARGET_AMD64)
        // If calls push the return address, we need to simulate that here, so the OSR
        // method sees the "expected" SP misalgnment on entry.
        _ASSERTE(currentSP % 16 == 0);
        currentSP -= 8;

#if defined(TARGET_WINDOWS)
        DWORD64 ssp = GetSSP(pFrameContext);
        if (ssp != 0)
        {
            SetSSP(pFrameContext, ssp - 8);
        }
#endif // TARGET_WINDOWS

        pFrameContext->Rbp = currentFP;
#endif // TARGET_AMD64

        SetSP(pFrameContext, currentSP);

        // Note we can get here w/o triggering, if there is an existing OSR method and
        // we hit the patchpoint.
        const int transitionLogLevel = isNewMethod ? LL_INFO10 : LL_INFO1000;
        LOG((LF_TIEREDCOMPILATION, transitionLogLevel, "Jit_Patchpoint: patchpoint [%d] (0x%p) TRANSITION to ip 0x%p\n", ppId, ip, osrMethodCode));

        // Install new entry point as IP
        SetIP(pFrameContext, osrMethodCode);

#ifdef _DEBUG
        // Keep this context around to aid in debugging OSR transition problems
        static CONTEXT s_lastOSRTransitionContext;
        s_lastOSRTransitionContext = *pFrameContext;
#endif

        // Restore last error (since call below does not return)
        ::SetLastError(dwLastError);

        // Transition!
        ClrRestoreNonvolatileContext(pFrameContext);
    }

 DONE:
    ::SetLastError(dwLastError);
}

#else

HCIMPL2(void, JIT_Patchpoint, int* counter, int ilOffset)
{
    // Stub version if OSR feature is disabled
    //
    // Should not be called.

    UNREACHABLE();
}
HCIMPLEND

HCIMPL1(VOID, JIT_PatchpointForced, int ilOffset)
{
    // Stub version if OSR feature is disabled
    //
    // Should not be called.

    UNREACHABLE();
}
HCIMPLEND

#endif // FEATURE_ON_STACK_REPLACEMENT

static unsigned HandleHistogramProfileRand()
{
    // Generate a random number (xorshift32)
    //
    // Intentionally simple for faster random. It's stored in TLS to avoid
    // multithread contention.
    //
    static thread_local unsigned s_rng = 100;

    unsigned x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

template<typename T>
FORCEINLINE static bool CheckSample(T* pIndex, size_t* sampleIndex)
{
    const unsigned S = ICorJitInfo::HandleHistogram32::SIZE;
    const unsigned N = ICorJitInfo::HandleHistogram32::SAMPLE_INTERVAL;
    static_assert_no_msg(N >= S);
    static_assert_no_msg((std::is_same<T, uint32_t>::value || std::is_same<T, uint64_t>::value));

    // If table is not yet full, just add entries in
    // and increment the table index.
    //
    T const index = *pIndex;

    if (index < S)
    {
        *sampleIndex = static_cast<size_t>(index);
        *pIndex = index + 1;
        return true;
    }

    unsigned const x = HandleHistogramProfileRand();

    // N is the sampling window size,
    // it should be larger than the table size.
    //
    // If we let N == count then we are building an entire
    // run sample -- probability of update decreases over time.
    // Would be a good strategy for an AOT profiler.
    //
    // But for TieredPGO we would prefer something that is more
    // weighted to recent observations.
    //
    // For S=4, N=128, we'll sample (on average) every 32nd call.
    //
    if ((x % N) >= S)
    {
        return false;
    }

    *sampleIndex = static_cast<size_t>(x % S);
    return true;
}

HCIMPL2(void, JIT_ValueProfile32, intptr_t val, ICorJitInfo::ValueHistogram32* valueProfile)
{
    FCALL_CONTRACT;

    size_t sampleIndex;
    if (!CheckSample(&valueProfile->Count, &sampleIndex))
    {
        return;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(valueProfile);
    PgoManager::VerifyAddress(valueProfile + 1);
#endif

    valueProfile->ValueTable[sampleIndex] = val;
}
HCIMPLEND

HCIMPL2(void, JIT_ValueProfile64, intptr_t val, ICorJitInfo::ValueHistogram64* valueProfile)
{
    FCALL_CONTRACT;

    size_t sampleIndex;
    if (!CheckSample(&valueProfile->Count, &sampleIndex))
    {
        return;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(valueProfile);
    PgoManager::VerifyAddress(valueProfile + 1);
#endif

    valueProfile->ValueTable[sampleIndex] = val;
}
HCIMPLEND

HCIMPL2(void, JIT_ClassProfile32, Object *obj, ICorJitInfo::HandleHistogram32* classProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t sampleIndex;
    if (!CheckSample(&classProfile->Count, &sampleIndex) || objRef == NULL)
    {
        return;
    }

    MethodTable* pMT = objRef->GetMethodTable();

    // If the object class is collectible, record an unknown typehandle.
    // We do this instead of recording NULL so that we won't over-estimate
    // the likelihood of known type handles.
    //
    if (pMT->Collectible())
    {
        pMT = (MethodTable*)DEFAULT_UNKNOWN_HANDLE;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(classProfile);
    PgoManager::VerifyAddress(classProfile + 1);
#endif

    classProfile->HandleTable[sampleIndex] = (CORINFO_CLASS_HANDLE)pMT;
}
HCIMPLEND

// Version of helper above used when the count is 64-bit
HCIMPL2(void, JIT_ClassProfile64, Object *obj, ICorJitInfo::HandleHistogram64* classProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t sampleIndex;
    if (!CheckSample(&classProfile->Count, &sampleIndex) || objRef == NULL)
    {
        return;
    }

    MethodTable* pMT = objRef->GetMethodTable();

    if (pMT->Collectible())
    {
        pMT = (MethodTable*)DEFAULT_UNKNOWN_HANDLE;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(classProfile);
    PgoManager::VerifyAddress(classProfile + 1);
#endif

    classProfile->HandleTable[sampleIndex] = (CORINFO_CLASS_HANDLE)pMT;
}
HCIMPLEND

HCIMPL2(void, JIT_DelegateProfile32, Object *obj, ICorJitInfo::HandleHistogram32* methodProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t methodSampleIndex;
    if (!CheckSample(&methodProfile->Count, &methodSampleIndex) || objRef == NULL)
    {
        return;
    }

    MethodTable* pMT = objRef->GetMethodTable();

    _ASSERTE(pMT->IsDelegate());

    // Resolve method. We handle only the common "direct" delegate as that is
    // in any case the only one we can reasonably do GDV for. For instance,
    // open delegates are filtered out here, and many cases with inner
    // "complicated" logic as well (e.g. static functions, multicast, unmanaged
    // functions).
    //
    MethodDesc* pRecordedMD = (MethodDesc*)DEFAULT_UNKNOWN_HANDLE;
    DELEGATEREF del = (DELEGATEREF)objRef;
    if ((del->GetInvocationCount() == 0) && (del->GetMethodPtrAux() == (PCODE)NULL))
    {
        MethodDesc* pMD = NonVirtualEntry2MethodDesc(del->GetMethodPtr());
        if ((pMD != nullptr) && !pMD->GetLoaderAllocator()->IsCollectible() && !pMD->IsDynamicMethod())
        {
            pRecordedMD = pMD;
        }
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(methodProfile);
    PgoManager::VerifyAddress(methodProfile + 1);
#endif

    // If table is not yet full, just add entries in.
    //
    methodProfile->HandleTable[methodSampleIndex] = (CORINFO_METHOD_HANDLE)pRecordedMD;
}
HCIMPLEND

// Version of helper above used when the count is 64-bit
HCIMPL2(void, JIT_DelegateProfile64, Object *obj, ICorJitInfo::HandleHistogram64* methodProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t methodSampleIndex;
    if (!CheckSample(&methodProfile->Count, &methodSampleIndex) || objRef == NULL)
    {
        return;
    }

    MethodTable* pMT = objRef->GetMethodTable();

    _ASSERTE(pMT->IsDelegate());

    // Resolve method. We handle only the common "direct" delegate as that is
    // in any case the only one we can reasonably do GDV for. For instance,
    // open delegates are filtered out here, and many cases with inner
    // "complicated" logic as well (e.g. static functions, multicast, unmanaged
    // functions).
    //
    MethodDesc* pRecordedMD = (MethodDesc*)DEFAULT_UNKNOWN_HANDLE;
    DELEGATEREF del = (DELEGATEREF)objRef;
    if ((del->GetInvocationCount() == 0) && (del->GetMethodPtrAux() == (PCODE)NULL))
    {
        MethodDesc* pMD = NonVirtualEntry2MethodDesc(del->GetMethodPtr());
        if ((pMD != nullptr) && !pMD->GetLoaderAllocator()->IsCollectible() && !pMD->IsDynamicMethod())
        {
            pRecordedMD = pMD;
        }
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(methodProfile);
    PgoManager::VerifyAddress(methodProfile + 1);
#endif

    // If table is not yet full, just add entries in.
    //
    methodProfile->HandleTable[methodSampleIndex] = (CORINFO_METHOD_HANDLE)pRecordedMD;
}
HCIMPLEND

HCIMPL3(void, JIT_VTableProfile32, Object* obj, MethodDesc* pBaseMD, ICorJitInfo::HandleHistogram32* methodProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t methodSampleIndex;
    if (!CheckSample(&methodProfile->Count, &methodSampleIndex) || objRef == NULL)
    {
        return;
    }

    // Method better be virtual
    _ASSERTE(pBaseMD->IsVirtual());

    // We do not expect to see interface methods here as we cannot efficiently
    // use method handle information for these anyway.
    _ASSERTE(!pBaseMD->IsInterface());

    // Shouldn't be doing this for instantiated methods as they live elsewhere
    _ASSERTE(!pBaseMD->HasMethodInstantiation());

    MethodTable* pMT = objRef->GetMethodTable();

    // Resolve method
    WORD slot = pBaseMD->GetSlot();
    _ASSERTE(slot < pBaseMD->GetMethodTable()->GetNumVirtuals());

    MethodDesc* pMD = pMT->GetMethodDescForSlot_NoThrow(slot);

    MethodDesc* pRecordedMD = (MethodDesc*)DEFAULT_UNKNOWN_HANDLE;
    if (!pMD->GetLoaderAllocator()->IsCollectible() && !pMD->IsDynamicMethod())
    {
        pRecordedMD = pMD;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(methodProfile);
    PgoManager::VerifyAddress(methodProfile + 1);
#endif

    methodProfile->HandleTable[methodSampleIndex] = (CORINFO_METHOD_HANDLE)pRecordedMD;
}
HCIMPLEND

HCIMPL3(void, JIT_VTableProfile64, Object* obj, MethodDesc* pBaseMD, ICorJitInfo::HandleHistogram64* methodProfile)
{
    FCALL_CONTRACT;

    OBJECTREF objRef = ObjectToOBJECTREF(obj);
    VALIDATEOBJECTREF(objRef);

    size_t methodSampleIndex;
    if (!CheckSample(&methodProfile->Count, &methodSampleIndex) || objRef == NULL)
    {
        return;
    }

    // Method better be virtual
    _ASSERTE(pBaseMD->IsVirtual());

    // We do not expect to see interface methods here as we cannot efficiently
    // use method handle information for these anyway.
    _ASSERTE(!pBaseMD->IsInterface());

    // Shouldn't be doing this for instantiated methods as they live elsewhere
    _ASSERTE(!pBaseMD->HasMethodInstantiation());

    MethodTable* pMT = objRef->GetMethodTable();

    // Resolve method
    WORD slot = pBaseMD->GetSlot();
    _ASSERTE(slot < pBaseMD->GetMethodTable()->GetNumVirtuals());

    MethodDesc* pMD = pMT->GetMethodDescForSlot_NoThrow(slot);

    MethodDesc* pRecordedMD = (MethodDesc*)DEFAULT_UNKNOWN_HANDLE;
    if (!pMD->GetLoaderAllocator()->IsCollectible() && !pMD->IsDynamicMethod())
    {
        pRecordedMD = pMD;
    }

#ifdef _DEBUG
    PgoManager::VerifyAddress(methodProfile);
    PgoManager::VerifyAddress(methodProfile + 1);
#endif

    methodProfile->HandleTable[methodSampleIndex] = (CORINFO_METHOD_HANDLE)pRecordedMD;
}
HCIMPLEND

// Helpers for scalable approximate counters
//
// Here threshold = 13 means we count accurately up to 2^13 = 8192 and
// then start counting probabilistically.
//
// See docs/design/features/ScalableApproximateCounting.md
//
HCIMPL1(void, JIT_CountProfile32, volatile LONG* pCounter)
{
    FCALL_CONTRACT;

    LONG count = *pCounter;
    LONG delta = 1;
    DWORD threshold = g_pConfig->TieredPGO_ScalableCountThreshold();

    if (count >= (LONG)(1 << threshold))
    {
        DWORD logCount;
        BitScanReverse(&logCount, count);

        delta = 1 << (logCount - (threshold - 1));
        const unsigned rand = HandleHistogramProfileRand();
        const bool update = (rand & (delta - 1)) == 0;
        if (!update)
        {
            return;
        }
    }

    InterlockedAdd(pCounter, delta);
}
HCIMPLEND

HCIMPL1(void, JIT_CountProfile64, volatile LONG64* pCounter)
{
    FCALL_CONTRACT;

    LONG64 count = *pCounter;
    LONG64 delta = 1;
    DWORD threshold = g_pConfig->TieredPGO_ScalableCountThreshold();

    if (count >= (LONG64)(1LL << threshold))
    {
        DWORD logCount;
        BitScanReverse64(&logCount, count);

        delta = 1LL << (logCount - (threshold - 1));
        const unsigned rand = HandleHistogramProfileRand();
        const bool update = (rand & (delta - 1)) == 0;
        if (!update)
        {
            return;
        }
    }

    InterlockedAdd64(pCounter, delta);
}
HCIMPLEND

//========================================================================
//
//      INTEROP HELPERS
//
//========================================================================

#ifdef HOST_64BIT

/**********************************************************************/
/* Fills out portions of an InlinedCallFrame for JIT64    */
/* The idea here is to allocate and initialize the frame to only once, */
/* regardless of how many PInvokes there are in the method            */
Thread * JIT_InitPInvokeFrame(InlinedCallFrame *pFrame)
{
    CONTRACTL
    {
        NOTHROW;
        GC_TRIGGERS;
    } CONTRACTL_END;

    Thread *pThread = GetThread();

    // The JIT messed up and is initializing a frame that is already live on the stack?!?!?!?!
    _ASSERTE(pFrame != pThread->GetFrame());

    pFrame->Init();
    pFrame->m_Next = pThread->GetFrame();

    return pThread;
}

#endif // HOST_64BIT

EXTERN_C void JIT_PInvokeBegin(InlinedCallFrame* pFrame);
EXTERN_C void JIT_PInvokeEnd(InlinedCallFrame* pFrame);

// Forward declaration
EXTERN_C void STDCALL ReversePInvokeBadTransition();

#ifndef FEATURE_EH_FUNCLETS
EXCEPTION_HANDLER_DECL(FastNExportExceptHandler);
#endif

// This is a slower version of the reverse PInvoke enter function.
NOINLINE static void JIT_ReversePInvokeEnterRare(ReversePInvokeFrame* frame, void* returnAddr, UMEntryThunkData* pUMEntryThunkData = NULL)
{
    _ASSERTE(frame != NULL);

    Thread* thread = GetThreadNULLOk();
    if (thread == NULL)
        CREATETHREAD_IF_NULL_FAILFAST(thread, W("Failed to setup new thread during reverse P/Invoke"));

    // Verify the current thread isn't in COOP mode.
    if (thread->PreemptiveGCDisabled())
        ReversePInvokeBadTransition();

    frame->currentThread = thread;

#ifdef PROFILING_SUPPORTED
        if (CORProfilerTrackTransitions())
        {
            ProfilerUnmanagedToManagedTransitionMD(frame->pMD, COR_PRF_TRANSITION_CALL);
        }
#endif

    thread->DisablePreemptiveGC();
#ifdef DEBUGGING_SUPPORTED
    // If the debugger is attached, we use this opportunity to see if
    // we're disabling preemptive GC on the way into the runtime from
    // unmanaged code. We end up here because
    // Increment/DecrementTraceCallCount() will bump
    // g_TrapReturningThreads for us.
    if (CORDebuggerTraceCall())
        g_pDebugInterface->TraceCall(pUMEntryThunkData ? (const BYTE*)pUMEntryThunkData->GetManagedTarget() : (const BYTE*)returnAddr);
#endif // DEBUGGING_SUPPORTED
}

NOINLINE static void JIT_ReversePInvokeEnterRare2(ReversePInvokeFrame* frame, void* returnAddr, UMEntryThunkData* pUMEntryThunkData = NULL)
{
    frame->currentThread->RareDisablePreemptiveGC();
#ifdef DEBUGGING_SUPPORTED
    // If the debugger is attached, we use this opportunity to see if
    // we're disabling preemptive GC on the way into the runtime from
    // unmanaged code. We end up here because
    // Increment/DecrementTraceCallCount() will bump
    // g_TrapReturningThreads for us.
    if (CORDebuggerTraceCall())
        g_pDebugInterface->TraceCall(pUMEntryThunkData ? (const BYTE*)pUMEntryThunkData->GetManagedTarget() : (const BYTE*)returnAddr);
#endif // DEBUGGING_SUPPORTED
}

// The following JIT_ReversePInvoke helpers are special.
// They handle setting up Reverse P/Invoke calls and transitioning back to unmanaged code.
// We may not have a managed thread set up in JIT_ReversePInvokeEnter, and the GC mode may be incorrect.
// On x86, SEH handlers are set up and torn down explicitly, so we avoid using dynamic contracts.
// This method uses the correct calling convention and argument layout manually, without relying on standard macros or contracts.
HCIMPL3_RAW(void, JIT_ReversePInvokeEnterTrackTransitions, ReversePInvokeFrame* frame, MethodDesc* pMD, UMEntryThunkData* pUMEntryThunkData)
{
    _ASSERTE(frame != NULL && pMD != NULL);
    _ASSERTE(!pMD->IsILStub() || pUMEntryThunkData != NULL);

    if (pUMEntryThunkData != NULL)
    {
        pMD = pUMEntryThunkData->GetMethod();
    }
    frame->pMD = pMD;

    Thread* thread = GetThreadNULLOk();

    // If a thread instance exists and is in the
    // correct GC mode attempt a quick transition.
    if (thread != NULL
        && !thread->PreemptiveGCDisabled())
    {
        frame->currentThread = thread;

#ifdef PROFILING_SUPPORTED
        if (CORProfilerTrackTransitions())
        {
            ProfilerUnmanagedToManagedTransitionMD(frame->pMD, COR_PRF_TRANSITION_CALL);
        }
#endif

        // Manually inline the fast path in Thread::DisablePreemptiveGC().
        thread->m_fPreemptiveGCDisabled.StoreWithoutBarrier(1);
        if (g_TrapReturningThreads != 0)
        {
            // If we're in an IL stub, we want to trace the address of the target method,
            // not the next instruction in the stub.
            JIT_ReversePInvokeEnterRare2(frame, _ReturnAddress(), pUMEntryThunkData);
        }
    }
    else
    {
        // If we're in an IL stub, we want to trace the address of the target method,
        // not the next instruction in the stub.
        JIT_ReversePInvokeEnterRare(frame, _ReturnAddress(), pUMEntryThunkData);
    }

#if defined(TARGET_X86) && defined(TARGET_WINDOWS)
#ifndef FEATURE_EH_FUNCLETS
    frame->record.m_pEntryFrame = frame->currentThread->GetFrame();
    frame->record.m_ExReg.Handler = (PEXCEPTION_ROUTINE)FastNExportExceptHandler;
    INSTALL_EXCEPTION_HANDLING_RECORD(&frame->record.m_ExReg);
#else
    frame->m_ExReg.Handler = (PEXCEPTION_ROUTINE)ProcessCLRException;
    INSTALL_SEH_RECORD(&frame->m_ExReg);
#endif
#endif
}
HCIMPLEND_RAW

HCIMPL1_RAW(void, JIT_ReversePInvokeEnter, ReversePInvokeFrame* frame)
{
    _ASSERTE(frame != NULL);

    Thread* thread = GetThreadNULLOk();

    // If a thread instance exists and is in the
    // correct GC mode attempt a quick transition.
    if (thread != NULL
        && !thread->PreemptiveGCDisabled())
    {
        frame->currentThread = thread;

        // Manually inline the fast path in Thread::DisablePreemptiveGC().
        thread->m_fPreemptiveGCDisabled.StoreWithoutBarrier(1);
        if (g_TrapReturningThreads != 0)
        {
            JIT_ReversePInvokeEnterRare2(frame, _ReturnAddress());
        }
    }
    else
    {
        JIT_ReversePInvokeEnterRare(frame, _ReturnAddress());
    }

#if defined(TARGET_X86) && defined(TARGET_WINDOWS)
#ifndef FEATURE_EH_FUNCLETS
    frame->record.m_pEntryFrame = frame->currentThread->GetFrame();
    frame->record.m_ExReg.Handler = (PEXCEPTION_ROUTINE)FastNExportExceptHandler;
    INSTALL_EXCEPTION_HANDLING_RECORD(&frame->record.m_ExReg);
#else
    frame->m_ExReg.Handler = (PEXCEPTION_ROUTINE)ProcessCLRException;
    INSTALL_SEH_RECORD(&frame->m_ExReg);
#endif
#endif
}
HCIMPLEND_RAW

HCIMPL1_RAW(void, JIT_ReversePInvokeExitTrackTransitions, ReversePInvokeFrame* frame)
{
    _ASSERTE(frame != NULL);
    _ASSERTE(frame->currentThread == GetThread());

    // Manually inline the fast path in Thread::EnablePreemptiveGC().
    // This is a trade off with GC suspend performance. We are opting
    // to make this exit faster.
    frame->currentThread->m_fPreemptiveGCDisabled.StoreWithoutBarrier(0);

#if defined(TARGET_X86) && defined(TARGET_WINDOWS)
#ifndef FEATURE_EH_FUNCLETS
    UNINSTALL_EXCEPTION_HANDLING_RECORD(&frame->record.m_ExReg);
#else
    UNINSTALL_SEH_RECORD(&frame->m_ExReg);
#endif
#endif

#ifdef PROFILING_SUPPORTED
    if (CORProfilerTrackTransitions())
    {
        ProfilerManagedToUnmanagedTransitionMD(frame->pMD, COR_PRF_TRANSITION_RETURN);
    }
#endif
}
HCIMPLEND_RAW

HCIMPL1_RAW(void, JIT_ReversePInvokeExit, ReversePInvokeFrame* frame)
{
    _ASSERTE(frame != NULL);
    _ASSERTE(frame->currentThread == GetThread());

    // Manually inline the fast path in Thread::EnablePreemptiveGC().
    // This is a trade off with GC suspend performance. We are opting
    // to make this exit faster.
    frame->currentThread->m_fPreemptiveGCDisabled.StoreWithoutBarrier(0);

#if defined(TARGET_X86) && defined(TARGET_WINDOWS)
#ifndef FEATURE_EH_FUNCLETS
    UNINSTALL_EXCEPTION_HANDLING_RECORD(&frame->record.m_ExReg);
#else
    UNINSTALL_SEH_RECORD(&frame->m_ExReg);
#endif
#endif
}
HCIMPLEND_RAW

// These two do take args but have a custom calling convention.
EXTERN_C void JIT_ValidateIndirectCall();
EXTERN_C void JIT_DispatchIndirectCall();

//========================================================================
//
//      JIT HELPERS INITIALIZATION
//
//========================================================================

// verify consistency of jithelpers.h and corinfo.h
enum __CorInfoHelpFunc {
#define JITHELPER(code, pfnHelper, sig) __##code,
#include "jithelpers.h"
};
#define JITHELPER(code, pfnHelper, sig) C_ASSERT((int)__##code == (int)code);
#include "jithelpers.h"

#ifdef _DEBUG
#define HELPERDEF(code, lpv, sig) { (LPVOID)(lpv), #code },
#else // !_DEBUG
#define HELPERDEF(code, lpv, sig) { (LPVOID)(lpv) },
#endif // !_DEBUG

// static helpers - constant array
const VMHELPDEF hlpFuncTable[CORINFO_HELP_COUNT] =
{
#define JITHELPER(code, pfnHelper, binderId) HELPERDEF(code, pfnHelper, binderId)
#define DYNAMICJITHELPER(code, pfnHelper, binderId) HELPERDEF(code, 1 + DYNAMIC_##code, binderId)
#include "jithelpers.h"
};

// dynamic helpers - filled in at runtime - See definition of DynamicCorInfoHelpFunc.
VMHELPDEF hlpDynamicFuncTable[DYNAMIC_CORINFO_HELP_COUNT] =
{
#define JITHELPER(code, pfnHelper, binderId)
#define DYNAMICJITHELPER(code, pfnHelper, binderId) HELPERDEF(DYNAMIC_ ## code, pfnHelper, binderId)
#include "jithelpers.h"
};

// dynamic helpers to Binder ID mapping - See definition of DynamicCorInfoHelpFunc.
static const BinderMethodID hlpDynamicToBinderMap[DYNAMIC_CORINFO_HELP_COUNT] =
{
#define JITHELPER(code, pfnHelper, binderId)
#define DYNAMICJITHELPER(code, pfnHelper, binderId) (pfnHelper != NULL) ? (BinderMethodID)METHOD__NIL : (BinderMethodID)binderId, // If pre-compiled code is provided for a jit helper, prefer that over the IL implementation
#include "jithelpers.h"
};

// Set the JIT helper function in the helper table
// Handles the case where the function does not reside in mscorwks.dll

void _SetJitHelperFunction(DynamicCorInfoHelpFunc ftnNum, void * pFunc)
{
    CONTRACTL
    {
        NOTHROW;
        GC_NOTRIGGER;
    }
    CONTRACTL_END;

    _ASSERTE(ftnNum < DYNAMIC_CORINFO_HELP_COUNT);

    LOG((LF_JIT, LL_INFO1000000, "Setting JIT dynamic helper %3d (%s) to %p\n",
        ftnNum, hlpDynamicFuncTable[ftnNum].name, pFunc));

    hlpDynamicFuncTable[ftnNum].pfnHelper = (void*)pFunc;
}

VMHELPDEF LoadDynamicJitHelper(DynamicCorInfoHelpFunc ftnNum, MethodDesc** methodDesc)
{
    STANDARD_VM_CONTRACT;

    _ASSERTE(ftnNum < DYNAMIC_CORINFO_HELP_COUNT);

    MethodDesc* pMD = NULL;
    void* helper = VolatileLoad(&hlpDynamicFuncTable[ftnNum].pfnHelper);
    if (helper == NULL)
    {
        BinderMethodID binderId = hlpDynamicToBinderMap[ftnNum];

        LOG((LF_JIT, LL_INFO1000000, "Loading JIT dynamic helper %3d (%s) to binderID %u\n",
            ftnNum, hlpDynamicFuncTable[ftnNum].name, binderId));

        if (binderId == METHOD__NIL)
            return {};

        pMD = CoreLibBinder::GetMethod(binderId);
        PCODE pFunc = pMD->GetMultiCallableAddrOfCode();
        InterlockedCompareExchangeT<void*>(&hlpDynamicFuncTable[ftnNum].pfnHelper, (void*)pFunc, nullptr);
    }

    // If the caller wants the MethodDesc, we may need to try and load it.
    if (methodDesc != NULL)
    {
        if (pMD == NULL)
        {
            BinderMethodID binderId = hlpDynamicToBinderMap[ftnNum];
            pMD = binderId != METHOD__NIL
                ? CoreLibBinder::GetMethod(binderId)
                : NULL;
        }
        *methodDesc = pMD;
    }

    return hlpDynamicFuncTable[ftnNum];
}

bool HasILBasedDynamicJitHelper(DynamicCorInfoHelpFunc ftnNum)
{
    STANDARD_VM_CONTRACT;

    _ASSERTE(ftnNum < DYNAMIC_CORINFO_HELP_COUNT);

    return (METHOD__NIL != hlpDynamicToBinderMap[ftnNum]);
}

bool IndirectionAllowedForJitHelper(CorInfoHelpFunc ftnNum)
{
    STANDARD_VM_CONTRACT;

    _ASSERTE(ftnNum < CORINFO_HELP_COUNT);

    if (
#define DYNAMICJITHELPER(code,fn,binderId)
#define JITHELPER(code,fn,binderId)
#define DYNAMICJITHELPER_NOINDIRECT(code,fn,binderId) (code == ftnNum) ||
#include "jithelpers.h"
        false)
    {
        return false;
    }

    return true;
}
