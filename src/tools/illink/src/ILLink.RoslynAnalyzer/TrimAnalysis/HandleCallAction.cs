// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using ILLink.RoslynAnalyzer;
using ILLink.RoslynAnalyzer.DataFlow;
using ILLink.RoslynAnalyzer.TrimAnalysis;
using ILLink.Shared.DataFlow;
using ILLink.Shared.TypeSystemProxy;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Operations;
using MultiValue = ILLink.Shared.DataFlow.ValueSet<ILLink.Shared.DataFlow.SingleValue>;

namespace ILLink.Shared.TrimAnalysis
{
    internal partial struct HandleCallAction
    {
#pragma warning disable CA1822 // Mark members as static - the other partial implementations might need to be instance methods
#pragma warning disable IDE0060 // Unused parameters - the other partial implementation may need the parameter

        private readonly ISymbol _owningSymbol;
        private readonly IOperation _operation;
        private readonly ReflectionAccessAnalyzer _reflectionAccessAnalyzer;
        private ValueSetLattice<SingleValue> _multiValueLattice;

        public HandleCallAction(
            Location location,
            ISymbol owningSymbol,
            IOperation operation,
            ValueSetLattice<SingleValue> multiValueLattice,
            Action<Diagnostic>? reportDiagnostic)
        {
            _owningSymbol = owningSymbol;
            _operation = operation;
            _isNewObj = operation.Kind == OperationKind.ObjectCreation;
            _diagnosticContext = new DiagnosticContext(location, reportDiagnostic);
            _annotations = FlowAnnotations.Instance;
            _reflectionAccessAnalyzer = new(reportDiagnostic, typeHierarchyType: null);
            _requireDynamicallyAccessedMembersAction = new(_diagnosticContext, _reflectionAccessAnalyzer);
            _multiValueLattice = multiValueLattice;
        }

        private sealed class HasGenericTypeSymbolsVisitor : SymbolVisitor<bool>
        {
            internal static readonly HasGenericTypeSymbolsVisitor Instance = new();

            public override bool VisitArrayType(IArrayTypeSymbol symbol) => Visit(symbol.ElementType);

            public override bool VisitPointerType(IPointerTypeSymbol symbol) => Visit(symbol.PointedAtType);

            public override bool VisitTypeParameter(ITypeParameterSymbol symbol) => true;

            public override bool VisitFunctionPointerType(IFunctionPointerTypeSymbol symbol)
            {
                IMethodSymbol signature = symbol.Signature;

                if (Visit(signature.ReturnType))
                {
                    return true;
                }

                foreach (var param in signature.Parameters)
                {
                    if (Visit(param.Type))
                    {
                        return true;
                    }
                }

                return false;
            }

            public override bool VisitNamedType(INamedTypeSymbol symbol)
            {
                foreach (var arg in symbol.TypeArguments)
                {
                    if (Visit(arg))
                    {
                        return true;
                    }
                }

                return false;
            }
        }

