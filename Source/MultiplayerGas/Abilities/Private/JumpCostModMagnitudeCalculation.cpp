// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.


#include "Abilities/JumpCostModMagnitudeCalculation.h"
#include "Player/IBaseCharacter.h"
#include "Abilities/CoreAttributeSet.h"

float UJumpCostModMagnitudeCalculation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const FGameplayEffectContextHandle& CHandle = Spec.GetEffectContext();
	if (CHandle.IsValid())
	{
		if (AIBaseCharacter* BaseChar = Cast<AIBaseCharacter>(CHandle.GetEffectCauser()))
		{
			if (BaseChar->AbilitySystem)
			{
				FGameplayTagContainer ActiveTags;
				BaseChar->AbilitySystem->GetAggregatedTags(ActiveTags);
				if (ActiveTags.HasTag(FGameplayTag::RequestGameplayTag(NAME_BuffInHomecave)))
				{
					return 0.0f; // jumping is free in homecave
				}
			}

			float CostMultiplier = 1.f;

			for (int32 i = 0; i < BaseChar->ConsecutiveJumps; i++)
			{
				//@TODO: This uses old StaminaJumpModifier. Move to GA like the rest.
				CostMultiplier *= BaseChar->GetStaminaJumpModifier();
			}

			CostMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaJumpCostMultiplierAttribute());

			return CostMultiplier;
		}
	}
	
	return 1.f;
}
