#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "POTStaminaRegenMMC.generated.h"

UCLASS(Config = Game)
class PATHOFTITANS_API UPOTStaminaRegenMMC : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UPOTStaminaRegenMMC();

protected:
	FGameplayEffectAttributeCaptureDefinition StaminaRecoveryRateAttribute;
	FGameplayEffectAttributeCaptureDefinition StaminaRecoveryMultiplierAttribute;

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
};
