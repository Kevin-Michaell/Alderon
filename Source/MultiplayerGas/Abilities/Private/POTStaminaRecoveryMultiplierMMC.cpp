#include "Abilities/POTStaminaRecoveryMultiplierMMC.h"
#include "Abilities/CoreAttributeSet.h"

UPOTStaminaRecoveryMultiplierMMC::UPOTStaminaRecoveryMultiplierMMC()
{
	// Define Attribute we want to capture
	// No snapshot in thise case since we want current value at each application.

	VenomRateAttribute.AttributeToCapture = UCoreAttributeSet::GetVenomRateAttribute();
	VenomRateAttribute.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	VenomRateAttribute.bSnapshot = false;
	RelevantAttributesToCapture.Add(VenomRateAttribute);
}

float UPOTStaminaRecoveryMultiplierMMC::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	if (MaxVenom == -1.f)
	{
		MaxVenom = UCoreAttributeSet::FindAttributeCapFromConfig(VenomRateAttribute.AttributeToCapture);
	}
	
	// Gather the tags from the source and target as that can affect which buffs should be used
	FAggregatorEvaluateParameters EvaluationParameters{};
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Multiplier = 1.f;
	
	// -------------- VENOM ------------------
	float CurrentVenomRate = 0.f;
	GetCapturedAttributeMagnitude(VenomRateAttribute, Spec, EvaluationParameters, CurrentVenomRate);
	
	if (CurrentVenomRate > 0.f && MaxVenom && VenomMultiplierCurve.IsValid())
	{
		Multiplier = FMath::Lerp(1.f, VenomMultiplierCurve.GetValueAtLevel(0), CurrentVenomRate / MaxVenom);
	}

	// -------------- OTHER APPLICATION ------------------


	// -------------- FINAL VALUE ------------------
	return Multiplier;
}