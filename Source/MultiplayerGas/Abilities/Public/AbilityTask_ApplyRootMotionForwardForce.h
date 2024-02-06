// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotion_Base.h"
#include "UObject/Class.h"
#include "Engine/NetSerialization.h"
#include "Animation/AnimationAsset.h"
#include "GameFramework/RootMotionSource.h"
#include "AbilityTask_ApplyRootMotionForwardForce.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UCurveFloat;
class UCurveVector;
class UGameplayTasksComponent;
class AActor;

USTRUCT()
struct PATHOFTITANS_API FRootMotionSource_ForwardForce : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

		FRootMotionSource_ForwardForce();

	virtual ~FRootMotionSource_ForwardForce() {}

	UPROPERTY()
	float Strength;

	UPROPERTY()
	TObjectPtr<UCurveFloat> StrengthOverTime;

	virtual FRootMotionSource* Clone() const override;

	virtual bool Matches(const FRootMotionSource* Other) const override;

	virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;

	virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;

	virtual void PrepareRootMotion(
		float SimulationTime,
		float MovementTickTime,
		const ACharacter& Character,
		const UCharacterMovementComponent& MoveComponent
	) override;

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits< FRootMotionSource_ForwardForce > : public TStructOpsTypeTraitsBase2< FRootMotionSource_ForwardForce >
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FApplyRootMotionForwardForceDelegate);


/**
 *	Applies force to character's movement
 */
UCLASS()
class PATHOFTITANS_API UAbilityTask_ApplyRootMotionForwardForce : public UAbilityTask_ApplyRootMotion_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FApplyRootMotionForwardForceDelegate OnFinish;

	/** Apply force to character's movement */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAbilityTask_ApplyRootMotionForwardForce* ApplyRootMotionForwardForce
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
	);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Tick function for this task, if bTickingTask == true */
	virtual void TickTask(float DeltaTime) override;

	virtual void PreDestroyFromReplication() override;
	virtual void OnDestroy(bool AbilityIsEnding) override;

	FORCEINLINE float GetStrength() const { return Strength; }

	void SetStrength(float NewStrength);

	FORCEINLINE float GetDuration() const { return Duration; }

	void SetDuration(float NewDuration);

	FORCEINLINE bool IsAdditive() const { return bIsAdditive; }

	void SetIsAdditive(bool bNewIsAdditive);

	FORCEINLINE UCurveFloat* GetStrengthOverTime() const { return StrengthOverTime; }

	void SetStrengthOverTime(UCurveFloat* NewStrengthOverTime);

	FORCEINLINE bool ShouldEnableGravity() const { return bEnableGravity; }

	void SetEnableGravity(bool bNewEnableGravity);
	
protected:

	virtual void SharedInitAndApply() override;

	UPROPERTY(Replicated)
	float Strength;

	UPROPERTY(Replicated)
	float Duration;

	UPROPERTY(Replicated)
	bool bIsAdditive;

	/** 
	 *  Strength of the force over time
	 *  Curve Y is 0 to 1 which is percent of full Strength parameter to apply
	 *  Curve X is 0 to 1 normalized time if this force has a limited duration (Duration > 0), or
	 *          is in units of seconds if this force has unlimited duration (Duration < 0)
	 */
	UPROPERTY(Replicated)
	UCurveFloat* StrengthOverTime;

	UPROPERTY(Replicated)
	bool bEnableGravity;

};
