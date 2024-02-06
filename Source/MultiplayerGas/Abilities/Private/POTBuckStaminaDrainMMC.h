// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayModMagnitudeCalculation.h"
#include "POTBuckStaminaDrainMMC.generated.h"

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UPOTBuckStaminaDrainMMC : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

protected:

	/**
	* @brief Slippery Gametag to use
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag SlipperyGameplayTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Slippery"));
	
	/**
	* @brief Bucking Stamina Drain
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FScalableFloat BuckingStaminaDrain;
	
	/**
	* @brief Increase (Multiplier) to the Bucking Stamina Drain when Source is Slippery
	*/
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FScalableFloat SlipperyStaminaMultiplier;
	
public:
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
};
