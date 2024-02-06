// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#include "Abilities/StamCostModMagnitudeCalculation.h"
#include "Player/IBaseCharacter.h"
#include "Abilities/CoreAttributeSet.h"

float UStamCostModMagnitudeCalculation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	float FinalMultiplier = 1.0f;

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
					return 0.0f; // can't use stam in homecave
				}

				FGameplayTagContainer TagsOwned;
				BaseChar->AbilitySystem->GetOwnedGameplayTags(TagsOwned);

				FGameplayTag SprintingTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionSprinting);
				FGameplayTag SwimTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionSwimming);
				FGameplayTag TrotSwimTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionTrotSwimming);
				FGameplayTag FastSwimTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionFastSwimming);
				FGameplayTag DiveTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionDiving);
				FGameplayTag TrotDiveTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionTrotDiving);
				FGameplayTag FastDiveTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionFastDiving);

				if (TagsOwned.HasTag(SprintingTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaSprintCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(SwimTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaSwimCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(TrotSwimTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaTrotSwimCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(FastSwimTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaFastSwimCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(DiveTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaDiveCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(TrotDiveTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaTrotDiveCostMultiplierAttribute());
				}
				else if (TagsOwned.HasTag(FastDiveTag))
				{
					FinalMultiplier *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaFastDiveCostMultiplierAttribute());
				}

				if (FinalMultiplier < 0.f)
				{
					// Handle Venom and other StaminaRecovery application
					const float StaminaMultiplierRate = BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaRecoveryMultiplierAttribute());
					FinalMultiplier *= StaminaMultiplierRate;
				}
			}
		}
	}

	return FinalMultiplier;
}
