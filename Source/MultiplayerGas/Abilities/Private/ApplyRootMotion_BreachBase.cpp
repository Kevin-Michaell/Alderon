// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#include "Abilities/ApplyRootMotion_BreachBase.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/ICharacterMovementComponent.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Player/Dinosaurs/IDinosaurCharacter.h"
#include "Player/IBaseCharacter.h"
#include "AbilitySystemLog.h"

UApplyRootMotion_BreachBase::UApplyRootMotion_BreachBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UApplyRootMotion_BreachBase::InitializeTask
(
	UApplyRootMotion_BreachBase* MyTask,
	UGameplayAbility* OwningAbility,
	FName TaskInstanceName,
	float Strength,
	float Duration,
	ERootMotionFinishVelocityMode VelocityOnFinishMode,
	FVector SetVelocityOnFinish,
	float ClampVelocityOnFinish,
	bool bEnableGravity
)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Duration(Duration);

	MyTask->ForceName = TaskInstanceName;
	MyTask->SetStrength(Strength);
	MyTask->SetDuration(Duration);
	MyTask->FinishVelocityMode = VelocityOnFinishMode;
	MyTask->FinishSetVelocity = SetVelocityOnFinish;
	MyTask->FinishClampVelocity = ClampVelocityOnFinish;
	MyTask->SetEnableGravity(bEnableGravity);
	MyTask->SharedInitAndApply();
}

void UApplyRootMotion_BreachBase::SetStrength(float NewStrength)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UApplyRootMotion_BreachBase, Strength, NewStrength, this);
}

void UApplyRootMotion_BreachBase::SetDuration(float NewDuration)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UApplyRootMotion_BreachBase, Duration, NewDuration, this);
}

void UApplyRootMotion_BreachBase::SetEnableGravity(bool bNewEnableGravity)
{
	COMPARE_ASSIGN_AND_MARK_PROPERTY_DIRTY(UApplyRootMotion_BreachBase, bEnableGravity, bNewEnableGravity, this);
}

void UApplyRootMotion_BreachBase::SharedInitAndApply()
{
	if (AbilitySystemComponent->AbilityActorInfo->MovementComponent.IsValid())
	{
		MovementComponent = Cast<UCharacterMovementComponent>(AbilitySystemComponent->AbilityActorInfo->MovementComponent.Get());
		StartTime = GetWorld()->GetTimeSeconds();
		EndTime = StartTime + GetDuration();

		if (MovementComponent)
		{
			TSharedPtr<FRootMotionSource_BreachBase> BreachForce = MakeSharedBreachMovementSource();
			BreachForce->InstanceName = ForceName;
			BreachForce->AccumulateMode = ERootMotionAccumulateMode::Override;
			BreachForce->Priority = 5;
			BreachForce->Strength = GetStrength();
			BreachForce->Duration = GetDuration();
			BreachForce->FinishVelocityParams.Mode = FinishVelocityMode;
			BreachForce->FinishVelocityParams.SetVelocity = FinishSetVelocity;
			BreachForce->FinishVelocityParams.ClampVelocity = FinishClampVelocity;
			if (ShouldEnableGravity())
			{
				BreachForce->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
			}

			if (GetStrength() != 0.0f && GetAvatarActor()) 
			{
				BreachForce->AbilityAcceleration = GetAvatarActor()->GetVelocity().Size() / GetStrength();
			}
			else 
			{
				BreachForce->AbilityAcceleration = 1.0f;
			}

			RootMotionSourceID = MovementComponent->ApplyRootMotionSource(BreachForce);
		}
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("UApplyRootMotion_BreachBase called in Ability %s with null MovementComponent; Task Instance Name %s."),
			Ability ? *Ability->GetName() : TEXT("NULL"),
			*InstanceName.ToString());
	}
}

void UApplyRootMotion_BreachBase::TickTask(float DeltaTime)
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
			CompleteBreachStage(false);
		}
	}
	else
	{
		CompleteBreachStage(false);
	}
}

void UApplyRootMotion_BreachBase::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params{};
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UApplyRootMotion_BreachBase, Strength, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UApplyRootMotion_BreachBase, Duration, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UApplyRootMotion_BreachBase, bEnableGravity, Params);
}

void UApplyRootMotion_BreachBase::PreDestroyFromReplication()
{
	CompleteBreachStage();
}

