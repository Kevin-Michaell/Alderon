// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#include "Abilities/AbilityTask_ApplyRootMotionForwardForce.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Components/ICharacterMovementComponent.h"
#include "AbilitySystemLog.h"

UAbilityTask_ApplyRootMotionForwardForce::UAbilityTask_ApplyRootMotionForwardForce(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	StrengthOverTime = nullptr;
}

UAbilityTask_ApplyRootMotionForwardForce* UAbilityTask_ApplyRootMotionForwardForce::ApplyRootMotionForwardForce
(
	UGameplayAbility* OwningAbility, 
	FName TaskInstanceName, 
	float Strength, 
	float Duration, 
	UCurveFloat* StrengthOverTime,
	ERootMotionFinishVelocityMode VelocityOnFinishMode, 
	FVector SetVelocityOnFinish, 
	float ClampVelocityOnFinish,
	bool bEnableGravity
)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Duration(Duration);

	UAbilityTask_ApplyRootMotionForwardForce* MyTask = NewAbilityTask<UAbilityTask_ApplyRootMotionForwardForce>(OwningAbility, TaskInstanceName);

	MyTask->ForceName = TaskInstanceName;
	MyTask->SetStrength(Strength);
	MyTask->SetDuration(Duration);
	MyTask->SetIsAdditive(false);
	MyTask->SetStrengthOverTime(StrengthOverTime);
	MyTask->FinishVelocityMode = VelocityOnFinishMode;
	MyTask->FinishSetVelocity = SetVelocityOnFinish;
	MyTask->FinishClampVelocity = ClampVelocityOnFinish;
	MyTask->SetEnableGravity(bEnableGravity);
	MyTask->SharedInitAndApply();

	return MyTask;
}

void UAbilityTask_ApplyRootMotionForwardForce::SetStrength(float NewStrength)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UAbilityTask_ApplyRootMotionForwardForce, Strength, NewStrength, this);
}

void UAbilityTask_ApplyRootMotionForwardForce::SetDuration(float NewDuration)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UAbilityTask_ApplyRootMotionForwardForce, Duration, NewDuration, this);
}

void UAbilityTask_ApplyRootMotionForwardForce::SetIsAdditive(bool bNewIsAdditive)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UAbilityTask_ApplyRootMotionForwardForce, bIsAdditive, bNewIsAdditive, this);
}

void UAbilityTask_ApplyRootMotionForwardForce::SetStrengthOverTime(UCurveFloat* NewStrengthOverTime)
{
	StrengthOverTime = NewStrengthOverTime;
	MARK_PROPERTY_DIRTY_FROM_NAME(UAbilityTask_ApplyRootMotionForwardForce, StrengthOverTime, this);
}

void UAbilityTask_ApplyRootMotionForwardForce::SetEnableGravity(bool bNewEnableGravity)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UAbilityTask_ApplyRootMotionForwardForce, bEnableGravity, bNewEnableGravity, this);
}

void UAbilityTask_ApplyRootMotionForwardForce::SharedInitAndApply()
{
	if (AbilitySystemComponent->AbilityActorInfo->MovementComponent.IsValid())
	{
		MovementComponent = Cast<UCharacterMovementComponent>(AbilitySystemComponent->AbilityActorInfo->MovementComponent.Get());
		StartTime = GetWorld()->GetTimeSeconds();
		EndTime = StartTime + GetDuration();

		if (MovementComponent)
		{
			ForceName = ForceName.IsNone() ? FName("AbilityTaskApplyRootMotionForwardForce"): ForceName;
			TSharedPtr<FRootMotionSource_ForwardForce> ForwardForce = MakeShared<FRootMotionSource_ForwardForce>();
			ForwardForce->InstanceName = ForceName;
			ForwardForce->AccumulateMode = IsAdditive() ? ERootMotionAccumulateMode::Additive : ERootMotionAccumulateMode::Override;
			ForwardForce->Priority = 5;
			ForwardForce->Strength = GetStrength();
			ForwardForce->Duration = GetDuration();
			ForwardForce->StrengthOverTime = GetStrengthOverTime();
			ForwardForce->FinishVelocityParams.Mode = FinishVelocityMode;
			ForwardForce->FinishVelocityParams.SetVelocity = FinishSetVelocity;
			ForwardForce->FinishVelocityParams.ClampVelocity = FinishClampVelocity;
			if (ShouldEnableGravity())
			{
				ForwardForce->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
			}
			RootMotionSourceID = MovementComponent->ApplyRootMotionSource(ForwardForce);
		}
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("UAbilityTask_ApplyRootMotionForwardForce called in Ability %s with null MovementComponent; Task Instance Name %s."), 
			Ability ? *Ability->GetName() : TEXT("NULL"), 
			*InstanceName.ToString());
	}
}

void UAbilityTask_ApplyRootMotionForwardForce::TickTask(float DeltaTime)
{
	if (bIsFinished)
	{
		return;
	}

	Super::TickTask(DeltaTime);

	AActor* MyActor = GetAvatarActor();
	if (MyActor)
	{
		const bool bTimedOut = HasTimedOut();
		const bool bIsInfiniteDuration = GetDuration() < 0.f;

		if (!bIsInfiniteDuration && bTimedOut)
		{
			// Task has finished
			bIsFinished = true;
			if (!bIsSimulating)
			{
				MyActor->ForceNetUpdate();
				if (ShouldBroadcastAbilityTaskDelegates())
				{
					OnFinish.Broadcast();
				}
				EndTask();
			}
		}
	}
	else
	{
		bIsFinished = true;
		EndTask();
	}
}