        private partial bool TryHandleIntrinsic(
            MethodProxy calledMethod,
            MultiValue instanceValue,
            IReadOnlyList<MultiValue> argumentValues,
            IntrinsicId intrinsicId,
            out MultiValue? methodReturnValue)
        {
            MultiValue? maybeMethodReturnValue = methodReturnValue = null;
            ValueSetLattice<SingleValue> multiValueLattice = _multiValueLattice;

            switch (intrinsicId)
            {
                case IntrinsicId.Array_Empty:
                    AddReturnValue(ArrayValue.Create(0));
                    break;

                case IntrinsicId.TypeDelegator_Ctor:
                    if (_operation is IObjectCreationOperation)
                        AddReturnValue(argumentValues[0]);

                    break;

                case IntrinsicId.Object_GetType:
                {
                    if (instanceValue.IsEmpty())
                    {
                        AddReturnValue(MultiValueLattice.Top);
                        break;
                    }

                    foreach (var valueNode in instanceValue.AsEnumerable())
                    {
                        // Note that valueNode can be statically typed as some generic argument type.
                        // For example:
                        //   void Method<T>(T instance) { instance.GetType().... }
                        // But it could be that T is annotated with for example PublicMethods:
                        //   void Method<[DAM(PublicMethods)] T>(T instance) { instance.GetType().GetMethod("Test"); }
                        // In this case it's in theory possible to handle it, by treating the T basically as a base class
                        // for the actual type of "instance". But the analysis for this would be pretty complicated (as the marking
                        // has to happen on the callsite, which doesn't know that GetType() will be used...).
                        // For now we're intentionally ignoring this case - it will produce a warning.
                        // The counter example is:
                        //   Method<Base>(new Derived);
                        // In this case to get correct results, trimmer would have to mark all public methods on Derived. Which
                        // currently it won't do.

                        ITypeSymbol? staticType = (valueNode as IValueWithStaticType)?.StaticType?.Type;
                        ITypeSymbol? staticTypeDef = staticType?.OriginalDefinition;
                        if (staticType is null || staticTypeDef is null || staticType is ITypeParameterSymbol)
                        {
                            DynamicallyAccessedMemberTypes annotation = default;
                            if (staticType is ITypeParameterSymbol genericParam)
                            {
                                foreach (var constraintType in genericParam.ConstraintTypes)
                                {
                                    if (constraintType.IsTypeOf("System", "Enum"))
                                        annotation = DynamicallyAccessedMemberTypes.PublicFields;
                                }
                            }

                            if (annotation != default)
                            {
                                AddReturnValue(FlowAnnotations.Instance.GetMethodReturnValue(calledMethod, _isNewObj, annotation));
                            }
                            else
                            {
                                // We don't know anything about the type GetType was called on. Track this as a usual "result of a method call without any annotations"
                                AddReturnValue(FlowAnnotations.Instance.GetMethodReturnValue(calledMethod, _isNewObj));
                            }
                        }
                        else if (staticType.IsSealed || staticType.IsTypeOf("System", "Delegate") || staticType.TypeKind == TypeKind.Array)
                        {
                            // We can treat this one the same as if it was a typeof() expression

                            // We can allow Object.GetType to be modeled as System.Delegate because we keep all methods
                            // on delegates anyway so reflection on something this approximation would miss is actually safe.

                            // We can also treat all arrays as "sealed" since it's not legal to derive from Array type (even though it is not sealed itself)

                            // We ignore the fact that the type can be annotated (see below for handling of annotated types)
                            // This means the annotations (if any) won't be applied - instead we rely on the exact knowledge
                            // of the type. So for example even if the type is annotated with PublicMethods
                            // but the code calls GetProperties on it - it will work - mark properties, don't mark methods
                            // since we ignored the fact that it's annotated.
                            // This can be seen a little bit as a violation of the annotation, but we already have similar cases
                            // where a parameter is annotated and if something in the method sets a specific known type to it
                            // we will also make it just work, even if the annotation doesn't match the usage.
                            AddReturnValue(new SystemTypeValue(new(staticType)));
                        }
                        else if (staticType.IsTypeOf("System", "Enum"))
                        {
                            AddReturnValue(FlowAnnotations.Instance.GetMethodReturnValue(calledMethod, _isNewObj, DynamicallyAccessedMemberTypes.PublicFields));
                        }
                        else
                        {
                            var annotation = FlowAnnotations.GetTypeAnnotation(staticType);
                            AddReturnValue(FlowAnnotations.Instance.GetMethodReturnValue(calledMethod, _isNewObj, annotation));
                        }
                    }
                    break;
                }

                case IntrinsicId.TypeMapping_GetOrCreateProxyTypeMapping:
                case IntrinsicId.TypeMapping_GetOrCreateExternalTypeMapping:
                {
                    ITypeSymbol typeMapGroup = calledMethod.Method.TypeArguments[0];
                    if (HasGenericTypeSymbolsVisitor.Instance.Visit(typeMapGroup))
                    {
                        // We only support GetOrCreateExternalTypeMapping or GetOrCreateProxyTypeMapping for a fully specified type.
                        _diagnosticContext.AddDiagnostic(DiagnosticId.TypeMapGroupTypeCannotBeStaticallyDetermined,
                            typeMapGroup.GetDisplayName());
                    }
                    break;
                }

                // Some intrinsics are unimplemented by the analyzer.
                // These will fall back to the usual return-value handling.
                case IntrinsicId.Array_CreateInstance:
                case IntrinsicId.Assembly_GetFile:
                case IntrinsicId.Assembly_GetFiles:
                case IntrinsicId.AssemblyName_get_EscapedCodeBase:
                case IntrinsicId.Assembly_get_Location:
                case IntrinsicId.AssemblyName_get_CodeBase:
                case IntrinsicId.Delegate_get_Method:
                case IntrinsicId.Enum_GetValues:
                case IntrinsicId.Marshal_DestroyStructure:
                case IntrinsicId.Marshal_GetDelegateForFunctionPointer:
                case IntrinsicId.Marshal_OffsetOf:
                case IntrinsicId.Marshal_PtrToStructure:
                case IntrinsicId.Marshal_SizeOf:
                case IntrinsicId.RuntimeReflectionExtensions_GetMethodInfo:
                    break;


                default:
                    return false;
            }

            methodReturnValue = maybeMethodReturnValue;
            return true;

            void AddReturnValue(MultiValue value)
            {
                maybeMethodReturnValue = (maybeMethodReturnValue is null) ? value : multiValueLattice.Meet((MultiValue)maybeMethodReturnValue, value);
            }
        }

