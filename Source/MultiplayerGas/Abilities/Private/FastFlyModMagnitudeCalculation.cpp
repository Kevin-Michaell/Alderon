// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#include "Abilities/FastFlyModMagnitudeCalculation.h"
#include "Abilities/FlyCostModMagnitudeCalculation.h"
#include "Player/IBaseCharacter.h"
#include "Components/ICharacterMovementComponent.h"
#include "Abilities/CoreAttributeSet.h"

float UFastFlyCostModMagnitudeCalculation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	float ReturnValue = 1.0f;
	const FGameplayEffectContextHandle& CHandle = Spec.GetEffectContext();

	if (!CHandle.IsValid())
	{
		return ReturnValue;
	}

	const AIBaseCharacter* const BaseChar = Cast<AIBaseCharacter>(CHandle.GetEffectCauser());

	if (BaseChar && BaseChar->AbilitySystem)
	{
		const UICharacterMovementComponent* CharMov = Cast<UICharacterMovementComponent>(BaseChar->GetCharacterMovement());
		const FGameplayTag FastFlyingTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionFastFlying);

		FGameplayTagContainer TagsOwned;
		BaseChar->AbilitySystem->GetOwnedGameplayTags(TagsOwned);

		if (CharMov && TagsOwned.HasTag(FastFlyingTag))
		{
			//Check velocity is above max trot speed
			if (!CharMov->IsExceedingMaxSpeed(CharMov->MaxFlySlowSpeed))
			{
				UE_LOG(TitansLog, Log, TEXT("UFastFlyCostModMagnitudeCalculation::CalculateBaseMagnitude_Implementation - Character is not moving fast enough to apply fast fly cost.  Fast fly Magnitude set to zero."));
				return 0.0f;
			}
					
			ReturnValue *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaFastFlyCostMultiplierAttribute());
			ReturnValue *= UFlyCostModMagnitudeCalculation::CalculateCarryFlightMagnitude(*BaseChar);
			ReturnValue *= UFlyCostModMagnitudeCalculation::CalculateWeatherFlightMagnitude(*BaseChar);
		}
	}

	return ReturnValue;
}