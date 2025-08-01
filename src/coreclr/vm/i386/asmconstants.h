// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// asmconstants.h -
//
// This header defines field offsets and constants used by assembly code
// Be sure to rebuild clr/src/vm/ceemain.cpp after changing this file, to
// ensure that the constants match the expected C/C++ values

//
// If you need to figure out a constant that has changed and is causing
// a compile-time assert, check out USE_COMPILE_TIME_CONSTANT_FINDER.
// TODO: put the constant finder in a common place so other platforms can use it.

#ifndef TARGET_X86
#error this file should only be used on an X86 platform
#endif

#include "../../inc/switches.h"

#ifndef ASMCONSTANTS_C_ASSERT
#define ASMCONSTANTS_C_ASSERT(cond)
#endif

#ifndef ASMCONSTANTS_RUNTIME_ASSERT
#define ASMCONSTANTS_RUNTIME_ASSERT(cond)
#endif

// Some constants are different in _DEBUG builds.  This macro factors out ifdefs from below.
#ifdef _DEBUG
#define DBG_FRE(dbg,fre) dbg
#else
#define DBG_FRE(dbg,fre) fre
#endif

#define FRAMETYPE_InlinedCallFrame 0x1
ASMCONSTANTS_C_ASSERT(FRAMETYPE_InlinedCallFrame == (int)FrameIdentifier::InlinedCallFrame)

#if defined(TARGET_X86) && !defined(UNIX_X86_ABI)
#define FRAMETYPE_TailCallFrame 0x2
ASMCONSTANTS_C_ASSERT(FRAMETYPE_TailCallFrame == (int)FrameIdentifier::TailCallFrame)
#endif

#define INITIAL_SUCCESS_COUNT               0x100

#define DynamicHelperFrameFlags_Default     0
#define DynamicHelperFrameFlags_ObjectArg   1
#define DynamicHelperFrameFlags_ObjectArg2  2

#define ThisPtrRetBufPrecodeData__Target      0x00
ASMCONSTANTS_C_ASSERT(ThisPtrRetBufPrecodeData__Target == offsetof(ThisPtrRetBufPrecodeData, Target));

// CONTEXT from pal.h
#define CONTEXT_Edi 0x9c
ASMCONSTANTS_C_ASSERT(CONTEXT_Edi == offsetof(CONTEXT,Edi))

#define CONTEXT_Esi 0xa0
ASMCONSTANTS_C_ASSERT(CONTEXT_Esi == offsetof(CONTEXT,Esi))

#define CONTEXT_Ebx 0xa4
ASMCONSTANTS_C_ASSERT(CONTEXT_Ebx == offsetof(CONTEXT,Ebx))

#define CONTEXT_Edx 0xa8
ASMCONSTANTS_C_ASSERT(CONTEXT_Edx == offsetof(CONTEXT,Edx))

#define CONTEXT_Eax 0xb0
ASMCONSTANTS_C_ASSERT(CONTEXT_Eax == offsetof(CONTEXT,Eax))

#define CONTEXT_Ebp 0xb4
ASMCONSTANTS_C_ASSERT(CONTEXT_Ebp == offsetof(CONTEXT,Ebp))

#define CONTEXT_Eip 0xb8
ASMCONSTANTS_C_ASSERT(CONTEXT_Eip == offsetof(CONTEXT,Eip))

#define CONTEXT_Esp 0xc4
ASMCONSTANTS_C_ASSERT(CONTEXT_Esp == offsetof(CONTEXT,Esp))

#ifndef FEATURE_EH_FUNCLETS
// EHContext from clr/src/vm/i386/cgencpu.h
#define EHContext_Eax 0x00
ASMCONSTANTS_C_ASSERT(EHContext_Eax == offsetof(EHContext,Eax))

#define EHContext_Ebx 0x04
ASMCONSTANTS_C_ASSERT(EHContext_Ebx == offsetof(EHContext,Ebx))

#define EHContext_Ecx 0x08
ASMCONSTANTS_C_ASSERT(EHContext_Ecx == offsetof(EHContext,Ecx))

