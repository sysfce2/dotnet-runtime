﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Microsoft.CodeAnalysis.CSharp.SyntaxFactory;

namespace Microsoft.Interop
{
    internal static class ComInterfaceGeneratorHelpers
    {
        private static readonly IMarshallingGeneratorResolver s_managedToUnmanagedDisabledMarshallingGeneratorResolver = CreateGeneratorResolver(EnvironmentFlags.DisableRuntimeMarshalling, MarshalDirection.ManagedToUnmanaged);
        private static readonly IMarshallingGeneratorResolver s_unmanagedToManagedDisabledMarshallingGeneratorResolver = CreateGeneratorResolver(EnvironmentFlags.DisableRuntimeMarshalling, MarshalDirection.UnmanagedToManaged);
        private static readonly IMarshallingGeneratorResolver s_managedToUnmanagedEnabledMarshallingGeneratorResolver = CreateGeneratorResolver(EnvironmentFlags.None, MarshalDirection.ManagedToUnmanaged);
        private static readonly IMarshallingGeneratorResolver s_unmanagedToManagedEnabledMarshallingGeneratorResolver = CreateGeneratorResolver(EnvironmentFlags.None, MarshalDirection.UnmanagedToManaged);

        private static IMarshallingGeneratorResolver CreateGeneratorResolver(EnvironmentFlags env, MarshalDirection direction)
            => DefaultMarshallingGeneratorResolver.Create(env, direction, TypeNames.GeneratedComInterfaceAttribute_ShortName,
                [
                    new StructAsHResultMarshallerFactory(),
                    new ManagedHResultExceptionGeneratorResolver(),
                    new ComInterfaceDispatchMarshallingResolver(),
                ]);

        public static IMarshallingGeneratorResolver GetGeneratorResolver(EnvironmentFlags env, MarshalDirection direction)
            => (env.HasFlag(EnvironmentFlags.DisableRuntimeMarshalling), direction) switch
            {
                (true, MarshalDirection.ManagedToUnmanaged) => s_managedToUnmanagedDisabledMarshallingGeneratorResolver,
                (true, MarshalDirection.UnmanagedToManaged) => s_unmanagedToManagedDisabledMarshallingGeneratorResolver,
                (false, MarshalDirection.ManagedToUnmanaged) => s_managedToUnmanagedEnabledMarshallingGeneratorResolver,
                (false, MarshalDirection.UnmanagedToManaged) => s_unmanagedToManagedEnabledMarshallingGeneratorResolver,
                _ => throw new UnreachableException(),
            };

        public static ExpressionSyntax CreateEmbeddedDataBlobCreationStatement(ReadOnlySpan<byte> bytes)
        {
            var literals = new CollectionElementSyntax[bytes.Length];

            for (int i = 0; i < bytes.Length; i++)
            {
                literals[i] = ExpressionElement(LiteralExpression(SyntaxKind.NumericLiteralExpression, Literal(bytes[i])));
            }

            // [ <byte literals> ]
            return CollectionExpression(SeparatedList(literals));
        }
    }
}
