# Optimizing programs targeting Native AOT

See the official documentation at https://learn.microsoft.com/dotnet/core/deploying/native-aot/optimizing.

The rest of the document talks about options that exist, but their names and purpose are subject to change without a breaking change notice.

## Options related to code generation
* `<IlcInstructionSet>`: By default, the compiler targets the minimum instruction set supported by the target OS and architecture. This option allows targeting newer instruction sets for better performance. The native binary will require the instruction sets to be supported by the hardware in order to run. For example, `<IlcInstructionSet>avx2,bmi2,fma,pclmul,popcnt,aes</IlcInstructionSet>` will produce binary that takes advantage of instruction sets that are typically present on current Intel and AMD processors. `<IlcInstructionSet>native</IlcInstructionSet>` will produce a binary that uses instructions that currently running CPU supports (no cross-compilation support). Run `ilc --help` for the full list of available instruction sets. `ilc` can be executed from the NativeAOT package in your local nuget cache e.g. `%USERPROFILE%\.nuget\packages\runtime.win-x64.microsoft.dotnet.ilcompiler\8.0.0-...\tools\ilc.exe` on Windows or `~/.nuget/packages/runtime.linux-arm64.microsoft.dotnet.ilcompiler/8.0.0-.../tools/ilc` on Linux.
* `<IlcMaxVectorTBitWidth>`: By default, the compiler targets the a `Vector<T>` size of `16` or `32` bytes, depending on the underlying instruction sets supported. This option allows specifying a different maximum bit width. For example, if by default on x64 hardware `Vector<T>` will be 16-bytes. However, if `AVX2` is targeted then `Vector<T>` will automatically grow to be 32-bytes instead, setting `<IlcMaxVectorTBitWidth>128</IlcMaxVectorTBitWidth>` would keep the size as 16-bytes. Alternatively, even if `AVX512F` is targeted then by default `Vector<T>` will not grow larger than 32-bytes, setting `<IlcMaxVectorTBitWidth>512</IlcMaxVectorTBitWidth>` would allow it to grow to 64-bytes.