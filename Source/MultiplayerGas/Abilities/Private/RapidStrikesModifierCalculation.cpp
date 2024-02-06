// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.


#include "Abilities/RapidStrikesModifierCalculation.h"
#include "Abilities/CoreAttributeSet.h"
#include "Player/IBaseCharacter.h"

float URapidStrikesModifierCalculation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const FGameplayEffectContextHandle& CHandle = Spec.GetEffectContext();

	if (CHandle.IsValid())
	{
		if (AIBaseCharacter* BaseChar = Cast<AIBaseCharacter>(CHandle.GetEffectCauser()))
		{
			const float BuffAmount = BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetRapidStrikesModifierAttribute());
			return BaseMagnitude + BuffAmount;
		}
	}

	return BaseMagnitude;
}