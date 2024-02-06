// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotion_Base.h"
#include "Animation/AnimationAsset.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/RootMotionSource.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "ApplyRootMotion_BreachBase.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UCurveFloat;
class UCurveVector;
class UGameplayTasksComponent;
class AActor;

USTRUCT()
struct PATHOFTITANS_API FRootMotionSource_BreachBase : public FRootMotionSource
{
	GENERATED_USTRUCT_BODY()

	FRootMotionSource_BreachBase();
	virtual ~FRootMotionSource_BreachBase() {}

	UPROPERTY()
	float Strength = 0.0f;

	UPROPERTY()
	float AbilityAcceleration = 0.0f;

	UPROPERTY()
	float RotationSpeed = 80.0f;

	virtual bool Matches(const FRootMotionSource* Other) const override;

	virtual bool MatchesAndHasSameState(const FRootMotionSource* Other) const override;

	virtual bool UpdateStateFrom(const FRootMotionSource* SourceToTakeStateFrom, bool bMarkForSimulatedCatchup = false) override;

	virtual void PrepareRootMotion(
		float SimulationTime,
		float MovementTickTime,
		const ACharacter& Character,
		const UCharacterMovementComponent& MoveComponent
	) override;

	virtual FTransform PrepareBreachMotion(
		float SimulationTime,
		float MovementTickTime,
		const ACharacter& Character,
		const UCharacterMovementComponent& MoveComponent) 
	{ensureMsgf(false, TEXT("Please Override this function in a child class")); return FTransform();};

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits< FRootMotionSource_BreachBase > : public TStructOpsTypeTraitsBase2< FRootMotionSource_BreachBase >
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FApplyRootMotionBreachStageDelegate, bool, bShouldContinue);

/**
 *	Applies force to character's movement
 */
UCLASS()
class PATHOFTITANS_API UApplyRootMotion_BreachBase : public UAbilityTask_ApplyRootMotion_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(BlueprintAssignable)
	FApplyRootMotionBreachStageDelegate OnFinish;

	static void InitializeTask(
		UApplyRootMotion_BreachBase* MyTask,
		UGameplayAbility* OwningAbility,
		FName TaskInstanceName,
		float Strength,
		float Duration,
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

	UFUNCTION(BlueprintCallable)
	void CompleteBreachStage(bool bSuccess = true);

	FORCEINLINE float GetStrength() const { return Strength; }

	void SetStrength(float NewStrength);

	FORCEINLINE float GetDuration() const { return Duration; }

	void SetDuration(float NewDuration);

	FORCEINLINE bool ShouldEnableGravity() const { return bEnableGravity; }

	void SetEnableGravity(bool bNewEnableGravity);

protected:

	virtual void SharedInitAndApply() override;

	virtual TSharedPtr<FRootMotionSource_BreachBase> MakeSharedBreachMovementSource()
	{
		ensureMsgf(false, TEXT("Please override this function in child classes"));
		ForceName = ForceName.IsNone() ? FName("AbilityTaskApplyRootMotionBreachBase") : ForceName;
		return MakeShared<FRootMotionSource_BreachBase>();
	}
	
	UPROPERTY(Replicated)
	float Strength;

	UPROPERTY(Replicated)
	float Duration;

	UPROPERTY(Replicated)
	bool bEnableGravity;
};

USTRUCT()
struct PATHOFTITANS_API FRootMotionSource_BreachRise : public FRootMotionSource_BreachBase
{
	GENERATED_USTRUCT_BODY()

	virtual FTransform PrepareBreachMotion(
		float SimulationTime,
		float MovementTickTime,
		const ACharacter& Character,
		const UCharacterMovementComponent& MoveComponent
	) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual FRootMotionSource* Clone() const override;

};

template<>
struct TStructOpsTypeTraits< FRootMotionSource_BreachRise > : public TStructOpsTypeTraitsBase2< FRootMotionSource_BreachRise >
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

/**
 *	Applies force to character's movement
 */
UCLASS()
class PATHOFTITANS_API UApplyRootMotion_BreachRise : public UApplyRootMotion_BreachBase
{
	GENERATED_UCLASS_BODY()

