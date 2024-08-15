﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Diagnostics;

namespace Microsoft.Interop
{
    public static class IncrementalGeneratorInitializationContextExtensions
    {
        public static IncrementalValueProvider<EnvironmentFlags> CreateEnvironmentFlagsProvider(this IncrementalGeneratorInitializationContext context)
        {
            var isModuleSkipLocalsInit = context.SyntaxProvider
                .ForAttributeWithMetadataName(
                    TypeNames.System_Runtime_CompilerServices_SkipLocalsInitAttribute,
                    (node, ct) => node is ICompilationUnitSyntax,
                    // If SkipLocalsInit is applied at the top level, it is either applied to the module
                    // or is invalid syntax. As a result, we just need to know if there's any top-level
                    // SkipLocalsInit attributes. So the result we return here is meaningless.
                    (context, ct) => true)
                .Collect()
                .Select((topLevelAttrs, ct) => !topLevelAttrs.IsEmpty ? EnvironmentFlags.SkipLocalsInit : EnvironmentFlags.None);

            var disabledRuntimeMarshalling = context.SyntaxProvider
                .ForAttributeWithMetadataName(
                    TypeNames.System_Runtime_CompilerServices_DisableRuntimeMarshallingAttribute,
                    // DisableRuntimeMarshalling is only available at the top level.
                    (node, ct) => true,
                    // Only allow DisableRuntimeMarshalling attributes from the attribute type in the core assembly.
                    // Otherwise the runtime isn't going to respect it and invalid behavior can happen.
                    (context, ct) => SymbolEqualityComparer.Default.Equals(context.Attributes[0].AttributeClass.ContainingAssembly, context.SemanticModel.Compilation.GetSpecialType(SpecialType.System_Object).ContainingAssembly))
                .Where(valid => valid)
                .Collect()
                .Select((topLevelAttrs, ct) => !topLevelAttrs.IsEmpty ? EnvironmentFlags.DisableRuntimeMarshalling : EnvironmentFlags.None);

            return isModuleSkipLocalsInit.Combine(disabledRuntimeMarshalling).Select((data, ct) => data.Left | data.Right);
        }

        public static IncrementalValueProvider<StubEnvironment> CreateStubEnvironmentProvider(this IncrementalGeneratorInitializationContext context)
        {
            return context.CompilationProvider
                .Combine(context.CreateEnvironmentFlagsProvider())
                .Select((data, ct) =>
                    new StubEnvironment(data.Left, data.Right));
        }

        public static void RegisterDiagnostics(this IncrementalGeneratorInitializationContext context, IncrementalValuesProvider<DiagnosticInfo> diagnostics)
        {
            context.RegisterSourceOutput(diagnostics.Where(diag => diag is not null), (context, diagnostic) =>
            {
                context.ReportDiagnostic(diagnostic.ToDiagnostic());
            });
        }

        public static void RegisterDiagnostics(this IncrementalGeneratorInitializationContext context, IncrementalValuesProvider<Diagnostic> diagnostics)
        {
            context.RegisterSourceOutput(diagnostics.Where(diag => diag is not null), (context, diagnostic) =>
            {
                context.ReportDiagnostic(diagnostic);
            });
        }

        public static void RegisterConcatenatedSyntaxOutputs<TNode>(this IncrementalGeneratorInitializationContext context, IncrementalValuesProvider<TNode> nodes, string fileName)
            where TNode : SyntaxNode
        {
            IncrementalValueProvider<ImmutableArray<string>> generatedMethods = nodes
                .Select(
                    static (node, ct) => node.NormalizeWhitespace().ToFullString())
                .Collect();

            context.RegisterSourceOutput(generatedMethods,
                (context, generatedSources) =>
                {
                    // Don't generate a file if we don't have to, to avoid the extra IDE overhead once we have generated
                    // files in play.
                    if (generatedSources.IsEmpty)
                        return;

                    StringBuilder source = new();
                    // Mark in source that the file is auto-generated.
                    // Explicitly unify on "\r\n" line endings to avoid issues with different line endings in the generated sources.
                    source.Append("// <auto-generated/>\r\n");
                    foreach (string generated in generatedSources)
                    {
                        source.Append(generated);
                        source.Append("\r\n");
                    }

                    // Once https://github.com/dotnet/roslyn/issues/61326 is resolved, we can avoid the ToString() here.
                    context.AddSource(fileName, source.ToString());
                });
        }
    }
}
