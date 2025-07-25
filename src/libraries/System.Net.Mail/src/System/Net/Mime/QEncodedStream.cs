// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Buffers;

namespace System.Net.Mime
{
    /// <summary>
    /// This stream performs in-place decoding of quoted-printable
    /// encoded streams.  Encoding requires copying into a separate
    /// buffer as the data being encoded will most likely grow.
    /// Encoding and decoding is done transparently to the caller.
    /// </summary>
    internal sealed class QEncodedStream : DelegatedStream, IEncodableStream
    {

        private static ReadOnlySpan<byte> HexDecodeMap =>
        [
            // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 0
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 1
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 2
             0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  255,  255, 255, 255, 255, 255, // 3
             255, 10,  11,  12,  13,  14,  15,  255, 255, 255, 255, 255, 255, 255, 255, 255, // 4
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 5
             255, 10,  11,  12,  13,  14,  15,  255, 255, 255, 255, 255, 255, 255, 255, 255, // 6
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 7
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 8
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 9
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // A
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // B
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // C
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // D
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // E
             255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // F
        ];

        private readonly WriteStateInfoBase _writeState;
        private readonly QEncoder _encoder;

        internal QEncodedStream(WriteStateInfoBase wsi) : base(new MemoryStream())
        {
            _writeState = wsi;
            _encoder = new QEncoder(_writeState);
        }

        private ReadStateInfo ReadState => field ??= new ReadStateInfo();

        internal WriteStateInfoBase WriteState => _writeState;

        public override bool CanRead => BaseStream.CanRead;
        public override bool CanWrite => BaseStream.CanWrite;

        public override void Close()
        {
            FlushInternal();
            base.Close();
        }

        public unsafe int DecodeBytes(Span<byte> buffer)
        {
            fixed (byte* pBuffer = buffer)
            {
                byte* start = pBuffer;
                byte* source = start;
                byte* dest = start;
                byte* end = start + buffer.Length;

                // if the last read ended in a partially decoded
                // sequence, pick up where we left off.
                if (ReadState.IsEscaped)
                {
                    // this will be -1 if the previous read ended
                    // with an escape character.
                    if (ReadState.Byte == -1)
                    {
                        // if we only read one byte from the underlying
                        // stream, we'll need to save the byte and
                        // ask for more.
                        if (buffer.Length == 1)
                        {
                            ReadState.Byte = *source;
                            return 0;
                        }

                        // '=\r\n' means a soft (aka. invisible) CRLF sequence...
                        if (source[0] != '\r' || source[1] != '\n')
                        {
                            byte b1 = HexDecodeMap[source[0]];
                            byte b2 = HexDecodeMap[source[1]];
                            if (b1 == 255)
                                throw new FormatException(SR.Format(SR.InvalidHexDigit, b1));
                            if (b2 == 255)
                                throw new FormatException(SR.Format(SR.InvalidHexDigit, b2));

                            *dest++ = (byte)((b1 << 4) + b2);
                        }

                        source += 2;
                    }
                    else
                    {
                        // '=\r\n' means a soft (aka. invisible) CRLF sequence...
                        if (ReadState.Byte != '\r' || *source != '\n')
                        {
                            byte b1 = HexDecodeMap[ReadState.Byte];
                            byte b2 = HexDecodeMap[*source];
                            if (b1 == 255)
                                throw new FormatException(SR.Format(SR.InvalidHexDigit, b1));
                            if (b2 == 255)
                                throw new FormatException(SR.Format(SR.InvalidHexDigit, b2));
                            *dest++ = (byte)((b1 << 4) + b2);
                        }
                        source++;
                    }
                    // reset state for next read.
                    ReadState.IsEscaped = false;
                    ReadState.Byte = -1;
                }

                // Here's where most of the decoding takes place.
                // We'll loop around until we've inspected all the
                // bytes read.
                while (source < end)
                {
                    // if the source is not an escape character, then
                    // just copy as-is.
                    if (*source != '=')
                    {
                        if (*source == '_')
                        {
                            *dest++ = (byte)' ';
                            source++;
                        }
                        else
                        {
                            *dest++ = *source++;
                        }
                    }
                    else
                    {
                        // determine where we are relative to the end
                        // of the data.  If we don't have enough data to
                        // decode the escape sequence, save off what we
                        // have and continue the decoding in the next
                        // read.  Otherwise, decode the data and copy
                        // into dest.
                        switch (end - source)
                        {
                            case 2:
                                ReadState.Byte = source[1];
                                goto case 1;
                            case 1:
                                ReadState.IsEscaped = true;
                                goto EndWhile;
                            default:
                                if (source[1] != '\r' || source[2] != '\n')
                                {
                                    byte b1 = HexDecodeMap[source[1]];
                                    byte b2 = HexDecodeMap[source[2]];
                                    if (b1 == 255)
                                        throw new FormatException(SR.Format(SR.InvalidHexDigit, b1));
                                    if (b2 == 255)
                                        throw new FormatException(SR.Format(SR.InvalidHexDigit, b2));

                                    *dest++ = (byte)((b1 << 4) + b2);
                                }
                                source += 3;
                                break;
                        }
                    }
                }
            EndWhile:
                return (int)(dest - start);
            }
        }

        public int EncodeBytes(ReadOnlySpan<byte> buffer) => _encoder.EncodeBytes(buffer, true, true);

        public int EncodeString(string value, Encoding encoding) => _encoder.EncodeString(value, encoding);

        public string GetEncodedString() => _encoder.GetEncodedString();

        public override void Flush()
        {
            FlushInternal();
            base.Flush();
        }

        public override async Task FlushAsync(CancellationToken cancellationToken)
        {
            await FlushInternalAsync(cancellationToken).ConfigureAwait(false);
            await base.FlushAsync(cancellationToken).ConfigureAwait(false);
        }

        private void FlushInternal()
        {
            if (_writeState != null && _writeState.Length > 0)
            {
                BaseStream.Write(WriteState.Buffer.AsSpan(0, WriteState.Length));
                WriteState.Reset();
            }
        }

        private async ValueTask FlushInternalAsync(CancellationToken cancellationToken)
        {
            if (_writeState != null && _writeState.Length > 0)
            {
                await BaseStream.WriteAsync(WriteState.Buffer.AsMemory(0, WriteState.Length), cancellationToken).ConfigureAwait(false);
                WriteState.Reset();
            }
        }

        protected override int ReadInternal(Span<byte> buffer)
        {
            throw new NotImplementedException();
        }

        protected override ValueTask<int> ReadAsyncInternal(Memory<byte> buffer, CancellationToken cancellationToken = default)
        {
            throw new NotImplementedException();
        }

        protected override void WriteInternal(ReadOnlySpan<byte> buffer)
        {
            int written = 0;
            while (true)
            {
                written += EncodeBytes(buffer.Slice(written));
                if (written < buffer.Length)
                {
                    FlushInternal();
                }
                else
                {
                    break;
                }
            }
        }

        protected override async ValueTask WriteAsyncInternal(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken = default)
        {
            int written = 0;
            while (true)
            {
                written += EncodeBytes(buffer.Span.Slice(written));
                if (written < buffer.Length)
                {
                    await FlushInternalAsync(cancellationToken).ConfigureAwait(false);
                }
                else
                {
                    break;
                }
            }
        }

        private sealed class ReadStateInfo
        {
            internal bool IsEscaped { get; set; }
            internal short Byte { get; set; } = -1;
        }
    }
}