void UApplyRootMotion_BreachBase::CompleteBreachStage(bool bSuccessful)
{
	#if WITH_EDITOR
	if (MovementComponent && MovementComponent->GetRootMotionSource(ForceName))
	{
		FString str = "Completed Stage: " + MovementComponent->GetRootMotionSource(ForceName)->ToSimpleString() + (bSuccessful ? "Success" : "Failed");
		UE_LOG(LogTemp, Log, TEXT("%s"), *str);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, str);
	}
	#endif

	bIsFinished = true;
	if (!bIsSimulating)
	{
		if (AActor* Dino = GetAvatarActor())
		{
			FRotator Rot = Dino->GetActorRotation();
			Rot.Roll = 0.0f;
			Dino->SetActorRotation(Rot);
			
			Dino->ForceNetUpdate();
		}

		if (ShouldBroadcastAbilityTaskDelegates())
		{
			// If we are in the water and we time out we don't want to play the 
			// effects associated with rising out of the water or spraying from the nose
			OnFinish.Broadcast(bSuccessful);
		}
	}
	EndTask();
}

void UApplyRootMotion_BreachBase::OnDestroy(bool AbilityIsEnding)
{
	if (MovementComponent)
	{
		MovementComponent->RemoveRootMotionSourceByID(RootMotionSourceID);
	}

	Super::OnDestroy(AbilityIsEnding);
}

//
// UApplyRootMotion_BreachRise
//

UApplyRootMotion_BreachRise::UApplyRootMotion_BreachRise(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UApplyRootMotion_BreachRise::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	if (bIsFinished)
	{
		return;
	}


	if (AIBaseCharacter* Dino = Cast<AIBaseCharacter>(GetAvatarActor()))
	{
		if (!Dino->IsInWater(1.0f))
		{
			CompleteBreachStage(true);
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Error: The breach Avatar was not a AIBaseCharacter, we are completing the Breach Rise early"));
		CompleteBreachStage(false);
	}
}

UApplyRootMotion_BreachRise* UApplyRootMotion_BreachRise::ApplyRootMotionBreachRise
(
	UGameplayAbility* OwningAbility,
	FName TaskInstanceName,
	float InStrength,
	float InDuration,
	ERootMotionFinishVelocityMode VelocityOnFinishMode,
	FVector SetVelocityOnFinish,
	float ClampVelocityOnFinish,
	bool bInEnableGravity
)
{
	UApplyRootMotion_BreachRise* Task = NewAbilityTask<UApplyRootMotion_BreachRise>(OwningAbility, TaskInstanceName);

	InitializeTask(Task, OwningAbility, TaskInstanceName, InStrength, InDuration, VelocityOnFinishMode, SetVelocityOnFinish, ClampVelocityOnFinish, bInEnableGravity);

	return Task;
}

TSharedPtr<FRootMotionSource_BreachBase> UApplyRootMotion_BreachRise::MakeSharedBreachMovementSource()
{
	this->ForceName = this->ForceName.IsNone() ? FName("AbilityTaskApplyRootMotionBreachRise") : this->ForceName;
	return MakeShared<FRootMotionSource_BreachRise>();	
}

//
// FRootMotionSource_BreachBase
//

FRootMotionSource_BreachBase::FRootMotionSource_BreachBase()
	: Strength(0)
{
	// Disable Partial End Tick for Constant Forces.
	// Otherwise we end up with very inconsistent velocities on the last frame.
	// This ensures that the ending velocity is maintained and consistent.
	//Settings.SetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
}

FRootMotionSource* FRootMotionSource_BreachRise::Clone() const
{
	FRootMotionSource_BreachRise* CopyPtr = new FRootMotionSource_BreachRise(*this);
	return CopyPtr;
}

FRootMotionSource* FRootMotionSource_BreachJump::Clone() const
{
	FRootMotionSource_BreachJump* CopyPtr = new FRootMotionSource_BreachJump(*this);
	return CopyPtr;
}


FRootMotionSource* FRootMotionSource_BreachFall::Clone() const
{
	FRootMotionSource_BreachFall* CopyPtr = new FRootMotionSource_BreachFall(*this);
	return CopyPtr;
}

bool FRootMotionSource_BreachBase::Matches(const FRootMotionSource* Other) const
{
	if (!FRootMotionSource::Matches(Other))
	{
		return false;
	}

	// We can cast safely here since in FRootMotionSource::Matches() we ensured ScriptStruct equality
	const FRootMotionSource_BreachBase* OtherCast = static_cast<const FRootMotionSource_BreachBase*>(Other);

	return FMath::Abs(OtherCast->Strength - Strength) < 0.1f 
		&& OtherCast->Settings.Flags == Settings.Flags;
}

bool FRootMotionSource_BreachBase::MatchesAndHasSameState(const FRootMotionSource* Other) const
{
	// Check that it matches
	if (!FRootMotionSource::MatchesAndHasSameState(Other))
	{
		return false;
	}

	return true; // ForwardForce has no unique state
}

bool FRootMotionSource_BreachBase::UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup)
{
	if (!FRootMotionSource::UpdateStateFrom(SourceToTakeStateFrom, bMarkForSimulatedCatchup))
	{
		return false;
	}

	const FRootMotionSource_BreachBase* OtherCast = static_cast<const FRootMotionSource_BreachBase*>(SourceToTakeStateFrom);

	return true;
}

