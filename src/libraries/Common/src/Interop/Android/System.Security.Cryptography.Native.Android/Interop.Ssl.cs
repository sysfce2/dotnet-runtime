// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Net.Security;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;

using Microsoft.Win32.SafeHandles;

using SafeSslHandle = System.Net.SafeSslHandle;

internal static partial class Interop
{
    internal static partial class AndroidCrypto
    {
        private const int UNSUPPORTED_API_LEVEL = 2;

        internal enum PAL_SSLStreamStatus
        {
            OK = 0,
            NeedData = 1,
            Error = 2,
            Renegotiate = 3,
            Closed = 4,
        };

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamCreate")]
        private static partial SafeSslHandle SSLStreamCreate(IntPtr sslStreamProxyHandle);
        internal static SafeSslHandle SSLStreamCreate(SslStream.JavaProxy sslStreamProxy)
            => SSLStreamCreate(sslStreamProxy.Handle);

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamCreateWithCertificates")]
        private static partial SafeSslHandle SSLStreamCreateWithCertificates(
            IntPtr sslStreamProxyHandle,
            ref byte pkcs8PrivateKey,
            int pkcs8PrivateKeyLen,
            PAL_KeyAlgorithm algorithm,
            IntPtr[] certs,
            int certsLen);
        internal static SafeSslHandle SSLStreamCreateWithCertificates(
            SslStream.JavaProxy sslStreamProxy,
            ReadOnlySpan<byte> pkcs8PrivateKey,
            PAL_KeyAlgorithm algorithm,
            IntPtr[] certificates)
        {
            return SSLStreamCreateWithCertificates(
                sslStreamProxy.Handle,
                ref MemoryMarshal.GetReference(pkcs8PrivateKey),
                pkcs8PrivateKey.Length,
                algorithm,
                certificates,
                certificates.Length);
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamCreateWithKeyStorePrivateKeyEntry")]
        private static partial SafeSslHandle SSLStreamCreateWithKeyStorePrivateKeyEntry(
            IntPtr sslStreamProxyHandle,
            IntPtr keyStorePrivateKeyEntryHandle);
        internal static SafeSslHandle SSLStreamCreateWithKeyStorePrivateKeyEntry(
            SslStream.JavaProxy sslStreamProxy,
            IntPtr keyStorePrivateKeyEntryHandle)
        {
            return SSLStreamCreateWithKeyStorePrivateKeyEntry(sslStreamProxy.Handle, keyStorePrivateKeyEntryHandle);
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_RegisterRemoteCertificateValidationCallback")]
        internal static unsafe partial void RegisterRemoteCertificateValidationCallback(
            delegate* unmanaged<IntPtr, bool> verifyRemoteCertificate);

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamInitialize")]
        private static unsafe partial int SSLStreamInitializeImpl(
            SafeSslHandle sslHandle,
            [MarshalAs(UnmanagedType.U1)] bool isServer,
            IntPtr managedContextHandle,
            delegate* unmanaged<IntPtr, byte*, int*, PAL_SSLStreamStatus> streamRead,
            delegate* unmanaged<IntPtr, byte*, int, void> streamWrite,
            delegate* unmanaged<IntPtr, void> managedContextCleanup,
            int appBufferSize,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string? peerHost);
        internal static unsafe void SSLStreamInitialize(
            SafeSslHandle sslHandle,
            bool isServer,
            IntPtr managedContextHandle,
            delegate* unmanaged<IntPtr, byte*, int*, PAL_SSLStreamStatus> streamRead,
            delegate* unmanaged<IntPtr, byte*, int, void> streamWrite,
            delegate* unmanaged<IntPtr, void> managedContextCleanup,
            int appBufferSize,
            string? peerHost)
        {
            int ret = SSLStreamInitializeImpl(sslHandle, isServer, managedContextHandle, streamRead, streamWrite, managedContextCleanup, appBufferSize, peerHost);
            if (ret != SUCCESS)
                throw new SslException();
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamSetTargetHost")]
        private static partial int SSLStreamSetTargetHostImpl(
            SafeSslHandle sslHandle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string targetHost);
        internal static void SSLStreamSetTargetHost(
            SafeSslHandle sslHandle,
            string targetHost)
        {
            int ret = SSLStreamSetTargetHostImpl(sslHandle, targetHost);
            if (ret == UNSUPPORTED_API_LEVEL)
                throw new PlatformNotSupportedException(SR.net_android_ssl_api_level_unsupported);
            else if (ret != SUCCESS)
                throw new SslException();
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamIsLocalCertificateUsed")]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static partial bool SSLStreamIsLocalCertificateUsed(SafeSslHandle sslHandle);

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamRequestClientAuthentication")]
        internal static partial void SSLStreamRequestClientAuthentication(SafeSslHandle sslHandle);

        [StructLayout(LayoutKind.Sequential)]
        private unsafe struct ApplicationProtocolData
        {
            public byte* Data;
            public int Length;
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamSetApplicationProtocols")]
        private static unsafe partial int SSLStreamSetApplicationProtocols(SafeSslHandle sslHandle, ApplicationProtocolData[] protocolData, int count);
        internal static unsafe void SSLStreamSetApplicationProtocols(SafeSslHandle sslHandle, List<SslApplicationProtocol> protocols)
        {
            int count = protocols.Count;
            MemoryHandle[] memHandles = new MemoryHandle[count];
            ApplicationProtocolData[] protocolData = new ApplicationProtocolData[count];
            try
            {
                for (int i = 0; i < count; i++)
                {
                    ReadOnlyMemory<byte> protocol = protocols[i].Protocol;
                    memHandles[i] = protocol.Pin();
                    protocolData[i] = new ApplicationProtocolData
                    {
                        Data = (byte*)memHandles[i].Pointer,
                        Length = protocol.Length
                    };
                }
                int ret = SSLStreamSetApplicationProtocols(sslHandle, protocolData, count);
                if (ret != SUCCESS)
                {
                    throw new SslException();
                }
            }
            finally
            {
                foreach (MemoryHandle memHandle in memHandles)
                {
                    memHandle.Dispose();
                }
            }
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamSetEnabledProtocols")]
        private static partial int SSLStreamSetEnabledProtocols(SafeSslHandle sslHandle, ref SslProtocols protocols, int length);
        internal static void SSLStreamSetEnabledProtocols(SafeSslHandle sslHandle, ReadOnlySpan<SslProtocols> protocols)
        {
            int ret = SSLStreamSetEnabledProtocols(sslHandle, ref MemoryMarshal.GetReference(protocols), protocols.Length);
            if (ret != SUCCESS)
                throw new SslException();
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamHandshake")]
        internal static partial PAL_SSLStreamStatus SSLStreamHandshake(SafeSslHandle sslHandle);

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamGetApplicationProtocol")]
        private static partial int SSLStreamGetApplicationProtocol(SafeSslHandle ssl, byte[]? buf, ref int len);
        internal static byte[]? SSLStreamGetApplicationProtocol(SafeSslHandle ssl)
        {
            int len = 0;
            int ret = SSLStreamGetApplicationProtocol(ssl, null, ref len);
            if (ret != INSUFFICIENT_BUFFER)
                return null;

            byte[] bytes = new byte[len];
            ret = SSLStreamGetApplicationProtocol(ssl, bytes, ref len);
            if (ret != SUCCESS)
                return null;

            return bytes;
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamRead")]
        private static unsafe partial PAL_SSLStreamStatus SSLStreamRead(
            SafeSslHandle sslHandle,
            byte* buffer,
            int length,
            out int bytesRead);
        internal static unsafe PAL_SSLStreamStatus SSLStreamRead(
            SafeSslHandle sslHandle,
            ReadOnlySpan<byte> buffer,
            out int bytesRead)
        {
            fixed (byte* bufferPtr = buffer)
            {
                return SSLStreamRead(sslHandle, bufferPtr, buffer.Length, out bytesRead);
            }
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamWrite")]
        private static unsafe partial PAL_SSLStreamStatus SSLStreamWrite(
            SafeSslHandle sslHandle,
            byte* buffer,
            int length);
        internal static unsafe PAL_SSLStreamStatus SSLStreamWrite(
            SafeSslHandle sslHandle,
            ReadOnlyMemory<byte> buffer)
        {
            using (MemoryHandle memHandle = buffer.Pin())
            {
                return SSLStreamWrite(sslHandle, (byte*)memHandle.Pointer, buffer.Length);
            }
        }

        [LibraryImport(Interop.Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamRelease")]
        internal static partial void SSLStreamRelease(IntPtr ptr);

        internal sealed class SslException : Exception
        {
            internal SslException()
            {
            }

            internal SslException(int errorCode)
            {
                HResult = errorCode;
            }
        }

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamGetProtocol")]
        private static partial int SSLStreamGetProtocol(SafeSslHandle ssl, out IntPtr protocol);
        internal static string SSLStreamGetProtocol(SafeSslHandle ssl)
        {
            IntPtr protocolPtr;
            int ret = SSLStreamGetProtocol(ssl, out protocolPtr);
            if (ret != SUCCESS)
                throw new SslException();

            if (protocolPtr == IntPtr.Zero)
                return string.Empty;

            string protocol = Marshal.PtrToStringUni(protocolPtr)!;
            Marshal.FreeHGlobal(protocolPtr);
            return protocol;
        }

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamGetPeerCertificate")]
        internal static partial SafeX509Handle SSLStreamGetPeerCertificate(SafeSslHandle ssl);

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamGetPeerCertificates")]
        private static partial void SSLStreamGetPeerCertificates(
            SafeSslHandle ssl,
            [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)] out IntPtr[] certs,
            out int count);
        internal static IntPtr[]? SSLStreamGetPeerCertificates(SafeSslHandle ssl)
        {
            IntPtr[]? ptrs;
            Interop.AndroidCrypto.SSLStreamGetPeerCertificates(ssl, out ptrs, out _);
            return ptrs;
        }

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamGetCipherSuite")]
        private static partial int SSLStreamGetCipherSuite(SafeSslHandle ssl, out IntPtr cipherSuite);
        internal static string SSLStreamGetCipherSuite(SafeSslHandle ssl)
        {
            IntPtr cipherSuitePtr;
            int ret = SSLStreamGetCipherSuite(ssl, out cipherSuitePtr);
            if (ret != SUCCESS)
                throw new SslException();

            if (cipherSuitePtr == IntPtr.Zero)
                return string.Empty;

            string cipherSuite = Marshal.PtrToStringUni(cipherSuitePtr)!;
            Marshal.FreeHGlobal(cipherSuitePtr);
            return cipherSuite;
        }

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamShutdown")]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static partial bool SSLStreamShutdown(SafeSslHandle ssl);

        [LibraryImport(Libraries.AndroidCryptoNative, EntryPoint = "AndroidCryptoNative_SSLStreamVerifyHostname")]
        [return: MarshalAs(UnmanagedType.U1)]
        internal static partial bool SSLStreamVerifyHostname(
            SafeSslHandle ssl,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string hostname);
    }
}

namespace System.Net
{
    internal sealed class SafeSslHandle : SafeHandle
    {
        public SafeSslHandle()
            : base(IntPtr.Zero, ownsHandle: true)
        {
        }

        protected override bool ReleaseHandle()
        {
            Interop.AndroidCrypto.SSLStreamRelease(handle);
            SetHandle(IntPtr.Zero);
            return true;
        }

        public override bool IsInvalid => handle == IntPtr.Zero;
    }
}
