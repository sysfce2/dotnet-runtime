// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using System.Collections.Generic;
using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using ILLink.RoslynAnalyzer.DataFlow;
using ILLink.Shared.DataFlow;
using Microsoft.CodeAnalysis;

namespace ILLink.RoslynAnalyzer
{
    public static class ISymbolExtensions
    {
        /// <summary>
        /// Returns true if symbol <see paramref="symbol"/> has an attribute with name <see paramref="attributeName"/>.
        /// </summary>
        internal static bool HasAttribute(this ISymbol symbol, string attributeName)
        {
            foreach (var attr in symbol.GetAttributes())
                if (attr.AttributeClass?.Name == attributeName)
                    return true;

            return false;
        }

        internal static bool TryGetAttribute(this ISymbol member, string attributeName, [NotNullWhen(returnValue: true)] out AttributeData? attribute)
        {
            attribute = null;
            foreach (var attr in member.GetAttributes())
            {
                if (attr.AttributeClass is { } attrClass && attrClass.HasName(attributeName))
                {
                    attribute = attr;
                    return true;
                }
            }

            return false;
        }

        internal static IEnumerable<AttributeData> GetAttributes(this ISymbol member, string attributeName)
        {
            foreach (var attr in member.GetAttributes())
            {
                if (attr.AttributeClass is { } attrClass && attrClass.HasName(attributeName))
                    yield return attr;
            }
        }

        /// <summary>
        /// Gets DynamicallyAccessedMemberTypes for any DynamicallyAccessedMembers annotation on the symbol.
        /// This doesn't validate whether the annotation is valid based on the type of the annotated symbol.
        /// Use FlowAnnotations.Get*Annotation when getting annotations that are valid and should participate
        /// in dataflow analysis.
        /// </summary>
        internal static DynamicallyAccessedMemberTypes GetDynamicallyAccessedMemberTypes(this ISymbol symbol)
        {
            if (!TryGetAttribute(symbol, DynamicallyAccessedMembersAnalyzer.DynamicallyAccessedMembersAttribute, out var dynamicallyAccessedMembers))
                return DynamicallyAccessedMemberTypes.None;

            return (DynamicallyAccessedMemberTypes)dynamicallyAccessedMembers!.ConstructorArguments[0].Value!;
        }

        internal static DynamicallyAccessedMemberTypes GetDynamicallyAccessedMemberTypesOnReturnType(this IMethodSymbol methodSymbol)
        {
            AttributeData? dynamicallyAccessedMembers = null;
            foreach (var returnTypeAttribute in methodSymbol.GetReturnTypeAttributes())
                if (returnTypeAttribute.AttributeClass is var attrClass && attrClass != null &&
                    attrClass.HasName(DynamicallyAccessedMembersAnalyzer.DynamicallyAccessedMembersAttribute))
                {
                    dynamicallyAccessedMembers = returnTypeAttribute;
                    break;
                }

            if (dynamicallyAccessedMembers == null)
                return DynamicallyAccessedMemberTypes.None;

            return (DynamicallyAccessedMemberTypes)dynamicallyAccessedMembers.ConstructorArguments[0].Value!;
        }

        internal static ValueSet<string> GetFeatureGuardAnnotations(this IPropertySymbol propertySymbol)
        {
            HashSet<string> featureSet = new();
            foreach (var featureGuardAttribute in propertySymbol.GetAttributes(DynamicallyAccessedMembersAnalyzer.FullyQualifiedFeatureGuardAttribute))
            {
                if (featureGuardAttribute.ConstructorArguments is [TypedConstant { Value: INamedTypeSymbol featureType }])
                    featureSet.Add(featureType.GetDisplayName());
            }
            return featureSet.Count == 0 ? ValueSet<string>.Empty : new ValueSet<string>(featureSet);
        }

        internal static bool TryGetReturnAttribute(this IMethodSymbol member, string attributeName, [NotNullWhen(returnValue: true)] out AttributeData? attribute)
        {
            attribute = null;
            foreach (var attr in member.GetReturnTypeAttributes())
            {
                if (attr.AttributeClass is { } attrClass && attrClass.HasName(attributeName))
                {
                    attribute = attr;
                    return true;
                }
            }

            return false;
        }

        internal static DynamicallyAccessedMemberTypes GetDynamicallyAccessedMemberTypesOnAssociatedSymbol(this IMethodSymbol methodSymbol) =>
            methodSymbol.AssociatedSymbol is ISymbol associatedSymbol ? GetDynamicallyAccessedMemberTypes(associatedSymbol) : DynamicallyAccessedMemberTypes.None;

        internal static bool TryGetOverriddenMember(this ISymbol? symbol, [NotNullWhen(returnValue: true)] out ISymbol? overriddenMember)
        {
            overriddenMember = symbol switch
            {
                IMethodSymbol method => method.OverriddenMethod,
                IPropertySymbol property => property.OverriddenProperty,
                IEventSymbol @event => @event.OverriddenEvent,
                _ => null,
            };
            return overriddenMember != null;
        }

