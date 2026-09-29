#include "Functions/CSFunction.h"
#include "Functions/CSFunction_Params.h"
#include "CSManagedGCHandle.h"
#include "CSManager.h"
#include "CSUnrealSharpSettings.h"
#include "Types/CSClass.h"
#include "Types/CSSkeletonClass.h"
#include "Engine/World.h"

#include "Blueprint/BlueprintExceptionInfo.h"

void UCSFunctionBase::Bind()
{
	UClass* ClassToFindFunction = GetOwnerClass();

#if WITH_EDITOR
	// Redirect to the generated class if we're trying to bind a function in a skeleton class.
	// Since NativeFunctionLookupTable is not copied over when duplicating for reinstancing due to not being a UPROPERTY.
	if (UCSSkeletonClass* OwnerClass = Cast<UCSSkeletonClass>(GetOuter()))
	{
		ClassToFindFunction = OwnerClass->GetGeneratedClass();
	}
#endif

	for (FNativeFunctionLookup& Function : ClassToFindFunction->NativeFunctionLookupTable)
	{
		if (Function.Name != GetFName())
		{
			continue;
		}

		SetNativeFunc(Function.Pointer);
		return;
	}

	// Reinstanced function has empty NativeFunctionLookupTable. So we need to set the function manually to prevent
	// a missing binding.
	// We can copy the behavior in CSFunctionFactory which just uses InvokeManagedMethod and InvokeManagedMethod_Params.
	if (NumParms == 0)
	{
		SetNativeFunc(&UCSFunctionBase::InvokeManagedMethod);
	}
	else
	{
		SetNativeFunc(&UCSFunction_Params::InvokeManagedMethod_Params);
	}
}

bool UCSFunctionBase::UpdateMethodHandle()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSFunctionBase::UpdateMethodHandle);

	// Ignore delegate signatures and classes that are not the generated class.
	// The Blueprint skeleton class is an example of a class that is not the generated class, but still has managed functions.
	if (HasValidMethodHandle() || !IsOwnedByManagedClass() || GetOwnerClass()->HasAllClassFlags(CLASS_Interface))
	{
		return true;
	}

	UCSClass* ManagedClass = static_cast<UCSClass*>(GetOwnerClass());
    // Reinstanced copies don't have a managed type definition. So we should return early cause theres nothing to call.
	if (!ManagedClass->HasManagedTypeDefinition())
	{
		return false;
	}
	UCSManagedAssembly* Assembly = ManagedClass->GetOwningAssembly();

	TSharedPtr<FCSManagedTypeDefinition> ClassInfo = ManagedClass->GetManagedTypeDefinition();
	TSharedPtr<FGCHandle> TypeHandle = ClassInfo->GetTypeGCHandle();

	MethodHandle = Assembly->GetManagedMethod(TypeHandle, FString::Printf(TEXT("Invoke_%s"), *GetName()));
	return MethodHandle.IsValid();
}

bool UCSFunctionBase::IsOwnedByManagedClass() const
{
#if WITH_EDITOR
	return FCSClassUtilities::IsManagedClass(GetOwnerClass());
#else
	return true;
#endif
}

void UCSFunctionBase::InvokeManagedMethod(UObject* ObjectToInvokeOn, FFrame& Stack, RESULT_DECL)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCSFunctionBase::InvokeManagedMethod);

	Stack.Code += !!Stack.Code;

	// Prefer using World as context since it's more stable
	UObject* WorldContext = nullptr;
	if (Stack.Object)
	{
		UWorld* World = Stack.Object->GetWorld();
		WorldContext = World ? World : Stack.Object;
	}

	UCSFunctionBase* ManagedFunction = static_cast<UCSFunctionBase*>(Stack.CurrentNativeFunction);

#if WITH_EDITOR
	// After a full reload, method pointers are stale, so we just lazy update them here.
	if (!ManagedFunction->HasValidMethodHandle() && !ManagedFunction->UpdateMethodHandle())
	{
		return;
	}
#endif

	FString ExceptionMessage;
	int32 ReturnCode;
	{
		UCSManager::FCSCurrentWorldContext CurrentWorldContext(WorldContext);
		ReturnCode = GetManagedCallbacks().InvokeManagedMethod(
			UCSManager::Get().FindManagedObject(ObjectToInvokeOn).GetPointer(),
			ManagedFunction->MethodHandle->GetPointer(),
			Stack.Locals,
			RESULT_PARAM,
			&ExceptionMessage);
	}
	
	if (ReturnCode == 0)
	{
		return;
	}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
	const EBlueprintExceptionType::Type ExceptionType = GetDefault<UCSUnrealSharpSettings>()->bCrashOnException
	                                                        ? EBlueprintExceptionType::FatalError
	                                                        : EBlueprintExceptionType::UserRaisedError;
#else
	const EBlueprintExceptionType::Type ExceptionType = EBlueprintExceptionType::FatalError;
#endif

	const FBlueprintExceptionInfo ExceptionInfo(ExceptionType, FText::FromString(ExceptionMessage));
	FBlueprintCoreDelegates::ThrowScriptException(ObjectToInvokeOn, Stack, ExceptionInfo);
}
