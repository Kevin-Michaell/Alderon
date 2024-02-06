// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "POTHealthRecoveryMultiplierMMC.generated.h"

/**
 *
 */
UCLASS(Config = Game)
class PATHOFTITANS_API UPOTHealthRecoveryMultiplierMMC : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()
public:
	UPOTHealthRecoveryMultiplierMMC();

	FGameplayEffectAttributeCaptureDefinition PoisonRateAttribute;
	
	inline static float MaxPoison = -1.f;

	/**
	* @brief Health Multiplier when at the higherPoisonRate (level 1)
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FScalableFloat PoisonMultiplierCurve;

	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
};
