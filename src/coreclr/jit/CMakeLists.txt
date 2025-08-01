set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories("./jitstd")
include_directories("../inc")

# gcc 10 and lower versions are too sensitive to bit fields width and warn from core compiler.
# Since there is no better / specific suppression available for these core warnings, we disable
# warn-as-error (-Werror) for JIT in this case. This issue has been fixed in gcc 11.
# See https://github.com/dotnet/runtime/issues/33541
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
  add_compile_options(-Wno-error)
endif()

if (MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/permissive->)
endif()

function(create_standalone_jit)

  set(oneValueArgs TARGET OS ARCH)
  cmake_parse_arguments(TARGETDETAILS "" "${oneValueArgs}" "" ${ARGN})

  if(TARGETDETAILS_OS STREQUAL "unix_osx" OR TARGETDETAILS_OS STREQUAL "unix_anyos")
    set(JIT_ARCH_LINK_LIBRARIES gcinfo_unix_${TARGETDETAILS_ARCH})
  else()
    set(JIT_ARCH_LINK_LIBRARIES gcinfo_${TARGETDETAILS_OS}_${TARGETDETAILS_ARCH})
  endif()

  if(TARGETDETAILS_ARCH STREQUAL "x64")
    set(JIT_ARCH_SOURCES ${JIT_AMD64_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_AMD64_HEADERS})
  elseif((TARGETDETAILS_ARCH STREQUAL "arm") OR (TARGETDETAILS_ARCH STREQUAL "armel"))
    set(JIT_ARCH_SOURCES ${JIT_ARM_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_ARM_HEADERS})
  elseif((TARGETDETAILS_ARCH STREQUAL "armv6") OR (TARGETDETAILS_ARCH STREQUAL "armv6l"))
    set(JIT_ARCH_SOURCES ${JIT_ARMV6_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_ARMV6_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "x86")
    set(JIT_ARCH_SOURCES ${JIT_I386_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_I386_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "arm64")
    set(JIT_ARCH_SOURCES ${JIT_ARM64_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_ARM64_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "s390x")
    set(JIT_ARCH_SOURCES ${JIT_S390X_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_S390X_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "ppc64le")
    set(JIT_ARCH_SOURCES ${JIT_POWERPC64_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_POWERPC64_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "loongarch64")
    set(JIT_ARCH_SOURCES ${JIT_LOONGARCH64_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_LOONGARCH64_HEADERS})
  elseif(TARGETDETAILS_ARCH STREQUAL "riscv64")
    set(JIT_ARCH_SOURCES ${JIT_RISCV64_SOURCES})
    set(JIT_ARCH_HEADERS ${JIT_RISCV64_HEADERS})
  else()
    clr_unknown_arch()
  endif()

  add_jit(${TARGETDETAILS_TARGET})

  set_target_definitions_to_custom_os_and_arch(${ARGN})
  set_target_properties(${TARGETDETAILS_TARGET} PROPERTIES IGNORE_FEATURE_STATICALLY_LINKED TRUE)

  target_compile_definitions(${TARGETDETAILS_TARGET} PRIVATE FEATURE_NO_HOST)
  target_compile_definitions(${TARGETDETAILS_TARGET} PRIVATE SELF_NO_HOST)

  if ((TARGETDETAILS_ARCH STREQUAL "x64") OR (TARGETDETAILS_ARCH STREQUAL "arm64") OR (TARGETDETAILS_ARCH STREQUAL "x86"))
    target_compile_definitions(${TARGETDETAILS_TARGET} PRIVATE FEATURE_SIMD)
    target_compile_definitions(${TARGETDETAILS_TARGET} PRIVATE FEATURE_HW_INTRINSICS)
    target_compile_definitions(${TARGETDETAILS_TARGET} PRIVATE FEATURE_MASKED_HW_INTRINSICS)
  endif ()
endfunction()

if (CLR_CMAKE_TARGET_ARCH_AMD64 OR CLR_CMAKE_TARGET_ARCH_ARM64 OR (CLR_CMAKE_TARGET_ARCH_I386 AND NOT CLR_CMAKE_HOST_UNIX))
  add_compile_definitions($<$<NOT:$<BOOL:$<TARGET_PROPERTY:IGNORE_DEFAULT_TARGET_ARCH>>>:FEATURE_SIMD>)
  add_compile_definitions($<$<NOT:$<BOOL:$<TARGET_PROPERTY:IGNORE_DEFAULT_TARGET_ARCH>>>:FEATURE_HW_INTRINSICS>)
  add_compile_definitions($<$<NOT:$<BOOL:$<TARGET_PROPERTY:IGNORE_DEFAULT_TARGET_ARCH>>>:FEATURE_MASKED_HW_INTRINSICS>)
endif ()

# JIT_BUILD disables certain PAL_TRY debugging features
add_definitions(-DJIT_BUILD)

if(CLR_CMAKE_TARGET_WIN32)
  set(JIT_RESOURCES Native.rc)
endif(CLR_CMAKE_TARGET_WIN32)

set( JIT_SOURCES
  abi.cpp
  alloc.cpp
  assertionprop.cpp
  async.cpp
  bitset.cpp
  block.cpp
  buildstring.cpp
  codegencommon.cpp
  codegenlinear.cpp
  compiler.cpp
  copyprop.cpp
  debuginfo.cpp
  disasm.cpp
  earlyprop.cpp
  ee_il_dll.cpp
  eeinterface.cpp
  emit.cpp
  error.cpp
  fgbasic.cpp
  fgdiagnostic.cpp
  fgehopt.cpp
  fgflow.cpp
  fginline.cpp
  fgopt.cpp
  fgprofile.cpp
  fgprofilesynthesis.cpp
  fgstmt.cpp
  flowgraph.cpp
  forwardsub.cpp
  gcdecode.cpp
  gcencode.cpp
  gcinfo.cpp
  gentree.cpp
  gschecks.cpp
  hashbv.cpp
  helperexpansion.cpp
  hostallocator.cpp
  hwintrinsic.cpp
  ifconversion.cpp
  importer.cpp
  importercalls.cpp
  importervectorization.cpp
  indirectcalltransformer.cpp
  inductionvariableopts.cpp
  inline.cpp
  inlinepolicy.cpp
  instr.cpp
  jitconfig.cpp
  jiteh.cpp
  jithashtable.cpp
  jitmetadata.cpp
  layout.cpp
  lclmorph.cpp
  lclvars.cpp
  likelyclass.cpp
  lir.cpp
  liveness.cpp
  loopcloning.cpp
  lower.cpp
  lsra.cpp
  lsrabuild.cpp
  morph.cpp
  morphblock.cpp
  objectalloc.cpp
  optcse.cpp
  optimizebools.cpp
  optimizemaskconversions.cpp
  optimizer.cpp
  patchpoint.cpp
  phase.cpp
  promotion.cpp
  promotiondecomposition.cpp
  promotionliveness.cpp
  rangecheck.cpp
  rangecheckcloning.cpp
  rationalize.cpp
  redundantbranchopts.cpp
  regalloc.cpp
  regMaskTPOps.cpp
  regset.cpp
  scev.cpp
  scopeinfo.cpp
  segmentlist.cpp
  sideeffects.cpp
  sm.cpp
  smdata.cpp
  smweights.cpp
  ssabuilder.cpp
  ssarenamestate.cpp
  stacklevelsetter.cpp
  switchrecognition.cpp
  treelifeupdater.cpp
  unwind.cpp
  utils.cpp
  valuenum.cpp
)

if (CLR_CMAKE_TARGET_WIN32)
  # Append clrjit.natvis file
  list (APPEND JIT_SOURCES
    clrjit.natvis)
endif(CLR_CMAKE_TARGET_WIN32)

# Define all the architecture-specific source files

set( JIT_AMD64_SOURCES
  codegenxarch.cpp
  emitxarch.cpp
  lowerxarch.cpp
  lsraxarch.cpp
  simd.cpp
  simdcodegenxarch.cpp
  targetamd64.cpp
  unwindamd64.cpp
  hwintrinsicxarch.cpp
  hwintrinsiccodegenxarch.cpp
)

set( JIT_ARM_SOURCES
  codegenarmarch.cpp
  codegenarm.cpp
  decomposelongs.cpp
  emitarm.cpp
  lowerarmarch.cpp
  lsraarmarch.cpp
  lsraarm.cpp
  targetarm.cpp
  unwindarmarch.cpp
)

set( JIT_I386_SOURCES
  codegenxarch.cpp
  decomposelongs.cpp
  emitxarch.cpp
  lowerxarch.cpp
  lsraxarch.cpp
  simd.cpp
  simdcodegenxarch.cpp
  targetx86.cpp
  unwindx86.cpp
  hwintrinsicxarch.cpp
  hwintrinsiccodegenxarch.cpp
)

set( JIT_ARM64_SOURCES
  codegenarmarch.cpp
  codegenarm64.cpp
  codegenarm64test.cpp
  emitarm64.cpp
  emitarm64sve.cpp
  lowerarmarch.cpp
  lsraarmarch.cpp
  lsraarm64.cpp
  simd.cpp
  targetarm64.cpp
  unwindarmarch.cpp
  unwindarm64.cpp
  hwintrinsicarm64.cpp
  hwintrinsiccodegenarm64.cpp
)

set( JIT_ARMV6_SOURCES
  # Not supported as JIT target
)

set( JIT_S390X_SOURCES
  # Not supported as JIT target
)

set( JIT_POWERPC64_SOURCES
  # Not supported as JIT target
)

set( JIT_LOONGARCH64_SOURCES
  codegenloongarch64.cpp
  emitloongarch64.cpp
  lowerloongarch64.cpp
  lsraloongarch64.cpp
  targetloongarch64.cpp
  unwindloongarch64.cpp
)

set( JIT_RISCV64_SOURCES
  codegenriscv64.cpp
  emitriscv64.cpp
  lowerriscv64.cpp
  lsrariscv64.cpp
  targetriscv64.cpp
  unwindriscv64.cpp
)

# We include the headers here for better experience in IDEs.
set( JIT_HEADERS
  ../inc/corinfo.h
  ../inc/corjit.h
  ../inc/corjitflags.h
  ../inc/corjithost.h
  _typeinfo.h
  abi.h
  alloc.h
  arraystack.h
  async.h
  bitset.h
  layout.h
  bitsetasshortlong.h
  bitsetasuint64.h
  bitsetasuint64inclass.h
  bitsetops.h
  bitvec.h
  block.h
  codegen.h
  codegeninterface.h
  compiler.h
  compiler.hpp
  compilerbitsettraits.h
  compilerbitsettraits.hpp
  compmemkind.h
  compphases.h
  handlekinds.h
  dataflow.h
  debuginfo.h
  decomposelongs.h
  disasm.h
  emit.h
  emitdef.h
  emitfmts.h
  emitinl.h
  emitjmps.h
  emitpub.h
  error.h
  fgprofilesynthesis.h
  gentree.h
  gentreeopsdef.h
  gtlist.h
  gtstructs.h
  hashbv.h
  host.h
  hostallocator.h
  hwintrinsic.h
  ICorJitInfo_names_generated.h
  ICorJitInfo_wrapper_generated.hpp
  inline.h
  inlinepolicy.h
  instr.h
  instrs.h
  jit.h
  jitconfig.h
  jitconfigvalues.h
  jitee.h
  jiteh.h
  jitexpandarray.h
  jitgcinfo.h
  jithashtable.h
  jitmetadata.h
  jitmetadatalist.h
  jitpch.h
  jitstd.h
  lir.h
  loopcloning.h
  loopcloningopts.h
  lower.h
  lsra_reftypes.h
  lsra_stats.h
  lsra_score.h
  lsra.h
  namedintrinsiclist.h
  objectalloc.h
  opcode.h
  optcse.h
  phase.h
  priorityqueue.h
  promotion.h
  rangecheck.h
  rangecheckcloning.h
  rationalize.h
  regalloc.h
  register.h
  regset.h
  scev.h
  segmentlist.h
  sideeffects.h
  simd.h
  sm.h
  smallhash.h
  smcommon.h
  smopenum.h
  ssabuilder.h
  ssaconfig.h
  ssarenamestate.h
  stacklevelsetter.h
  target.h
  targetx86.h
  targetamd64.h
  targetarm.h
  targetarm64.h
  treelifeupdater.h
  typelist.h
  unwind.h
  utils.h
  valuenum.h
  valuenumfuncs.h
  valuenumtype.h
  varset.h
  vartype.h
  vartypesdef.h
)

# Arch specific headers
set( JIT_AMD64_HEADERS
  emitfmtsxarch.h
  emitxarch.h
  hwintrinsiclistxarch.h
  hwintrinsic.h
  instrsxarch.h
)

set( JIT_I386_HEADERS ${JIT_AMD64_HEADERS} )

set( JIT_ARM64_HEADERS
    emitarm64.h
    emitfmtsarm64.h
    emitfmtsarm64sve.h
    hwintrinsiclistarm64.h
    hwintrinsiclistarm64sve.h
    instrsarm64.h
    instrsarm64sve.h
    registerarm64.h
)

set( JIT_ARM_HEADERS
  emitarm.h
  emitfmtsarm.h
  instrsarm.h
  registerarm.h
)

set ( JIT_ARMV6_HEADERS
  # Not supported as JIT target
)

set ( JIT_S390X_HEADERS
  # Not supported as JIT target
)

set ( JIT_POWERPC64_HEADERS
  # Not supported as JIT target
)

set( JIT_LOONGARCH64_HEADERS
    emitloongarch64.h
    emitfmtsloongarch64.h
    instrsloongarch64.h
    registerloongarch64.h
)

set( JIT_RISCV64_HEADERS
    emitriscv64.h
    emitfmtsriscv64.h
    instrsriscv64.h
    registerriscv64.h
)

convert_to_absolute_path(JIT_SOURCES ${JIT_SOURCES})
convert_to_absolute_path(JIT_HEADERS ${JIT_HEADERS})
convert_to_absolute_path(JIT_RESOURCES ${JIT_RESOURCES})

# Also convert the per-architecture sources to absolute paths, if the subdirs want to use them.

convert_to_absolute_path(JIT_AMD64_SOURCES ${JIT_AMD64_SOURCES})
convert_to_absolute_path(JIT_AMD64_HEADERS ${JIT_AMD64_HEADERS})
convert_to_absolute_path(JIT_ARM_SOURCES ${JIT_ARM_SOURCES})
convert_to_absolute_path(JIT_ARM_HEADERS ${JIT_ARM_HEADERS})
convert_to_absolute_path(JIT_I386_SOURCES ${JIT_I386_SOURCES})
convert_to_absolute_path(JIT_I386_HEADERS ${JIT_I386_HEADERS})
convert_to_absolute_path(JIT_ARM64_SOURCES ${JIT_ARM64_SOURCES})
convert_to_absolute_path(JIT_ARM64_HEADERS ${JIT_ARM64_HEADERS})
convert_to_absolute_path(JIT_ARMV6_SOURCES ${JIT_ARMV6_SOURCES})
convert_to_absolute_path(JIT_ARMV6_HEADERS ${JIT_ARMV6_HEADERS})
convert_to_absolute_path(JIT_S390X_SOURCES ${JIT_S390X_SOURCES})
convert_to_absolute_path(JIT_S390X_HEADERS ${JIT_S390X_HEADERS})
convert_to_absolute_path(JIT_LOONGARCH64_SOURCES ${JIT_LOONGARCH64_SOURCES})
convert_to_absolute_path(JIT_LOONGARCH64_HEADERS ${JIT_LOONGARCH64_HEADERS})
convert_to_absolute_path(JIT_RISCV64_SOURCES ${JIT_RISCV64_SOURCES})
convert_to_absolute_path(JIT_RISCV64_HEADERS ${JIT_RISCV64_HEADERS})

if(CLR_CMAKE_TARGET_ARCH_AMD64)
  set(JIT_ARCH_SOURCES ${JIT_AMD64_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_AMD64_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_ARM)
  set(JIT_ARCH_SOURCES ${JIT_ARM_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_ARM_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_ARMV6)
  set(JIT_ARCH_SOURCES ${JIT_ARMV6_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_ARMV6_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_I386)
  set(JIT_ARCH_SOURCES ${JIT_I386_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_I386_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_ARM64)
  set(JIT_ARCH_SOURCES ${JIT_ARM64_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_ARM64_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_S390X)
  set(JIT_ARCH_SOURCES ${JIT_S390X_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_S390X_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_POWERPC64)
  set(JIT_ARCH_SOURCES ${JIT_POWERPC64_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_POWERPC64_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_LOONGARCH64)
  set(JIT_ARCH_SOURCES ${JIT_LOONGARCH64_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_LOONGARCH64_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_RISCV64)
  set(JIT_ARCH_SOURCES ${JIT_RISCV64_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_RISCV64_HEADERS})
elseif(CLR_CMAKE_TARGET_ARCH_WASM)
  set(JIT_ARCH_SOURCES ${JIT_WASM32_SOURCES})
  set(JIT_ARCH_HEADERS ${JIT_WASM32_HEADERS})
else()
  clr_unknown_arch()
endif()


set(JIT_DLL_MAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/dllmain.cpp)

if(CLR_CMAKE_HOST_WIN32)
  set(CLRJIT_EXPORTS ${CMAKE_CURRENT_LIST_DIR}/ClrJit.exports)
  set(JIT_EXPORTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/ClrJit.exports.def)
  preprocess_file (${CLRJIT_EXPORTS} ${JIT_EXPORTS_FILE})

  set(JIT_DEF_FILE ${JIT_EXPORTS_FILE})
else()
  set(CLRJIT_EXPORTS ${CMAKE_CURRENT_LIST_DIR}/ClrJit.PAL.exports)

  set(JIT_EXPORTS_FILE ${CMAKE_CURRENT_BINARY_DIR}/clrjit.exports)
  generate_exports_file(${CLRJIT_EXPORTS} ${JIT_EXPORTS_FILE})

  if(CLR_CMAKE_TARGET_LINUX OR CLR_CMAKE_TARGET_FREEBSD OR CLR_CMAKE_TARGET_NETBSD OR CLR_CMAKE_TARGET_SUNOS)
    # This is required to force using our own PAL, not one that we are loaded with.
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Xlinker -Bsymbolic")
  endif()

  set_exports_linker_option(${JIT_EXPORTS_FILE})
  set(JIT_EXPORTS_LINKER_OPTION ${EXPORTS_LINKER_OPTION})
endif()

add_custom_target(jit_exports DEPENDS ${JIT_EXPORTS_FILE})

set(JIT_LINK_LIBRARIES
   utilcodestaticnohost
   minipal
)

set(JIT_ARCH_LINK_LIBRARIES
   gcinfo
)

if(CLR_CMAKE_HOST_UNIX)
    list(APPEND JIT_LINK_LIBRARIES
       mscorrc
       coreclrpal
       coreclrminipal
    )
else()
    list(APPEND JIT_LINK_LIBRARIES
       ${STATIC_MT_CRT_LIB}
       ${STATIC_MT_VCRT_LIB}
       kernel32.lib
       advapi32.lib
       ole32.lib
       oleaut32.lib
       uuid.lib
       user32.lib
       version.lib
       shlwapi.lib
       bcrypt.lib
       crypt32.lib
       RuntimeObject.lib
       coreclrminipal
    )
endif(CLR_CMAKE_HOST_UNIX)

if (CLR_CMAKE_HOST_ANDROID)
    list(APPEND JIT_LINK_LIBRARIES log)
endif()

# Shared function for generating JIT
function(add_jit jitName)

    set_source_files_properties(${JIT_EXPORTS_FILE} PROPERTIES GENERATED TRUE)

    if (CLR_CMAKE_TARGET_WIN32)
      # If generating for Visual Studio then include headers for a better
      # IDE experience.
      add_library_clr(${jitName}
          SHARED
          ${JIT_SOURCES}
          ${JIT_ARCH_SOURCES}
          ${JIT_HEADERS}
          ${JIT_ARCH_HEADERS}
          ${JIT_RESOURCES}
          ${JIT_DEF_FILE}
          ${JIT_DLL_MAIN_FILE}
      )
    else()
      add_library_clr(${jitName}
          SHARED
          ${JIT_SOURCES}
          ${JIT_ARCH_SOURCES}
          ${JIT_RESOURCES}
          ${JIT_DEF_FILE}
          ${JIT_DLL_MAIN_FILE}
      )
    endif(CLR_CMAKE_TARGET_WIN32)

    if(CLR_CMAKE_TARGET_WIN32)
        target_compile_definitions(${jitName} PRIVATE FX_VER_INTERNALNAME_STR=${jitName}.dll)
    endif(CLR_CMAKE_TARGET_WIN32)

    target_include_directories(${jitName} PRIVATE ${JIT_SOURCE_DIR})
    target_precompile_headers(${jitName} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:jitpch.h>")

    add_dependencies(${jitName} jit_exports)

    set_property(TARGET ${jitName} APPEND_STRING PROPERTY LINK_FLAGS ${JIT_EXPORTS_LINKER_OPTION})
    set_property(TARGET ${jitName} APPEND_STRING PROPERTY LINK_DEPENDS ${JIT_EXPORTS_FILE})

    target_link_libraries(${jitName}
        PRIVATE
        ${JIT_LINK_LIBRARIES}
        ${JIT_ARCH_LINK_LIBRARIES}
    )

    if (CLR_CMAKE_HOST_WIN32)
        link_natvis_sources_for_target(${jitName} PRIVATE clrjit.natvis)
    endif()

    # add the install targets
    install_clr(TARGETS ${jitName} DESTINATIONS . COMPONENT alljits)
endfunction()

set(JIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Creates a static library "clrjit_static" to link into the VM.
add_subdirectory(static)

if (CLR_CMAKE_TARGET_APPLE)
  set(TARGET_OS_NAME unix_osx)
elseif (CLR_CMAKE_TARGET_UNIX)
  set(TARGET_OS_NAME unix)
else()
  set(TARGET_OS_NAME win)
endif()

create_standalone_jit(TARGET clrjit OS ${TARGET_OS_NAME} ARCH ${ARCH_TARGET_NAME})
install_clr(TARGETS clrjit DESTINATIONS . sharedFramework COMPONENT jit)

if (NOT CLR_CROSS_COMPONENTS_BUILD)
    # Install the clrjit into the sharedframework dir only when we're not doing a cross-components build.
    # We never want to ship the cross-component JIT (it would never be used).
    install_clr(TARGETS clrjit DESTINATIONS sharedFramework COMPONENT alljits)
endif()

# Enable profile guided optimization
add_pgo(clrjit)

if (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)
  create_standalone_jit(TARGET clrjit_universal_arm64_${ARCH_HOST_NAME} OS universal ARCH arm64)
  create_standalone_jit(TARGET clrjit_unix_x64_${ARCH_HOST_NAME} OS unix_anyos ARCH x64)
  create_standalone_jit(TARGET clrjit_win_x64_${ARCH_HOST_NAME} OS win ARCH x64)
  if (CLR_CMAKE_BUILD_COMMUNITY_ALTJITS EQUAL 1)
    create_standalone_jit(TARGET clrjit_unix_loongarch64_${ARCH_HOST_NAME} OS unix ARCH loongarch64)
    create_standalone_jit(TARGET clrjit_unix_riscv64_${ARCH_HOST_NAME} OS unix ARCH riscv64)
  endif(CLR_CMAKE_BUILD_COMMUNITY_ALTJITS EQUAL 1)
endif (CLR_CMAKE_TARGET_ARCH_ARM64 OR CLR_CMAKE_TARGET_ARCH_AMD64)

if (CLR_CMAKE_TARGET_ARCH_LOONGARCH64)
  create_standalone_jit(TARGET clrjit_unix_loongarch64_${ARCH_HOST_NAME} OS unix ARCH loongarch64)
endif (CLR_CMAKE_TARGET_ARCH_LOONGARCH64)

if (CLR_CMAKE_TARGET_ARCH_RISCV64)
  create_standalone_jit(TARGET clrjit_unix_riscv64_${ARCH_HOST_NAME} OS unix ARCH riscv64)
else()
  create_standalone_jit(TARGET clrjit_universal_arm_${ARCH_HOST_NAME} OS universal ARCH arm)
  target_compile_definitions(clrjit_universal_arm_${ARCH_HOST_NAME} PRIVATE ARM_SOFTFP CONFIGURABLE_ARM_ABI)
  create_standalone_jit(TARGET clrjit_win_x86_${ARCH_HOST_NAME} OS win ARCH x86)
endif (CLR_CMAKE_TARGET_ARCH_RISCV64)

if (CLR_CMAKE_TARGET_ARCH_I386 AND CLR_CMAKE_TARGET_UNIX)
  create_standalone_jit(TARGET clrjit_unix_x86_${ARCH_HOST_NAME} OS unix ARCH x86)
endif (CLR_CMAKE_TARGET_ARCH_I386 AND CLR_CMAKE_TARGET_UNIX)

if (CLR_CMAKE_TARGET_UNIX)
    if (NOT ARCH_TARGET_NAME STREQUAL s390x AND NOT ARCH_TARGET_NAME STREQUAL armv6 AND NOT ARCH_TARGET_NAME STREQUAL ppc64le AND NOT ARCH_TARGET_NAME STREQUAL riscv64)
      if(CLR_CMAKE_TARGET_ARCH_ARM OR CLR_CMAKE_TARGET_ARCH_ARM64)
        install_clr(TARGETS clrjit_universal_${ARCH_TARGET_NAME}_${ARCH_HOST_NAME} DESTINATIONS . COMPONENT jit)
      else()
        install_clr(TARGETS clrjit_unix_${ARCH_TARGET_NAME}_${ARCH_HOST_NAME} DESTINATIONS . COMPONENT jit)
      endif()
    endif(NOT ARCH_TARGET_NAME STREQUAL s390x AND NOT ARCH_TARGET_NAME STREQUAL armv6 AND NOT ARCH_TARGET_NAME STREQUAL ppc64le AND NOT ARCH_TARGET_NAME STREQUAL riscv64)
endif()

if (CLR_CMAKE_TARGET_WIN32 AND CLR_CMAKE_PGO_INSTRUMENT)
  # Copy PGO dependency to target dir
  set(PGORT_DLL "pgort140.dll")
  if(CLR_CMAKE_TARGET_ARCH_ARM64)
    cmake_path(SET PGORT_DIR "$ENV{VCToolsInstallDir}bin/arm64")
  elseif(CLR_CMAKE_TARGET_ARCH_I386)
    cmake_path(SET PGORT_DIR "$ENV{VCToolsInstallDir}bin/Hostx86/x86")
  else()
    cmake_path(SET PGORT_DIR "$ENV{VCToolsInstallDir}bin/Hostx64/x64")
  endif()
  install(FILES "${PGORT_DIR}/${PGORT_DLL}" DESTINATION ${CMAKE_INSTALL_PREFIX})
  install(FILES "${PGORT_DIR}/${PGORT_DLL}" DESTINATION ${CMAKE_INSTALL_PREFIX}/sharedFramework)
endif ()