void UAbilityTask_ApplyRootMotionForwardForce::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params{};
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UAbilityTask_ApplyRootMotionForwardForce, Strength, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UAbilityTask_ApplyRootMotionForwardForce, Duration, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UAbilityTask_ApplyRootMotionForwardForce, bIsAdditive, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UAbilityTask_ApplyRootMotionForwardForce, StrengthOverTime, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UAbilityTask_ApplyRootMotionForwardForce, bEnableGravity, Params);
}

void UAbilityTask_ApplyRootMotionForwardForce::PreDestroyFromReplication()
{
	bIsFinished = true;
	EndTask();
}

void UAbilityTask_ApplyRootMotionForwardForce::OnDestroy(bool AbilityIsEnding)
{
	if (MovementComponent)
	{
		MovementComponent->RemoveRootMotionSourceByID(RootMotionSourceID);
	}

	Super::OnDestroy(AbilityIsEnding);
}

//
// FRootMotionSource_ForwardForce
//

FRootMotionSource_ForwardForce::FRootMotionSource_ForwardForce()
	: Strength(0)
	, StrengthOverTime(nullptr)
{
	// Disable Partial End Tick for Constant Forces.
	// Otherwise we end up with very inconsistent velocities on the last frame.
	// This ensures that the ending velocity is maintained and consistent.
	Settings.SetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
}

FRootMotionSource* FRootMotionSource_ForwardForce::Clone() const
{
	FRootMotionSource_ForwardForce* CopyPtr = new FRootMotionSource_ForwardForce(*this);
	return CopyPtr;
}

bool FRootMotionSource_ForwardForce::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}

	// We can cast safely here since in FRootMotionSource::Matches() we ensured ScriptStruct equality
	const FRootMotionSource_ForwardForce* OtherCast = static_cast<const FRootMotionSource_ForwardForce*>(Other);

	return FMath::Abs(OtherCast->Strength - Strength) < 0.1f &&
		StrengthOverTime == OtherCast->StrengthOverTime;
}

bool FRootMotionSource_ForwardForce::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
	// Check that it matches
	if (!FRootMotionSource::MatchesAndHasSameState(Other))
	{
		return false;
	}

	return true; // ForwardForce has no unique state
}

bool FRootMotionSource_ForwardForce::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	if (!FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup))
	{
		return false;
	}

	return true; // ForwardForce has no unique state other than Time which is handled by FRootMotionSource
}

void FRootMotionSource_ForwardForce::PrepareRootMotion
(
	float SimulationTime,
	float MovementTickTime,
	const ACharacter& Character,
	const UCharacterMovementComponent& MoveComponent
)
{
	if (!(Character.HasAuthority() || Character.IsLocallyControlled()))
	{
		// Character is neither owned or server, do nothing.
		RootMotionParams.Set(FTransform::Identity);

		SetTime(GetTime() + SimulationTime);

		return;
	}

	RootMotionParams.Clear();
	
	FVector ForwardVector = Character.GetActorForwardVector();

	if (const UICharacterMovementComponent* ICharacterMovementComponent = Cast<UICharacterMovementComponent>(&MoveComponent))
	{
		FVector Acceleration = ICharacterMovementComponent->GetCurrentAcceleration();
		if (!Acceleration.IsZero())
		{
			const float AngleTolerance = 5e-2f;
			ForwardVector = ICharacterMovementComponent->RotateToFrom(Acceleration.GetSafeNormal().Rotation(), ForwardVector.Rotation(), AngleTolerance, MovementTickTime).Vector();
		}
			
	}

	FTransform NewTransform = FTransform::Identity;
	NewTransform.SetTranslation(ForwardVector * Strength);
	NewTransform.SetRotation(ForwardVector.Rotation().Quaternion() * Character.GetActorRotation().Quaternion().Inverse());

	// Scale strength of force over time
	if (StrengthOverTime)
	{
		const float TimeValue = Duration > 0.f ? FMath::Clamp(GetTime() / Duration, 0.f, 1.f) : GetTime();
		const float TimeFactor = StrengthOverTime->GetFloatValue(TimeValue);
		NewTransform.ScaleTranslation(TimeFactor);
	}

	// Scale force based on Simulation/MovementTime differences
	// Ex: Force is to go 200 cm per second forward.
	//     To catch up with server state we need to apply
	//     3 seconds of this root motion in 1 second of
	//     movement tick time -> we apply 600 cm for this frame
	const float Multiplier = (MovementTickTime > SMALL_NUMBER) ? (SimulationTime / MovementTickTime) : 1.f;
	NewTransform.ScaleTranslation(Multiplier);

#if ROOT_MOTION_DEBUG
	if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnAnyThread() == 1)
	{
		FString AdjustedDebugString = FString::Printf(TEXT("FRootMotionSource_ForwardForce::PrepareRootMotion NewTransform(%s) Multiplier(%f)"),
			*NewTransform.GetTranslation().ToCompactString(), Multiplier);
		RootMotionSourceDebug::PrintOnScreen(Character, AdjustedDebugString);
	}
#endif

	RootMotionParams.Set(NewTransform);

	SetTime(GetTime() + SimulationTime);
}

bool FRootMotionSource_ForwardForce::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}

	Ar << Strength;
	Ar << StrengthOverTime;

	bOutSuccess = true;
	return true;
}

UScriptStruct* FRootMotionSource_ForwardForce::GetScriptStruct() const
{
	return FRootMotionSource_ForwardForce::StaticStruct();
}

FString FRootMotionSource_ForwardForce::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FRootMotionSource_ForwardForce %s"), LocalID, *InstanceName.GetPlainNameString());
}

void FRootMotionSource_ForwardForce::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(StrengthOverTime);

	FRootMotionSource::AddReferencedObjects(Collector);
}
