#include "Abilities/POTHealthRecoveryMultiplierMMC.h"
#include "Abilities/CoreAttributeSet.h"

UPOTHealthRecoveryMultiplierMMC::UPOTHealthRecoveryMultiplierMMC()
{
	// Define Attribute we want to capture
	// No snapshot in thise case since we want current value at each application.

	PoisonRateAttribute.AttributeToCapture = UCoreAttributeSet::GetPoisonRateAttribute();
	PoisonRateAttribute.AttributeSource = EGameplayEffectAttributeCaptureSource::Source;
	PoisonRateAttribute.bSnapshot = false;
	RelevantAttributesToCapture.Add(PoisonRateAttribute);
}

float UPOTHealthRecoveryMultiplierMMC::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	if (MaxPoison == -1.f)
	{
		MaxPoison = UCoreAttributeSet::FindAttributeCapFromConfig(PoisonRateAttribute.AttributeToCapture);
	}
	
	// Gather the tags from the source and target as that can affect which buffs should be used
	FAggregatorEvaluateParameters EvaluationParameters{};
	EvaluationParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluationParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Multiplier = 1.f;
	
	// -------------- Poison APPLICATION ------------------
	float CurrentPoisonRate = 0.f;
	GetCapturedAttributeMagnitude(PoisonRateAttribute, Spec, EvaluationParameters, CurrentPoisonRate);
	
	if (CurrentPoisonRate > 0.f && MaxPoison > 0.f && PoisonMultiplierCurve.IsValid())
	{
		Multiplier = FMath::Lerp(1.f, PoisonMultiplierCurve.GetValueAtLevel(0), CurrentPoisonRate / MaxPoison);
	}

	// -------------- OTHER APPLICATION ------------------

	// -------------- FINAL VALUE ------------------
	return Multiplier;
}