	public:

		/** Tick function for this task, if bTickingTask == true */
	virtual void TickTask(float DeltaTime) override;

	/** Apply force to character's movement */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", DeterminesOutputType = "RootMotion_BreachStage", BlueprintInternalUseOnly = "TRUE"))
		static UApplyRootMotion_BreachRise* ApplyRootMotionBreachRise
		(
			UGameplayAbility* OwningAbility,
			FName TaskInstanceName,
			float InStrength,
			float InDuration,
			ERootMotionFinishVelocityMode VelocityOnFinishMode,
			FVector SetVelocityOnFinish,
			float ClampVelocityOnFinish,
			bool bInEnableGravity
		);

protected:

	virtual TSharedPtr<FRootMotionSource_BreachBase> MakeSharedBreachMovementSource() override;

};

USTRUCT()
struct PATHOFTITANS_API FRootMotionSource_BreachJump : public FRootMotionSource_BreachBase
{
	GENERATED_USTRUCT_BODY()

		virtual FTransform PrepareBreachMotion(
			float SimulationTime,
			float MovementTickTime,
			const ACharacter& Character,
			const UCharacterMovementComponent& MoveComponent
		) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	FRotator fallRotation = FRotator::ZeroRotator;

	virtual FRootMotionSource* Clone() const override;

};

template<>
struct TStructOpsTypeTraits< FRootMotionSource_BreachJump> : public TStructOpsTypeTraitsBase2< FRootMotionSource_BreachJump>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

UCLASS()
class PATHOFTITANS_API UApplyRootMotion_BreachJump : public UApplyRootMotion_BreachBase
{
	GENERATED_UCLASS_BODY()

	public:

		/** Tick function for this task, if bTickingTask == true */
	virtual void TickTask(float DeltaTime) override;

	/** Apply force to character's movement */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", DeterminesOutputType = "RootMotion_BreachStage", BlueprintInternalUseOnly = "TRUE"))
		static UApplyRootMotion_BreachJump* ApplyRootMotionBreachJump
		(
			UGameplayAbility* OwningAbility,
			FName TaskInstanceName,
			float InStrength,
			float InDuration,
			ERootMotionFinishVelocityMode VelocityOnFinishMode,
			FVector SetVelocityOnFinish,
			float ClampVelocityOnFinish,
			bool bInEnableGravity
		);

protected:

	virtual TSharedPtr<FRootMotionSource_BreachBase> MakeSharedBreachMovementSource() override;

}; 


USTRUCT()
struct PATHOFTITANS_API FRootMotionSource_BreachFall : public FRootMotionSource_BreachBase
{
	GENERATED_USTRUCT_BODY()

		virtual FTransform PrepareBreachMotion(
			float SimulationTime,
			float MovementTickTime,
			const ACharacter& Character,
			const UCharacterMovementComponent& MoveComponent
		) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	FRotator fallRotation = FRotator::ZeroRotator;

	virtual FRootMotionSource* Clone() const override;

};

template<>
struct TStructOpsTypeTraits< FRootMotionSource_BreachFall> : public TStructOpsTypeTraitsBase2< FRootMotionSource_BreachFall>
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};

UCLASS()
class PATHOFTITANS_API UApplyRootMotion_BreachFall : public UApplyRootMotion_BreachBase
{
	GENERATED_UCLASS_BODY()

public:

	/** Tick function for this task, if bTickingTask == true */
	virtual void TickTask(float DeltaTime) override;

	/** Apply force to character's movement */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", DeterminesOutputType = "RootMotion_BreachStage", BlueprintInternalUseOnly = "TRUE"))
		static UApplyRootMotion_BreachFall* ApplyRootMotionBreachFall
		(
			UGameplayAbility* OwningAbility,
			FName TaskInstanceName,
			float InStrength,
			float InDuration,
			ERootMotionFinishVelocityMode VelocityOnFinishMode,
			FVector SetVelocityOnFinish,
			float ClampVelocityOnFinish,
			bool bInEnableGravity
		);

protected:

	virtual TSharedPtr<FRootMotionSource_BreachBase> MakeSharedBreachMovementSource() override;

};