#define EHContext_Edx 0x0c
ASMCONSTANTS_C_ASSERT(EHContext_Edx == offsetof(EHContext,Edx))

#define EHContext_Esi 0x10
ASMCONSTANTS_C_ASSERT(EHContext_Esi == offsetof(EHContext,Esi))

#define EHContext_Edi 0x14
ASMCONSTANTS_C_ASSERT(EHContext_Edi == offsetof(EHContext,Edi))

#define EHContext_Ebp 0x18
ASMCONSTANTS_C_ASSERT(EHContext_Ebp == offsetof(EHContext,Ebp))

#define EHContext_Esp 0x1c
ASMCONSTANTS_C_ASSERT(EHContext_Esp == offsetof(EHContext,Esp))

#define EHContext_Eip 0x20
ASMCONSTANTS_C_ASSERT(EHContext_Eip == offsetof(EHContext,Eip))
#endif // FEATURE_EH_FUNCLETS

#define VASigCookie__StubOffset 4
ASMCONSTANTS_C_ASSERT(VASigCookie__StubOffset == offsetof(VASigCookie, pPInvokeILStub))

#ifndef UNIX_X86_ABI
#define SIZEOF_TailCallFrame 32
ASMCONSTANTS_C_ASSERT(SIZEOF_TailCallFrame == sizeof(TailCallFrame))
#endif // !UNIX_X86_ABI

// ICodeManager::SHADOW_SP_IN_FILTER from clr/src/inc/eetwain.h
#define SHADOW_SP_IN_FILTER_ASM 0x1
ASMCONSTANTS_C_ASSERT(SHADOW_SP_IN_FILTER_ASM == ICodeManager::SHADOW_SP_IN_FILTER)



#define Thread_m_State      0x00
ASMCONSTANTS_C_ASSERT(Thread_m_State == offsetof(Thread, m_State))

#define Thread_m_fPreemptiveGCDisabled     0x04
ASMCONSTANTS_C_ASSERT(Thread_m_fPreemptiveGCDisabled == offsetof(Thread, m_fPreemptiveGCDisabled))

#define Thread_m_pFrame     0x08
ASMCONSTANTS_C_ASSERT(Thread_m_pFrame == offsetof(Thread, m_pFrame))


#ifdef FEATURE_HIJACK
#define TS_Hijacked_ASM 0x80
ASMCONSTANTS_C_ASSERT(Thread::TS_Hijacked == TS_Hijacked_ASM)
#endif

#define               OFFSETOF__RuntimeThreadLocals__ee_alloc_context 0
ASMCONSTANTS_C_ASSERT(OFFSETOF__RuntimeThreadLocals__ee_alloc_context == offsetof(RuntimeThreadLocals, alloc_context));

#ifdef TARGET_WINDOWS
#define               OFFSETOF__ee_alloc_context__alloc_ptr 0x8
#else
#define               OFFSETOF__ee_alloc_context__alloc_ptr 0x4
#endif
ASMCONSTANTS_C_ASSERT(OFFSETOF__ee_alloc_context__alloc_ptr == offsetof(ee_alloc_context, m_GCAllocContext) +
                                                               offsetof(gc_alloc_context, alloc_ptr));

#define               OFFSETOF__ee_alloc_context__combined_limit 0x0
ASMCONSTANTS_C_ASSERT(OFFSETOF__ee_alloc_context__combined_limit == offsetof(ee_alloc_context, m_CombinedLimit));


// from clr/src/vm/appdomain.hpp

// This is the offset from EBP at which the original CONTEXT is stored in one of the
// RedirectedHandledJITCase*_Stub functions.
#define REDIRECTSTUB_EBP_OFFSET_CONTEXT (-4)

#define MethodTable_m_wNumInterfaces    0x0E
ASMCONSTANTS_C_ASSERT(MethodTable_m_wNumInterfaces == offsetof(MethodTable, m_wNumInterfaces))

#define MethodTable_m_dwFlags           0x0
ASMCONSTANTS_C_ASSERT(MethodTable_m_dwFlags == offsetof(MethodTable, m_dwFlags))

