// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.


#include "POTBuckStaminaDrainMMC.h"

#include "AbilitySystemComponent.h"
#include "Abilities/POTAbilitySystemGlobals.h"

float UPOTBuckStaminaDrainMMC::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	if (!BuckingStaminaDrain.IsValid() || !SlipperyStaminaMultiplier.IsValid())
	{
		UE_LOG(TitansLog, Error, TEXT("UPOTBuckStaminaDrainMMC: BuckingStaminaDrain or SlipperyStaminaMultiplier is not set correctly"));
		return 0.f;
	}

	float StaminaDrain = BuckingStaminaDrain.GetValueAtLevel(Spec.GetLevel());
	const UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Spec.GetEffectContext().GetInstigator());

	if (SourceASC && SourceASC->HasMatchingGameplayTag(SlipperyGameplayTag))
	{
		StaminaDrain *= SlipperyStaminaMultiplier.GetValueAtLevel(Spec.GetLevel());
	}

	return StaminaDrain;
}
