#include "Abilities/POTStaminaRegenMMC.h"
#include "Abilities/CoreAttributeSet.h"

// Sets default values
UPOTStaminaRegenMMC::UPOTStaminaRegenMMC()
	: Super()
{
	// Define Attribute we want to capture
	// No snapshot in thise case since we want current value at each application.
	StaminaRecoveryRateAttribute.AttributeToCapture = UCoreAttributeSet::GetStaminaRecoveryRateAttribute();
	StaminaRecoveryRateAttribute.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	StaminaRecoveryRateAttribute.bSnapshot = false;
	RelevantAttributesToCapture.Add(StaminaRecoveryRateAttribute);

	StaminaRecoveryMultiplierAttribute.AttributeToCapture = UCoreAttributeSet::GetStaminaRecoveryMultiplierAttribute();
	StaminaRecoveryMultiplierAttribute.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	StaminaRecoveryMultiplierAttribute.bSnapshot = false;
	RelevantAttributesToCapture.Add(StaminaRecoveryMultiplierAttribute);
}

float UPOTStaminaRegenMMC::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	// Gather the tags from the source and target as that can affect which buffs should be used
	FAggregatorEvaluateParameters EvaluationParameters{};
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float StaminaRecovery = 0.f;
	GetCapturedAttributeMagnitude(StaminaRecoveryRateAttribute, Spec, EvaluationParameters, StaminaRecovery);

	float StaminaMultiplierRate = 1.f;
	GetCapturedAttributeMagnitude(StaminaRecoveryMultiplierAttribute, Spec, EvaluationParameters, StaminaMultiplierRate);

	if (StaminaRecovery > 0.f)
	{
		StaminaRecovery *= StaminaMultiplierRate;
	}

	return StaminaRecovery * Spec.Period;
}