#define MethodTable_m_pInterfaceMap     DBG_FRE(0x28, 0x24)
ASMCONSTANTS_C_ASSERT(MethodTable_m_pInterfaceMap == offsetof(MethodTable, m_pInterfaceMap))

#define SIZEOF_MethodTable              DBG_FRE(0x2C, 0x28)
ASMCONSTANTS_C_ASSERT(SIZEOF_MethodTable == sizeof(MethodTable))

#define SIZEOF_InterfaceInfo_t          0x4
ASMCONSTANTS_C_ASSERT(SIZEOF_InterfaceInfo_t == sizeof(InterfaceInfo_t))

#define               OFFSETOF__MethodTable__m_dwFlags              0x00
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_dwFlags
                    == offsetof(MethodTable, m_dwFlags));

#define               OFFSETOF__MethodTable__m_usComponentSize    0
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_usComponentSize == offsetof(MethodTable, m_dwFlags));

#define               OFFSETOF__MethodTable__m_uBaseSize    0x04
ASMCONSTANTS_C_ASSERT(OFFSETOF__MethodTable__m_uBaseSize == offsetof(MethodTable, m_BaseSize));

#define               OFFSETOF__Object__m_pEEType   0
ASMCONSTANTS_C_ASSERT(OFFSETOF__Object__m_pEEType == offsetof(Object, m_pMethTab));

#define               OFFSETOF__Array__m_Length     0x4
ASMCONSTANTS_C_ASSERT(OFFSETOF__Array__m_Length == offsetof(ArrayBase, m_NumComponents));

#define               MAX_STRING_LENGTH 0x3FFFFFDF
ASMCONSTANTS_C_ASSERT(MAX_STRING_LENGTH == CORINFO_String_MaxLength);

#define               STRING_COMPONENT_SIZE 2

#define               STRING_BASE_SIZE 0xE
ASMCONSTANTS_C_ASSERT(STRING_BASE_SIZE == OBJECT_BASESIZE + sizeof(DWORD) + sizeof(WCHAR));

#define               SZARRAY_BASE_SIZE 0xC
ASMCONSTANTS_C_ASSERT(SZARRAY_BASE_SIZE == OBJECT_BASESIZE + sizeof(DWORD));

#ifdef FEATURE_COMINTEROP

#ifndef FEATURE_EH_FUNCLETS
#define SIZEOF_FrameHandlerExRecord 0x0c
#define OFFSETOF__FrameHandlerExRecord__m_ExReg__Next 0
#define OFFSETOF__FrameHandlerExRecord__m_ExReg__Handler 4
#define OFFSETOF__FrameHandlerExRecord__m_pEntryFrame 8
ASMCONSTANTS_C_ASSERT(SIZEOF_FrameHandlerExRecord == sizeof(FrameHandlerExRecord))
ASMCONSTANTS_C_ASSERT(OFFSETOF__FrameHandlerExRecord__m_ExReg__Next == offsetof(FrameHandlerExRecord, m_ExReg) + offsetof(EXCEPTION_REGISTRATION_RECORD, Next))
ASMCONSTANTS_C_ASSERT(OFFSETOF__FrameHandlerExRecord__m_ExReg__Handler == offsetof(FrameHandlerExRecord, m_ExReg) + offsetof(EXCEPTION_REGISTRATION_RECORD, Handler))
ASMCONSTANTS_C_ASSERT(OFFSETOF__FrameHandlerExRecord__m_pEntryFrame == offsetof(FrameHandlerExRecord, m_pEntryFrame))
#endif

#ifdef _DEBUG
#ifndef STACK_OVERWRITE_BARRIER_SIZE
#define STACK_OVERWRITE_BARRIER_SIZE 20
#endif
#ifndef STACK_OVERWRITE_BARRIER_VALUE
#define STACK_OVERWRITE_BARRIER_VALUE 0xabcdefab
#endif

#endif