        private partial IEnumerable<SystemReflectionMethodBaseValue> GetMethodsOnTypeHierarchy(TypeProxy type, string name, BindingFlags? bindingFlags)
        {
            foreach (var method in type.Type.GetMethodsOnTypeHierarchy(m => m.Name == name, bindingFlags))
                yield return new SystemReflectionMethodBaseValue(new MethodProxy(method));
        }

        private partial IEnumerable<SystemTypeValue> GetNestedTypesOnType(TypeProxy type, string name, BindingFlags? bindingFlags)
        {
            foreach (var nestedType in type.Type.GetNestedTypesOnType(t => t.Name == name, bindingFlags))
                yield return new SystemTypeValue(new TypeProxy(nestedType));
        }

        private partial bool MethodIsTypeConstructor(MethodProxy method)
        {
            if (!method.Method.IsConstructor())
                return false;
            var type = method.Method.ContainingType;
            while (type is not null)
            {
                if (type.IsTypeOf(WellKnownType.System_Type))
                    return true;
                type = type.BaseType;
            }
            return false;
        }

        private partial bool TryGetBaseType(TypeProxy type, out TypeProxy? baseType)
        {
            if (type.Type.BaseType is not null)
            {
                baseType = new TypeProxy(type.Type.BaseType);
                return true;
            }

            baseType = null;
            return false;
        }

        private partial bool TryResolveTypeNameForCreateInstanceAndMark(in MethodProxy calledMethod, string assemblyName, string typeName, out TypeProxy resolvedType)
        {
            // Intentionally never resolve anything. Analyzer can really only see types from the current compilation unit. For other assemblies
            // it typically only sees reference assemblies and thus just public API. It's not worth (at least for now) to try to resolve
            // the assembly name and type name as it should be rare this is actually ever used and even rarer to have problems (Warnings).
            // In any case the trimmer will process this correctly as it has a global view.
            resolvedType = default;
            return false;
        }

        private partial void MarkStaticConstructor(TypeProxy type)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForConstructorsOnType(_diagnosticContext.Location, type.Type, BindingFlags.Static, parameterCount: 0);

        private partial void MarkEventsOnTypeHierarchy(TypeProxy type, string name, BindingFlags? bindingFlags)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForEventsOnTypeHierarchy(_diagnosticContext.Location, type.Type, name, bindingFlags);

        private partial void MarkFieldsOnTypeHierarchy(TypeProxy type, string name, BindingFlags? bindingFlags)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForFieldsOnTypeHierarchy(_diagnosticContext.Location, type.Type, name, bindingFlags);

        private partial void MarkPropertiesOnTypeHierarchy(TypeProxy type, string name, BindingFlags? bindingFlags)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForPropertiesOnTypeHierarchy(_diagnosticContext.Location, type.Type, name, bindingFlags);

        private partial void MarkPublicParameterlessConstructorOnType(TypeProxy type)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForPublicParameterlessConstructor(_diagnosticContext.Location, type.Type);

        private partial void MarkConstructorsOnType(TypeProxy type, BindingFlags? bindingFlags, int? parameterCount)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForConstructorsOnType(_diagnosticContext.Location, type.Type, bindingFlags, parameterCount);

        private partial void MarkMethod(MethodProxy method)
            => _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForMethod(_diagnosticContext.Location, method.Method);

        // TODO: Does the analyzer need to do something here?
        private partial void MarkType(TypeProxy type) { }

        private partial bool MarkAssociatedProperty(MethodProxy method)
        {
            if (method.Method.MethodKind == MethodKind.PropertyGet || method.Method.MethodKind == MethodKind.PropertySet)
            {
                var property = (IPropertySymbol)method.Method.AssociatedSymbol!;
                Debug.Assert(property != null);
                _reflectionAccessAnalyzer.GetReflectionAccessDiagnosticsForProperty(_diagnosticContext.Location, property!);
                return true;
            }

            return false;
        }

        private partial string GetContainingSymbolDisplayName() => _operation.FindContainingSymbol(_owningSymbol).GetDisplayName();
    }
}
