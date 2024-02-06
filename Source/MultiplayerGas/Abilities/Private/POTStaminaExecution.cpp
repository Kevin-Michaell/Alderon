// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.


#include "Abilities/POTStaminaExecution.h"
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

struct POTStaminaStatics
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(PoisonHealRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(VenomHealRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(StaminaRecoveryRate);

	DECLARE_ATTRIBUTE_CAPTUREDEF(PoisonRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(VenomRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Stamina);

	POTStaminaStatics()
	{
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, PoisonHealRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, VenomHealRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, StaminaRecoveryRate, Target, false);

		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, PoisonRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, VenomRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Stamina, Target, false);
	}
};

static const POTStaminaStatics& StaminaStatics()
{
	static POTStaminaStatics DmgStatics;
	return DmgStatics;
}

UPOTStaminaExecution::UPOTStaminaExecution()
{
	RelevantAttributesToCapture.Add(StaminaStatics().PoisonHealRateDef);
	RelevantAttributesToCapture.Add(StaminaStatics().PoisonRateDef);
	RelevantAttributesToCapture.Add(StaminaStatics().VenomHealRateDef);
	RelevantAttributesToCapture.Add(StaminaStatics().VenomRateDef);
	RelevantAttributesToCapture.Add(StaminaStatics().StaminaDef);
	RelevantAttributesToCapture.Add(StaminaStatics().StaminaRecoveryRateDef);
}

void UPOTStaminaExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	UPOTAbilitySystemComponentBase* TargetAbilitySystemComponent = Cast<UPOTAbilitySystemComponentBase>(ExecutionParams.GetTargetAbilitySystemComponent());
	UPOTAbilitySystemComponentBase* SourceAbilitySystemComponent = Cast<UPOTAbilitySystemComponentBase>(ExecutionParams.GetSourceAbilitySystemComponent());

	AActor* SourceActor = SourceAbilitySystemComponent ? SourceAbilitySystemComponent->GetAvatarActor() : nullptr;
	AActor* TargetActor = TargetAbilitySystemComponent ? TargetAbilitySystemComponent->GetAvatarActor() : nullptr;

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	FGameplayEffectContextHandle ContextHandle = Spec.GetContext();
	FGameplayEffectContext* GEContext = ContextHandle.Get();

	FPOTGameplayEffectContext* POTEffectContext = GEContext != nullptr
		? StaticCast<FPOTGameplayEffectContext*>(GEContext) : nullptr;

	// Gather the tags from the source and target as that can affect which buffs should be used
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;


	float CurrentPoisonRate = 0;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		StaminaStatics().PoisonRateDef, EvaluationParameters, CurrentPoisonRate);

	float CurrentVenomRate = 0;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		StaminaStatics().VenomRateDef, EvaluationParameters, CurrentVenomRate);

	float HealRate = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		StaminaStatics().StaminaRecoveryRateDef, EvaluationParameters, HealRate);


	if (HealRate > 0.f && CurrentVenomRate == 0 && CurrentPoisonRate == 0)
	{
		OutExecutionOutput.
			AddOutputModifier(
				FGameplayModifierEvaluatedData(
					StaminaStatics().StaminaProperty,
					EGameplayModOp::Additive,
					HealRate));
	}
}