#define CLRToCOMCallMethodDesc__m_pCLRToCOMCallInfo DBG_FRE(0x20, 0xC)
ASMCONSTANTS_C_ASSERT(CLRToCOMCallMethodDesc__m_pCLRToCOMCallInfo == offsetof(CLRToCOMCallMethodDesc, m_pCLRToCOMCallInfo))

#define CLRToCOMCallInfo__m_cbStackPop 0x0e
ASMCONSTANTS_C_ASSERT(CLRToCOMCallInfo__m_cbStackPop == offsetof(CLRToCOMCallInfo, m_cbStackPop))

#define COMMETHOD_PREPAD_ASM  8
ASMCONSTANTS_C_ASSERT(COMMETHOD_PREPAD_ASM == COMMETHOD_PREPAD)

#define OFFSETOF__UnmanagedToManagedFrame__m_pvDatum 8
ASMCONSTANTS_C_ASSERT(OFFSETOF__UnmanagedToManagedFrame__m_pvDatum == offsetof(UnmanagedToManagedFrame, m_pvDatum))

#endif // FEATURE_COMINTEROP

#define ASM__VTABLE_SLOTS_PER_CHUNK 8
ASMCONSTANTS_C_ASSERT(ASM__VTABLE_SLOTS_PER_CHUNK == VTABLE_SLOTS_PER_CHUNK)

#define ASM__VTABLE_SLOTS_PER_CHUNK_LOG2 3
ASMCONSTANTS_C_ASSERT(ASM__VTABLE_SLOTS_PER_CHUNK_LOG2 == VTABLE_SLOTS_PER_CHUNK_LOG2)

#define JIT_TailCall_StackOffsetToFlags       0x08

#define CallDescrData__pSrc                0x00
#define CallDescrData__numStackSlots       0x04
#define CallDescrData__pArgumentRegisters  0x08
#define CallDescrData__fpReturnSize        0x0C
#define CallDescrData__pTarget             0x10
#ifndef __GNUC__
#define CallDescrData__returnValue         0x18
#else
#define CallDescrData__returnValue         0x14
#endif

ASMCONSTANTS_C_ASSERT(CallDescrData__pSrc                 == offsetof(CallDescrData, pSrc))
ASMCONSTANTS_C_ASSERT(CallDescrData__numStackSlots        == offsetof(CallDescrData, numStackSlots))
ASMCONSTANTS_C_ASSERT(CallDescrData__pArgumentRegisters   == offsetof(CallDescrData, pArgumentRegisters))
ASMCONSTANTS_C_ASSERT(CallDescrData__fpReturnSize         == offsetof(CallDescrData, fpReturnSize))
ASMCONSTANTS_C_ASSERT(CallDescrData__pTarget              == offsetof(CallDescrData, pTarget))
ASMCONSTANTS_C_ASSERT(CallDescrData__returnValue          == offsetof(CallDescrData, returnValue))

// For JIT_PInvokeBegin and JIT_PInvokeEnd helpers
#define               Frame__m_Next 0x04
ASMCONSTANTS_C_ASSERT(Frame__m_Next == offsetof(Frame, m_Next));

#define               InlinedCallFrame__m_Datum 0x08
ASMCONSTANTS_C_ASSERT(InlinedCallFrame__m_Datum == offsetof(InlinedCallFrame, m_Datum));

#define               InlinedCallFrame__m_pCallSiteSP 0x0C
ASMCONSTANTS_C_ASSERT(InlinedCallFrame__m_pCallSiteSP == offsetof(InlinedCallFrame, m_pCallSiteSP));

#define               InlinedCallFrame__m_pCallerReturnAddress 0x10
ASMCONSTANTS_C_ASSERT(InlinedCallFrame__m_pCallerReturnAddress == offsetof(InlinedCallFrame, m_pCallerReturnAddress));

#define               InlinedCallFrame__m_pCalleeSavedFP 0x14
ASMCONSTANTS_C_ASSERT(InlinedCallFrame__m_pCalleeSavedFP == offsetof(InlinedCallFrame, m_pCalleeSavedFP));