        public static SymbolDisplayFormat ILLinkTypeDisplayFormat { get; } =
            new SymbolDisplayFormat(
                typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces,
                genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters
            );

        public static SymbolDisplayFormat ILLinkMemberDisplayFormat { get; } =
            new SymbolDisplayFormat(
                typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameOnly,
                genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters,
                memberOptions:
                    SymbolDisplayMemberOptions.IncludeParameters |
                    SymbolDisplayMemberOptions.IncludeExplicitInterface,
                parameterOptions:
                    SymbolDisplayParameterOptions.IncludeType |
                    SymbolDisplayParameterOptions.IncludeParamsRefOut
            );

        public static string GetDisplayName(this ISymbol symbol)
        {
            var sb = new StringBuilder();
            switch (symbol)
            {
                case IFieldSymbol fieldSymbol:
                    sb.Append(fieldSymbol.ContainingSymbol.ToDisplayString(ILLinkTypeDisplayFormat));
                    sb.Append('.');
                    sb.Append(fieldSymbol.MetadataName);
                    break;

                case IParameterSymbol parameterSymbol:
                    sb.Append(parameterSymbol.Name);
                    break;

                case IMethodSymbol methodSymbol when methodSymbol.IsStaticConstructor():
                    sb.Append(symbol.ContainingType.ToDisplayString(ILLinkTypeDisplayFormat));
                    sb.Append("..cctor()");
                    break;

                case IMethodSymbol methodSymbol:
                    // Use definition type parameter names, not instance type parameters
                    methodSymbol = methodSymbol.OriginalDefinition;
                    // Format the declaring type with namespace and containing types.
                    if (methodSymbol.ContainingSymbol?.Kind == SymbolKind.NamedType)
                    {
                        // If the containing symbol is a method (for example for local functions),
                        // don't include the containing type's name. This matches the behavior of
                        // CSharpErrorMessageFormat.
                        sb.Append(methodSymbol.ContainingType.ToDisplayString(ILLinkTypeDisplayFormat));
                        sb.Append('.');
                    }
                    // Format parameter types with only type names.
                    sb.Append(methodSymbol.ToDisplayString(ILLinkMemberDisplayFormat));
                    break;

                default:
                    sb.Append(symbol.ToDisplayString());
                    break;
            }

            return sb.ToString();
        }

        public static bool IsInterface(this ISymbol symbol)
        {
            if (symbol is not INamedTypeSymbol namedTypeSymbol)
                return false;

            var typeSymbol = namedTypeSymbol as ITypeSymbol;
            return typeSymbol.TypeKind == TypeKind.Interface;
        }

        public static bool IsSubclassOf(this ISymbol symbol, string ns, string type)
        {
            if (symbol is not ITypeSymbol typeSymbol)
                return false;

            while (typeSymbol != null)
            {
                if (typeSymbol.IsTypeOf(ns, type))
                    return true;

                typeSymbol = typeSymbol.ContainingType;
            }

            return false;
        }

        public static bool IsConstructor([NotNullWhen(returnValue: true)] this ISymbol? symbol)
            => (symbol as IMethodSymbol)?.MethodKind is MethodKind.Constructor or MethodKind.StaticConstructor;

        public static bool IsStaticConstructor([NotNullWhen(returnValue: true)] this ISymbol? symbol)
            => (symbol as IMethodSymbol)?.MethodKind == MethodKind.StaticConstructor;

        public static bool IsEntryPoint(this IMethodSymbol methodSymbol, Compilation compilation)
            => methodSymbol.Name is WellKnownMemberNames.EntryPointMethodName or WellKnownMemberNames.TopLevelStatementsEntryPointMethodName &&
               methodSymbol.IsStatic &&
               (methodSymbol.ReturnsVoid ||
                methodSymbol.ReturnType.SpecialType == SpecialType.System_Int32 ||
                SymbolEqualityComparer.Default.Equals(methodSymbol.ReturnType.OriginalDefinition, compilation.TaskType()) ||
                SymbolEqualityComparer.Default.Equals(methodSymbol.ReturnType.OriginalDefinition, compilation.TaskOfTType()));

        public static bool IsUnmanagedCallersOnlyEntryPoint(this IMethodSymbol methodSymbol)
        {
            foreach (var attr in methodSymbol.GetAttributes())
            {
                if (attr.AttributeClass is { } attrClass && attrClass.HasName("System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute"))
                {
                    foreach (var namedArgument in attr.NamedArguments)
                    {
                        if (namedArgument.Key == "EntryPoint")
                            return true;
                    }
                }
            }

            return false;
        }
    }
}
