// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.ComponentModel;

namespace System.Runtime.InteropServices.ComTypes
{
    /// <remarks>
    /// Note: ADVF_ONLYONCE and ADVF_PRIMEFIRST values conform with objidl.dll but are backwards from
    /// the Platform SDK documentation as of 07/21/2003.
    /// https://learn.microsoft.com/windows/desktop/api/objidl/ne-objidl-tagadvf.
    /// </remarks>
    [EditorBrowsable(EditorBrowsableState.Never)]
    [Flags]
    public enum ADVF
    {
        ADVF_NODATA = 1,
        ADVF_PRIMEFIRST = 2,
        ADVF_ONLYONCE = 4,
        ADVF_DATAONSTOP = 64,
        ADVFCACHE_NOHANDLER = 8,
        ADVFCACHE_FORCEBUILTIN = 16,
        ADVFCACHE_ONSAVE = 32
    }
}
