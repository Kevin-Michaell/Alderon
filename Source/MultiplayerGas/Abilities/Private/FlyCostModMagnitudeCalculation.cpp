// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#include "Abilities/FlyCostModMagnitudeCalculation.h"
#include "Player/IBaseCharacter.h"
#include "IWorldSettings.h"
#include "World/IUltraDynamicSky.h"
#include "Abilities/CoreAttributeSet.h"

float UFlyCostModMagnitudeCalculation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
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
		const FGameplayTag FlyingTag = FGameplayTag::RequestGameplayTag(NAME_StateLocomotionFlying);
				
		FGameplayTagContainer TagsOwned;
		BaseChar->AbilitySystem->GetOwnedGameplayTags(TagsOwned);

		if (TagsOwned.HasTag(FlyingTag))
		{
			//Apply stamina cost multipliers
			ReturnValue *= BaseChar->AbilitySystem->GetNumericAttribute(UCoreAttributeSet::GetStaminaFlyCostMultiplierAttribute());
			ReturnValue *= UFlyCostModMagnitudeCalculation::CalculateCarryFlightMagnitude(*BaseChar);
			ReturnValue *= UFlyCostModMagnitudeCalculation::CalculateWeatherFlightMagnitude(*BaseChar);
		}
	}
	
	return ReturnValue;
}

float UFlyCostModMagnitudeCalculation::CalculateCarryFlightMagnitude(const AIBaseCharacter& BaseChar)
{
	float ReturnValue = 1.0f;

	if (BaseChar.IsCarryingObject())
	{
		ReturnValue *= BaseChar.GetStaminaCarryCostMultiplier();
		GEngine->AddOnScreenDebugMessage(194145, 2.0f, FColor::Blue, FString("Carrying object: multiplying stamina usage by " + FString::SanitizeFloat(BaseChar.GetStaminaCarryCostMultiplier())));
	}

	return ReturnValue;
}

float UFlyCostModMagnitudeCalculation::CalculateWeatherFlightMagnitude(const AIBaseCharacter& BaseChar)
{
	float ReturnValue = 1.0f;

	const AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(BaseChar.Controller);
	
	if (const AIUltraDynamicSky* Sky = IWorldSettings ? IWorldSettings->UltraDynamicSky : nullptr)
	{
		const EWeatherType CurrentWeather = Sky->GetTargetWeatherType();

		if (CurrentWeather == EWeatherType::Snow || CurrentWeather == EWeatherType::Rain || CurrentWeather == EWeatherType::Storm)
		{
			ReturnValue *= BaseChar.GetStaminaBadWeatherCostMultiplier();
			GEngine->AddOnScreenDebugMessage(194146, 2.0f, FColor::Blue, FString("Bad weather: multiplying stamina usage by " + FString::SanitizeFloat(BaseChar.GetStaminaBadWeatherCostMultiplier())));
		}
	}

	return ReturnValue;
}