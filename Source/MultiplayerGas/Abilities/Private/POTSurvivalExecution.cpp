// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#include "Abilities/POTSurvivalExecution.h"
#include "Abilities/CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/POTAbilityTypes.h"
#include "Abilities/POTAbilitySystemComponent.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "Player/IBaseCharacter.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Online/IGameState.h"
#include "IGameplayStatics.h"

struct POTSurvivalStatics
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(Hunger);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Thirst);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Oxygen);

	DECLARE_ATTRIBUTE_CAPTUREDEF(HungerDepletionRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(ThirstDepletionRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(ThirstReplenishRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(OxygenDepletionRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(OxygenRecoveryRate);

	DECLARE_ATTRIBUTE_CAPTUREDEF(HungerDamage);
	DECLARE_ATTRIBUTE_CAPTUREDEF(ThirstDamage);
	DECLARE_ATTRIBUTE_CAPTUREDEF(OxygenDamage);
	
	DECLARE_ATTRIBUTE_CAPTUREDEF(HealthRecoveryRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(HealthRecoveryMultiplier);
	DECLARE_ATTRIBUTE_CAPTUREDEF(MaxHunger);
	DECLARE_ATTRIBUTE_CAPTUREDEF(MaxThirst);
	DECLARE_ATTRIBUTE_CAPTUREDEF(MaxOxygen);
	DECLARE_ATTRIBUTE_CAPTUREDEF(MaxHealth);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Health);

	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingDamage);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingSurvivalDamage);

	// Statuses
	DECLARE_ATTRIBUTE_CAPTUREDEF(BleedingHealRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(BleedingRate);

	POTSurvivalStatics()
	{
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Hunger, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Thirst, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Oxygen, Target, false);

		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, HungerDepletionRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, ThirstDepletionRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, ThirstReplenishRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, OxygenDepletionRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, OxygenRecoveryRate, Target, false);

		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, HungerDamage, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, ThirstDamage, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, OxygenDamage, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, HealthRecoveryRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, HealthRecoveryMultiplier, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, MaxHunger, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, MaxThirst, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, MaxOxygen, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, MaxHealth, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Health, Target, false);


		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingDamage, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingSurvivalDamage, Target, false);

		// Statuses
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, BleedingHealRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, BleedingRate, Target, false);
	}
};

static const POTSurvivalStatics& SurvivalStatics()
{
	static POTSurvivalStatics DmgStatics;
	return DmgStatics;
}

UPOTSurvivalExecution::UPOTSurvivalExecution()
{
	RelevantAttributesToCapture.Add(SurvivalStatics().HungerDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().ThirstDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().OxygenDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().HungerDepletionRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().ThirstDepletionRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().ThirstReplenishRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().OxygenDepletionRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().OxygenRecoveryRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().HungerDamageDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().ThirstDamageDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().OxygenDamageDef);
	
	RelevantAttributesToCapture.Add(SurvivalStatics().HealthRecoveryRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().HealthRecoveryMultiplierDef);
	
	RelevantAttributesToCapture.Add(SurvivalStatics().MaxHungerDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().MaxThirstDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().HealthDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().MaxHealthDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().MaxOxygenDef);

	// Statuses
	RelevantAttributesToCapture.Add(SurvivalStatics().BleedingHealRateDef);
	RelevantAttributesToCapture.Add(SurvivalStatics().BleedingRateDef);
}

float UPOTSurvivalExecution::ProcessAttribute(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	const FGameplayEffectAttributeCaptureDefinition& BaseAttribute,
	const FGameplayEffectAttributeCaptureDefinition& MaxValueAttribute,
	const FGameplayEffectAttributeCaptureDefinition& AttributeDepletion,
	const FGameplayEffectAttributeCaptureDefinition& AttributeDamage,
	//const FGameplayEffectAttributeCaptureDefinition& IncomingAttributeModification,
	const FAggregatorEvaluateParameters& EvaluationParameters,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput,
	EDamageType DamageType,
	FPOTGameplayEffectContext* POTEffectContext,
	bool bIsRecovery /*= false*/) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	float Base;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		BaseAttribute, EvaluationParameters, Base);


	if (Base > 0.f || bIsRecovery)
	{
		float Depletion;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			AttributeDepletion, EvaluationParameters, Depletion);

		Depletion *= Spec.GetPeriod() * (bIsRecovery ? 1.f : -1.f);

		OutExecutionOutput.
			AddOutputModifier(
				FGameplayModifierEvaluatedData(
					BaseAttribute.AttributeToCapture,
					//IncomingAttributeModification,
					EGameplayModOp::Additive,
					Depletion));
	}
	else
	{
		float Damage;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			AttributeDamage, EvaluationParameters, Damage);

		Damage *= 0.01f; //Turn % into multiplier.


		float Max;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			MaxValueAttribute, EvaluationParameters, Max);

		if (POTEffectContext)
		{
			POTEffectContext->SetDamageType(FMath::Min(DamageType, POTEffectContext->GetDamageType()));
		}

		return Damage * Max * Spec.GetPeriod();
	}

	return 0.f;
}

void UPOTSurvivalExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const UPOTAbilitySystemComponentBase* TargetAbilitySystemComponent = Cast<UPOTAbilitySystemComponentBase>(ExecutionParams.GetTargetAbilitySystemComponent());
	
	AActor* TargetActor = TargetAbilitySystemComponent ? TargetAbilitySystemComponent->GetAvatarActor() : nullptr;

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	FGameplayEffectContextHandle ContextHandle = Spec.GetContext();
	FGameplayEffectContext* GEContext = ContextHandle.Get();

	FPOTGameplayEffectContext* POTEffectContext = GEContext != nullptr 
		? StaticCast<FPOTGameplayEffectContext*>(GEContext) : nullptr;

	// Gather the tags from the source and target as that can affect which buffs should be used
	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float DamageSum = 0.f;

	const AIBaseCharacter* TargetCharacter = Cast<AIBaseCharacter>(TargetActor);

	bool bHomecaveBuff = false;
	bool bIsAquaticSwimming = false;
	bool bHungerThirst = true;
	bool bHealing = false;
	
	if (TargetCharacter)
	{
		bIsAquaticSwimming = TargetCharacter->IsAquatic() && TargetCharacter->IsSwimming();
		bHomecaveBuff = TargetCharacter->IsHomecaveBuffActive();
	}
	
	// Don't allow drowning inside a friendly instance
	const bool bInsideFriendlyInstance = EvaluationParameters.TargetTags->HasTag(UPOTAbilitySystemGlobals::Get().FriendlyInstanceTag);

	// Disable health & thirst depletion and damage inside friendly instances if configured
	if (bInsideFriendlyInstance)
	{
		if (const AIGameState* IGameState = UIGameplayStatics::GetIGameState(TargetActor))
		{
			bHungerThirst = IGameState->GetGameStateFlags().bHungerThirstInCaves;
			bHealing = IGameState->GetGameStateFlags().bHealingInHomeCave;
		}
	}

	if (bHungerThirst && !bHomecaveBuff)
	{
		// Survival Depletion is happening

		DamageSum += ProcessAttribute(ExecutionParams,
			SurvivalStatics().HungerDef,
			SurvivalStatics().MaxHealthDef,
			SurvivalStatics().HungerDepletionRateDef,
			SurvivalStatics().HungerDamageDef,
			//SurvivalStatics().IncomingHungerModificationDef,
			EvaluationParameters, OutExecutionOutput,
			EDamageType::DT_HUNGER,
			POTEffectContext);

		DamageSum += ProcessAttribute(ExecutionParams,
			SurvivalStatics().ThirstDef,
			SurvivalStatics().MaxHealthDef,
			SurvivalStatics().ThirstDepletionRateDef,
			SurvivalStatics().ThirstDamageDef,
			//SurvivalStatics().IncomingThirstModificationDef,
			EvaluationParameters, OutExecutionOutput,
			EDamageType::DT_THIRST,
			POTEffectContext);

		DamageSum += ProcessAttribute(ExecutionParams,
			SurvivalStatics().ThirstDef,
			SurvivalStatics().MaxHealthDef,
			SurvivalStatics().ThirstReplenishRateDef,
			SurvivalStatics().ThirstDamageDef,
			//SurvivalStatics().IncomingThirstModificationDef,
			EvaluationParameters, OutExecutionOutput,
			EDamageType::DT_THIRST,
			POTEffectContext,
			true);
	}
	else
	{
		// Make sure you thirst regenates as aquatic when In tutorial Cave or homecavebuff
		if (bIsAquaticSwimming)
		{
			DamageSum += ProcessAttribute(ExecutionParams,
				SurvivalStatics().ThirstDef,
				SurvivalStatics().MaxHealthDef,
				SurvivalStatics().ThirstReplenishRateDef,
				SurvivalStatics().ThirstDamageDef,
				//SurvivalStatics().IncomingThirstModificationDef,
				EvaluationParameters, OutExecutionOutput,
				EDamageType::DT_THIRST,
				POTEffectContext,
				true);
		}
	}
	
	if (EvaluationParameters.TargetTags->HasTag(UPOTAbilitySystemGlobals::Get().UnderwaterTag) && !bInsideFriendlyInstance)
	{
		DamageSum += ProcessAttribute(ExecutionParams,
			SurvivalStatics().OxygenDef,
			SurvivalStatics().MaxHealthDef,
			SurvivalStatics().OxygenDepletionRateDef,
			SurvivalStatics().OxygenDamageDef,
			//SurvivalStatics().IncomingOxygenModificationDef,
			EvaluationParameters, OutExecutionOutput,
			EDamageType::DT_OXYGEN,
			POTEffectContext);
	}

	float CurrentHunger = 0;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SurvivalStatics().HungerDef, EvaluationParameters, CurrentHunger);

	float CurrentThirst = 0;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SurvivalStatics().ThirstDef, EvaluationParameters, CurrentThirst);

	float CurrentBleedingRate = 0;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SurvivalStatics().BleedingRateDef, EvaluationParameters, CurrentBleedingRate);

	float HealRate = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		SurvivalStatics().HealthRecoveryRateDef, EvaluationParameters, HealRate);
	
	const bool bDebuffHealthRecoveryBlocked = EvaluationParameters.TargetTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_DebuffHealthRecoveryBlocked));
	const bool bBuffHealing = EvaluationParameters.TargetTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_BuffHeal));
	const bool bExitHomecaveBuff = EvaluationParameters.TargetTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_BuffHomecaveExitSafety));
	bool bShouldBlockHeal = bShouldBlockHeal = bDebuffHealthRecoveryBlocked && (!bBuffHealing || bExitHomecaveBuff) && !bHealing;

	if (!bShouldBlockHeal && TargetCharacter)
	{
		if (const AIPlayerCaveBase* TargetInstance = TargetCharacter->GetCurrentInstance())
		{
			if (!TargetInstance->AllowsHealing()) bShouldBlockHeal = true;
		}
	}

	if (HealRate > 0.f && CurrentHunger > 0 && CurrentThirst > 0 && CurrentBleedingRate == 0 && !bShouldBlockHeal)
	{
		float MaxHunger = 0;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			SurvivalStatics().MaxHungerDef, EvaluationParameters, MaxHunger);

		float MaxThirst = 0;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			SurvivalStatics().MaxThirstDef, EvaluationParameters, MaxThirst);

		const float HungerPercent = MaxHunger > 0.f ? CurrentHunger / MaxHunger : 1.f;
		const float ThirstPercent = MaxThirst > 0.f ? CurrentThirst / MaxThirst : 1.f;
		float HealRatio = ((HungerPercent + ThirstPercent) / 2);
		
		if (EvaluationParameters.TargetTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_BuffHeal)))
		{
			// healing should be unscaled if player is being healed from a healing buff
			HealRatio = 1.0f;
		}

		float HealMultiplier = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			SurvivalStatics().HealthRecoveryMultiplierDef, EvaluationParameters, HealMultiplier);

		if (HealRate > 0)
		{
			HealRate *= HealMultiplier;
		}

		OutExecutionOutput.
			AddOutputModifier(
				FGameplayModifierEvaluatedData(
					SurvivalStatics().HealthProperty,
					EGameplayModOp::Additive,
					HealRate * HealRatio));
	}

	const float EventMagnitude = POTEffectContext != nullptr ? POTEffectContext->GetEventMagnitude() : 1.f;
	DamageSum *= EventMagnitude;
	
	OutExecutionOutput.
		AddOutputModifier(
			FGameplayModifierEvaluatedData(
				SurvivalStatics().IncomingSurvivalDamageProperty,
				EGameplayModOp::Additive, 
				DamageSum));
}