void FRootMotionSource_BreachBase::PrepareRootMotion
(
	float SimulationTime,
	float MovementTickTime,
	const ACharacter& Character,
	const UCharacterMovementComponent& MoveComponent
)
{
	//if (!(Character.HasAuthority() || Character.IsLocallyControlled()))
	//{
	//	FTransform NewTransform = FTransform(MoveComponent.GetLastUpdateVelocity());
	//	RootMotionParams.Set(NewTransform);
	//	SetTime(GetTime() + SimulationTime);
	//	return;
	//}

	RootMotionParams.Clear();

	FTransform NewTransform = PrepareBreachMotion(SimulationTime, MovementTickTime, Character, MoveComponent);

	// Scale force based on Simulation/MovementTime differences
	// Ex: Force is to go 200 cm per second forward.
	//     To catch up with server state we need to apply
	//     3 seconds of this root motion in 1 second of
	//     movement tick time -> we apply 600 cm for this frame
	const float Multiplier = (MovementTickTime > SMALL_NUMBER) ? (SimulationTime / MovementTickTime) : 1.f;
	NewTransform.ScaleTranslation(Multiplier);

#if WITH_EDITOR
	//if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnAnyThread() == 1)
	{
		FString AdjustedDebugString = FString::Printf(TEXT("%s NewTransform(%s) Multiplier(%f)"), 
		    *ToSimpleString(),
			*NewTransform.GetTranslation().ToCompactString(), Multiplier);

		UE_LOG(LogTemp, Log, TEXT("%s"), *AdjustedDebugString);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 2.0f, FColor::Red, AdjustedDebugString);
		}
	}
#endif

	RootMotionParams.Set(NewTransform);

	SetTime(GetTime() + SimulationTime);
}

bool FRootMotionSource_BreachBase::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (!FRootMotionSource::NetSerialize(Ar, Map, bOutSuccess))
	{
		return false;
	}

	Ar << Strength;
	Ar << AbilityAcceleration;

	bOutSuccess = true;
	return true;
}

UScriptStruct* FRootMotionSource_BreachBase::GetScriptStruct() const
{
	return FRootMotionSource_BreachBase::StaticStruct();
}

FString FRootMotionSource_BreachBase::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FRootMotionSource_BreachBase %s"), LocalID, *InstanceName.GetPlainNameString());
}

void FRootMotionSource_BreachBase::AddReferencedObjects(class FReferenceCollector& Collector)
{
	FRootMotionSource::AddReferencedObjects(Collector);
}


//
// FRootMotionSource_BreachRise
//

FTransform FRootMotionSource_BreachRise::PrepareBreachMotion
(
	float SimulationTime,
	float MovementTickTime,
	const ACharacter& Character,
	const UCharacterMovementComponent& MoveComponent
)
{
	FVector ForwardVector;
	if (Character.GetController())
	{
		ForwardVector = Character.GetControlRotation().Vector();
	}
	else if (const AIBaseCharacter* IPawn = Cast<AIBaseCharacter>(&Character))
	{
		ensure(false); // not used
		//ForwardVector = IPawn->GetReplicatedControlRotation().Vector();
	}
	// Dinos sink, so they may have a velocity that is -9 for gravity
	FVector VelocityVector = MoveComponent.Velocity.IsNearlyZero(10.0)
									? Character.GetActorForwardVector() 
									: MoveComponent.Velocity.GetSafeNormal();

	FRotator VelocityRotator = VelocityVector.Rotation();
	VelocityRotator.Pitch = FMath::Clamp(VelocityRotator.Pitch, -80.0f, 80.0f);

	FTransform NewTransform = FTransform::Identity;

	if (AbilityAcceleration < 1.0)
	{
		AbilityAcceleration += MovementTickTime;
		NewTransform.SetTranslation(Character.GetActorForwardVector() * Strength * AbilityAcceleration);
		return NewTransform;
	}
	if (AbilityAcceleration > 1.0)
	{
		AbilityAcceleration = 1.0f;
	}

	if (const UICharacterMovementComponent* ICharacterMovementComponent = Cast<UICharacterMovementComponent>(&MoveComponent))
	{
		const float AngleTolerance = 5e-2f;
		ForwardVector = ICharacterMovementComponent->RotateToFrom(ForwardVector.Rotation(), VelocityRotator, AngleTolerance, MovementTickTime).Vector();
	}

	NewTransform.SetTranslation(ForwardVector * Strength * AbilityAcceleration);
	NewTransform.SetRotation(VelocityRotator.Quaternion() * Character.GetActorForwardVector().Rotation().Quaternion().Inverse());	

    return NewTransform;
}

