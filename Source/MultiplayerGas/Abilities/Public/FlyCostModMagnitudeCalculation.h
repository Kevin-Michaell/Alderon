// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "FlyCostModMagnitudeCalculation.generated.h"

class AIBaseCharacter;

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UFlyCostModMagnitudeCalculation : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

	static float CalculateCarryFlightMagnitude(const AIBaseCharacter& BaseChar);
	static float CalculateWeatherFlightMagnitude(const AIBaseCharacter& BaseChar);
};
