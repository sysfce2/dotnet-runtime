// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Generated by Fuzzlyn v2.4 on 2024-08-26 23:38:13
// Run on Arm64 Linux
// Seed: 8716802894387291290-vectort,vector64,vector128,armadvsimd,armadvsimdarm64,armaes,armarmbase,armarmbasearm64,armcrc32,armcrc32arm64,armdp,armrdm,armrdmarm64,armsha1,armsha256
// Reduced from 19.5 KiB to 0.5 KiB in 00:00:27
// Debug: Outputs <0, 0, 0, 0>
// Release: Outputs <0, 0, 4457472, 0>
using System;
using System.Numerics;
using System.Runtime.Intrinsics;
using System.Runtime.Intrinsics.Arm;
using Xunit;

public class C0
{
    public ushort F2;
    public ushort F8;
}

public class Runtime_107173
{
    public static C0 s_8 = new C0();

    [Fact]
    public static void TestEntryPoint()
    {
        if (AdvSimd.IsSupported)
        {
            var vr6 = s_8.F8;
            var vr7 = s_8.F2;
            var vr8 = Vector64.Create(vr6, vr7, 0, 0);
            Vector128<uint> vr9 = AdvSimd.ShiftLeftLogicalWideningLower(vr8, 0);
            Assert.Equal(vr9, Vector128<uint>.Zero);
        }
    }
}