UScriptStruct* FRootMotionSource_BreachRise::GetScriptStruct() const
{
	return FRootMotionSource_BreachRise::StaticStruct();
}

FString FRootMotionSource_BreachRise::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FRootMotionSource_BreachRise %s"), LocalID, *InstanceName.GetPlainNameString());
}

//
// Breach Jump
//

UApplyRootMotion_BreachJump::UApplyRootMotion_BreachJump(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	//Intentionally Empty
}

void UApplyRootMotion_BreachJump::TickTask(float DeltaTime)
{
	if (bIsFinished)
	{
		return;
	}

	Super::TickTask(DeltaTime);

	if (AIBaseCharacter* Dino = Cast<AIBaseCharacter>(GetAvatarActor()))
	{
		if (Dino->GetVelocity().Z <= 0.0f || Dino->GetVelocity().IsNearlyZero())
		{
			CompleteBreachStage(true);
		}
		else if (Dino->IsWalking() || MovementComponent->IsMovingOnGround())
		{
			CompleteBreachStage(false);
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Error: The breach Avatar was not a AIBaseCharacter, we are completing the Jump early"));
		CompleteBreachStage(false);
	}
}

UApplyRootMotion_BreachJump* UApplyRootMotion_BreachJump::ApplyRootMotionBreachJump
(
	UGameplayAbility* OwningAbility,
	FName TaskInstanceName,
	float InStrength,
	float InDuration,
	ERootMotionFinishVelocityMode VelocityOnFinishMode,
	FVector SetVelocityOnFinish,
	float ClampVelocityOnFinish,
	bool bInEnableGravity
)
{
	if (!ensureMsgf(InStrength <= 1.0f, TEXT("The strength should not be greater than 1 as we are using the previous velocity to calculate this velocity")))
	{
		InStrength = 1.0f;
	}

	UApplyRootMotion_BreachJump* Task = NewAbilityTask<UApplyRootMotion_BreachJump>(OwningAbility, TaskInstanceName);

	InitializeTask(Task, OwningAbility, TaskInstanceName, InStrength, InDuration, VelocityOnFinishMode, SetVelocityOnFinish, ClampVelocityOnFinish, bInEnableGravity);

	return Task;
}

TSharedPtr<FRootMotionSource_BreachBase> UApplyRootMotion_BreachJump::MakeSharedBreachMovementSource()
{
	this->ForceName = this->ForceName.IsNone() ? FName("AbilityTaskApplyRootMotionBreachJump") : this->ForceName;
	TSharedPtr<FRootMotionSource_BreachJump> BreachJump = MakeShared<FRootMotionSource_BreachJump>();
	BreachJump->Settings.SetFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck);
	return BreachJump;
}

//
// RootMotion Breach Jump
//

FTransform FRootMotionSource_BreachJump::PrepareBreachMotion
(
	float SimulationTime,
	float MovementTickTime,
	const ACharacter& Character,
	const UCharacterMovementComponent& MoveComponent
)
{
	if (fallRotation == FRotator::ZeroRotator)
	{
		fallRotation = Character.GetActorForwardVector().Rotation();
	}

	FVector UpVector = MoveComponent.Velocity;

	FTransform NewTransform = FTransform::Identity;
	NewTransform.SetTranslation(UpVector * Strength);

	FRotator CharRotator = Character.GetActorForwardVector().Rotation();

	const float AngleTolerance = 5e-2f;
	FRotator MovementDirection = FMath::RInterpConstantTo(fallRotation, UpVector.Rotation(), MovementTickTime, RotationSpeed);
	fallRotation = MovementDirection;

	#if WITH_EDITOR
	GEngine->AddOnScreenDebugMessage(859195, 0.0f, FColor::Blue, FString::SanitizeFloat(MovementDirection.Pitch));
	#endif

	NewTransform.SetRotation(MovementDirection.Quaternion() * CharRotator.Quaternion().Inverse());

	return NewTransform;
}