// ResolveCacheElem from src/vm/virtualcallstub.h
#define ResolveCacheElem__pMT               0x00
#define ResolveCacheElem__token             0x04
#define ResolveCacheElem__target            0x08
#define ResolveCacheElem__pNext             0x0C

ASMCONSTANTS_C_ASSERT(ResolveCacheElem__pMT     == offsetof(ResolveCacheElem, pMT));
ASMCONSTANTS_C_ASSERT(ResolveCacheElem__token   == offsetof(ResolveCacheElem, token));
ASMCONSTANTS_C_ASSERT(ResolveCacheElem__target  == offsetof(ResolveCacheElem, target));
ASMCONSTANTS_C_ASSERT(ResolveCacheElem__pNext   == offsetof(ResolveCacheElem, pNext));

#define ASM__CALL_STUB_CACHE_INITIAL_SUCCESS_COUNT (0x100)
ASMCONSTANTS_C_ASSERT(ASM__CALL_STUB_CACHE_INITIAL_SUCCESS_COUNT == CALL_STUB_CACHE_INITIAL_SUCCESS_COUNT)

#define FixupPrecodeData__Target 0x00
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__Target            == offsetof(FixupPrecodeData, Target))
#define FixupPrecodeData__MethodDesc 0x04
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__MethodDesc        == offsetof(FixupPrecodeData, MethodDesc))
#define FixupPrecodeData__PrecodeFixupThunk 0x08
ASMCONSTANTS_C_ASSERT(FixupPrecodeData__PrecodeFixupThunk == offsetof(FixupPrecodeData, PrecodeFixupThunk))

#define StubPrecodeData__Target 0x04
ASMCONSTANTS_C_ASSERT(StubPrecodeData__Target            == offsetof(StubPrecodeData, Target))
#define StubPrecodeData__SecretParam 0x00
ASMCONSTANTS_C_ASSERT(StubPrecodeData__SecretParam        == offsetof(StubPrecodeData, SecretParam))

#define CallCountingStubData__RemainingCallCountCell 0x00
ASMCONSTANTS_C_ASSERT(CallCountingStubData__RemainingCallCountCell == offsetof(CallCountingStubData, RemainingCallCountCell))

#define CallCountingStubData__TargetForMethod 0x04
ASMCONSTANTS_C_ASSERT(CallCountingStubData__TargetForMethod == offsetof(CallCountingStubData, TargetForMethod))

#define CallCountingStubData__TargetForThresholdReached 0x08
ASMCONSTANTS_C_ASSERT(CallCountingStubData__TargetForThresholdReached == offsetof(CallCountingStubData, TargetForThresholdReached))

#undef ASMCONSTANTS_C_ASSERT
#undef ASMCONSTANTS_RUNTIME_ASSERT

// #define USE_COMPILE_TIME_CONSTANT_FINDER // Uncomment this line to use the constant finder
#if defined(__cplusplus) && defined(USE_COMPILE_TIME_CONSTANT_FINDER)
// This class causes the compiler to emit an error with the constant we're interested in
// in the error message. This is useful if a size or offset changes. To use, comment out
// the compile-time assert that is firing, enable the constant finder, add the appropriate
// constant to find to BogusFunction(), and build.
//
// Here's a sample compiler error:
// d:\dd\clr\src\ndp\clr\src\vm\i386\asmconstants.h(326) : error C2248: 'FindCompileTimeConstant<N>::FindCompileTimeConstant' : cannot access private member declared in class 'FindCompileTimeConstant<N>'
//         with
//         [
//             N=1520
//         ]
//         d:\dd\clr\src\ndp\clr\src\vm\i386\asmconstants.h(321) : see declaration of 'FindCompileTimeConstant<N>::FindCompileTimeConstant'
//         with
//         [
//             N=1520
//         ]
template<size_t N>
class FindCompileTimeConstant
{
private:
	FindCompileTimeConstant();
};

void BogusFunction()
{
	// Sample usage to generate the error
	FindCompileTimeConstant<offsetof(Thread, m_ExceptionState)> bogus_variable;
}
#endif // defined(__cplusplus) && defined(USE_COMPILE_TIME_CONSTANT_FINDER)
