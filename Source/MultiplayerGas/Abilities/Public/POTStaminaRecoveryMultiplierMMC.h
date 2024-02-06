// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "POTStaminaRecoveryMultiplierMMC.generated.h"

/**
 *
 */
UCLASS(Config = Game)
class PATHOFTITANS_API UPOTStaminaRecoveryMultiplierMMC : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()
public:
	UPOTStaminaRecoveryMultiplierMMC();

	FGameplayEffectAttributeCaptureDefinition VenomRateAttribute;

	inline static float MaxVenom = -1.f;

	/**
	* @brief Stamina Multiplier when at the higherVenomRate (level 1)
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FScalableFloat VenomMultiplierCurve;

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
};
