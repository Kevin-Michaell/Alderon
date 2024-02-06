// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "RapidStrikesModifierCalculation.generated.h"

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API URapidStrikesModifierCalculation : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
	
private:
	static inline const float BaseMagnitude = 1.0f;

};