UScriptStruct* FRootMotionSource_BreachJump::GetScriptStruct() const
{
	return FRootMotionSource_BreachJump::StaticStruct();
}

FString FRootMotionSource_BreachJump::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FRootMotionSource_BreachJump %s"), LocalID, *InstanceName.GetPlainNameString());
}

FTransform FRootMotionSource_BreachFall::PrepareBreachMotion(float SimulationTime, float MovementTickTime, const ACharacter& Character, const UCharacterMovementComponent& MoveComponent)
{
	if (fallRotation == FRotator::ZeroRotator)
	{
		fallRotation = Character.GetActorForwardVector().Rotation();
	}

	FVector UpVector = MoveComponent.Velocity;

	FTransform NewTransform = FTransform::Identity;
	NewTransform.SetTranslation(UpVector * Strength);

	//Char rot is inaccurate, and is being clamped somehow
	FRotator CharRotator = Character.GetActorForwardVector().Rotation();

	const float AngleTolerance = 5e-2f;
	FRotator MovementDirection = FMath::RInterpConstantTo(fallRotation, UpVector.Rotation(), MovementTickTime, RotationSpeed);
	fallRotation = MovementDirection;

	FRotator UpRot = UpVector.Rotation();

	#if WITH_EDITOR
	GEngine->AddOnScreenDebugMessage(859195, 0.0f, FColor::Blue, FString::SanitizeFloat(MovementDirection.Pitch));
	#endif

	FQuat4d finalQuat = MovementDirection.Quaternion() * CharRotator.Quaternion().Inverse();
	FRotator finalRotator = finalQuat.Rotator();

	NewTransform.SetRotation(finalQuat);

	return NewTransform;
}

UScriptStruct* FRootMotionSource_BreachFall::GetScriptStruct() const
{
	return FRootMotionSource_BreachFall::StaticStruct();
}

FString FRootMotionSource_BreachFall::ToSimpleString() const
{
	return FString::Printf(TEXT("[ID:%u]FRootMotionSource_BreachFall %s"), LocalID, *InstanceName.GetPlainNameString());
}

UApplyRootMotion_BreachFall::UApplyRootMotion_BreachFall(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	//Intentionally Empty
}

void UApplyRootMotion_BreachFall::TickTask(float DeltaTime)
{
	if (bIsFinished)
	{
		return;
	}

	Super::TickTask(DeltaTime);

	if (AIBaseCharacter* Dino = Cast<AIBaseCharacter>(GetAvatarActor()))
	{
		if (Dino->IsInWater(1.0f))
		{
			CompleteBreachStage(true);
		}
		else if (Dino->IsWalking() || Dino->IsSprinting())
		{
			FRotator Rot = Dino->GetActorRotation();
			Rot.Pitch = 0.0f;
			Dino->SetActorRotation(Rot);
			CompleteBreachStage(false);
		}
	}
	else
	{
		ensureMsgf(false, TEXT("Error: The breach Avatar was not a AIBaseCharacter, we are completing the fall early"));
		CompleteBreachStage(false);
	}
}

UApplyRootMotion_BreachFall* UApplyRootMotion_BreachFall::ApplyRootMotionBreachFall(UGameplayAbility* OwningAbility, FName TaskInstanceName, float InStrength, float InDuration, ERootMotionFinishVelocityMode VelocityOnFinishMode, FVector SetVelocityOnFinish, float ClampVelocityOnFinish, bool bInEnableGravity)
{
	if (!ensureMsgf(InStrength <= 1.0f, TEXT("The strength should not be greater than 1 as we are using the previous velocity to calculate this velocity")))
	{
		InStrength = 1.0f;
	}
	
	UApplyRootMotion_BreachFall* Task = NewAbilityTask<UApplyRootMotion_BreachFall>(OwningAbility, TaskInstanceName);
	
	InitializeTask(Task, OwningAbility, TaskInstanceName, InStrength, InDuration, VelocityOnFinishMode, SetVelocityOnFinish, ClampVelocityOnFinish, bInEnableGravity);

	return Task;
}

TSharedPtr<FRootMotionSource_BreachBase> UApplyRootMotion_BreachFall::MakeSharedBreachMovementSource()
{
	this->ForceName = this->ForceName.IsNone() ? FName("AbilityTaskApplyRootMotionBreachFall") : this->ForceName;
	TSharedPtr<FRootMotionSource_BreachFall> BreachFall = MakeShared<FRootMotionSource_BreachFall>();
	return BreachFall;
}
