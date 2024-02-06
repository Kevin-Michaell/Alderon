// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#include "Abilities/POTDamageExecution.h"
#include "Abilities/DamageStatics.h"
#include "Abilities/CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/POTAbilityTypes.h"
#include "Abilities/POTAbilitySystemComponent.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Player/IBaseCharacter.h"
#include "IGameplayStatics.h"
#include "GameMode/IGameMode.h"


UPOTDamageExecution::UPOTDamageExecution()
{
	const POTDamageStatics& DamageStatics = POTDamageStatics::DamageStatics();
	
	RelevantAttributesToCapture.Add(DamageStatics.AttackDamageDef);
	RelevantAttributesToCapture.Add(DamageStatics.BoneBreakChanceDef);
	RelevantAttributesToCapture.Add(DamageStatics.BoneBreakAmountDef);
	for (auto& DamageStatusConfig : DamageStatics.DamageConfig)
	{
		if (DamageStatusConfig.Value.Definition)
		{
			RelevantAttributesToCapture.Add(*DamageStatusConfig.Value.Definition);
		}
	}
	RelevantAttributesToCapture.Add(DamageStatics.ArmorDef);
	RelevantAttributesToCapture.Add(DamageStatics.SourceCombatWeightDef);
	RelevantAttributesToCapture.Add(DamageStatics.TargetCombatWeightDef);
}

void UPOTDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const POTDamageStatics& DamageStatics = POTDamageStatics::DamageStatics();
	
	UPOTAbilitySystemComponentBase* TargetAbilitySystemComponent = Cast<UPOTAbilitySystemComponentBase>(ExecutionParams.GetTargetAbilitySystemComponent());
	UPOTAbilitySystemComponentBase* SourceAbilitySystemComponent = Cast<UPOTAbilitySystemComponentBase>(ExecutionParams.GetSourceAbilitySystemComponent());

	AActor* SourceActor = SourceAbilitySystemComponent ? SourceAbilitySystemComponent->GetAvatarActor() : nullptr;
	AActor* TargetActor = TargetAbilitySystemComponent ? TargetAbilitySystemComponent->GetAvatarActor() : nullptr;

	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	FGameplayEffectContextHandle ContextHandle = Spec.GetContext();
	FGameplayEffectContext* GEContext = ContextHandle.Get();

	const FHitResult* HResult = ContextHandle.GetHitResult();
	FName HitBoneName = HResult != nullptr ? HResult->BoneName : NAME_None;
	FName SourceHitBoneName = HResult != nullptr ? HResult->MyBoneName : NAME_None;
	
	if (!ensureAlwaysMsgf(GEContext != nullptr, TEXT("Context in POTDamageExecution was null! Aborting...")))
	{
		return;
	}

	FPOTGameplayEffectContext* POTEffectContext = StaticCast<FPOTGameplayEffectContext*>(GEContext);

	// Gather the tags from the source and target as that can affect which buffs should be used
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	FAggregatorEvaluateParameters EvaluationParameters;
	EvaluationParameters.SourceTags = SourceTags;
	EvaluationParameters.TargetTags = TargetTags;

	FBoneDamageModifier DamageModifier;
	FBoneDamageModifier SourceDamageModifier;

	if (UPOTAbilitySystemComponent* WAC = Cast<UPOTAbilitySystemComponent>(TargetAbilitySystemComponent))
	{
		if (SourceTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_AbilityIgnoreBoneMultipliers)))
		{
			DamageModifier = FBoneDamageModifier();
		}
		else
		{
			WAC->GetDamageMultiplierForBone(HitBoneName, DamageModifier);
		}
	}

	if (UPOTAbilitySystemComponent* SourceWAC = Cast<UPOTAbilitySystemComponent>(SourceAbilitySystemComponent))
	{
		SourceWAC->GetDamageMultiplierForBone(SourceHitBoneName, SourceDamageModifier);
	}

	float SourceWeight = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics.SourceCombatWeightDef, EvaluationParameters, SourceWeight);
	
	float TargetWeight = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics.TargetCombatWeightDef, EvaluationParameters, TargetWeight);

	float WeightRatio = 1.f;
	if (TargetWeight > 0.f && SourceWeight > 0.f)
	{
		WeightRatio = SourceWeight / TargetWeight;
	}


	float EventMagnitude = POTEffectContext != nullptr ? POTEffectContext->GetEventMagnitude() : 1.f;
	if (POTEffectContext->GetDamageType() == EDamageType::DT_TRAMPLE)
	{
		// If this is trample damage, the event magnitude should start at 0 (i.e. no damage at same weight) and rise exponentially.
		EventMagnitude = FMath::Max(FMath::Pow(WeightRatio - 1.f, TramplePower), 0.f);
	}

	float TargetArmor = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics.ArmorDef, EvaluationParameters, TargetArmor);
	
	if (WeightRatio > 0.0f)
	{
		TargetArmor /= WeightRatio;
	}
	
	float DamageSum = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics.AttackDamageDef, EvaluationParameters, DamageSum);

	DamageSum *= DamageModifier.DamageMultiplier;
	if (TargetArmor > 0.0f && POTEffectContext->GetDamageType() != EDamageType::DT_ARMORPIERCING)
	{
		DamageSum /= TargetArmor;
	}

	float BoneBreakChance = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
		DamageStatics.BoneBreakChanceDef, EvaluationParameters, BoneBreakChance);

	// Bone break chance should not scale by WeightRatio.
	BoneBreakChance *= DamageModifier.BoneBreakChanceMultiplier;
	
	// Block ability logic, except for bonebreak blocking, which is further below
	bool bSomethingWasBlocked = false;
	const AIBaseCharacter* const TargetCharacter = Cast<AIBaseCharacter>(TargetActor);
	const UPOTGameplayAbility* const TargetCurrentAbility = TargetAbilitySystemComponent->GetCurrentAttackAbility();
	
	if (TargetCurrentAbility && TargetCurrentAbility->bBlockAbility && TargetCharacter)
	{
		EDamageWoundCategory Category = EDamageWoundCategory::MAX;

		for (const auto& Elem : TargetCharacter->BoneNameToWoundCategory)
		{
			if (Elem.Value.BoneNames.Contains(HitBoneName))
			{
				Category = Elem.Key;
				break;
			}
		}

		if (Category != EDamageWoundCategory::MAX && TargetCurrentAbility->BlockingRegions.Contains(Category))
		{
			bSomethingWasBlocked = true;
			DamageSum *= TargetCurrentAbility->GetBlockScalarForDamageType(EDamageEffectType::DAMAGED);
		}
	}

	// The event magnitude usually comes from the weapon slot and it increases all damage and force effects.
	DamageSum *= EventMagnitude;

	// Bone break chance should not be affected by EventMagnitude.
	// BoneBreakChance *= EventMagnitude;

	float FinalSpikesDamageMultiplier = 1.f;
	
	if (POTEffectContext->GetDamageType() == EDamageType::DT_SPIKES)
	{
		FinalSpikesDamageMultiplier = SourceAbilitySystemComponent->GetNumericAttribute(UCoreAttributeSet::GetSpikeDamageMultiplierAttribute());

		if (!SourceTags->HasTag(FGameplayTag::RequestGameplayTag(NAME_AbilityIgnoreBoneMultipliers)))
		{
			FinalSpikesDamageMultiplier *= SourceDamageModifier.GetMultiplierByDamageEffectType(EDamageEffectType::SPIKES);
		}

		DamageSum *= FinalSpikesDamageMultiplier;

		// GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, FString("Spikes multiplier ") + FString::SanitizeFloat(FinalSpikesDamageMultiplier));
	}

	POTEffectContext->SetBrokeBone(false);
	
	TMap<EDamageEffectType, float> OtherDamage{};
	// Status Damage
	for (auto& DamageStatusConfig : DamageStatics.DamageConfig)
	{
		if (!DamageStatusConfig.Value.Definition)
		{
			continue;
		}
		
		float StatusDamageSum = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
			*DamageStatusConfig.Value.Definition, EvaluationParameters, StatusDamageSum);
		
		if (FMath::IsNearlyZero(StatusDamageSum))
		{
			continue;
		}
		
		StatusDamageSum *= DamageModifier.GetMultiplierByDamageEffectType(DamageStatusConfig.Key);
		
		if (DamageStatusConfig.Value.bEventMagnitude)
		{
			StatusDamageSum *= EventMagnitude;			
		}

		if (DamageStatusConfig.Value.bSpikes)
		{
			StatusDamageSum *= FinalSpikesDamageMultiplier;
		}

		if (bSomethingWasBlocked)
		{
			StatusDamageSum *= TargetCurrentAbility->GetBlockScalarForDamageType(DamageStatusConfig.Key);
		}

		// STATUS_UPDATE_MARKER - Add any status specific behavior here
		switch (DamageStatusConfig.Key) {
			case EDamageEffectType::NONE:
			case EDamageEffectType::KNOCKBACK:
			case EDamageEffectType::DAMAGED:
			case EDamageEffectType::SPIKES:
				continue;
			
			case EDamageEffectType::BROKENBONEONGOING:
			case EDamageEffectType::BROKENBONE:
			{
				// Handle Broken Bones. Bone break should not have RNG, so accept Bone Break every time if the chance is above 0.
				if (BoneBreakChance > 0.f && StatusDamageSum > 0.f)
				{
					POTEffectContext->SetBrokeBone(true);
				} else
				{
					// No BoneBreak at all
					continue;
				}
				break;
			}
			
			case EDamageEffectType::BLEED:
			case EDamageEffectType::POISONED:
			case EDamageEffectType::VENOM:
			default:
			{
				break;
			}
		}

		if (StatusDamageSum > 0.f)
		{
			OtherDamage.Add(DamageStatusConfig.Key, StatusDamageSum);
		}
		
		OutExecutionOutput.
			AddOutputModifier(
				FGameplayModifierEvaluatedData(
					DamageStatusConfig.Value.IncomingProperty,
					EGameplayModOp::Additive,
					StatusDamageSum));
	}

	AIBaseCharacter* SourceBaseCharacter = Cast<AIBaseCharacter>(SourceActor);
	if (AIBaseCharacter* TargetBaseCharacter = Cast<AIBaseCharacter>(TargetActor))
	{
		if (AIGameMode* IGameMode = UIGameplayStatics::GetIGameMode(TargetBaseCharacter))
		{
			// Damage can be canceled by mods. If the mod overrides this function and returns false, the damage should be canceled. e.g. avoiding team friendly damage
			if (!IGameMode->OnCharacterTakeDamage(TargetBaseCharacter, SourceBaseCharacter, POTEffectContext->GetDamageType(), DamageSum, OtherDamage))
			{
				return;
			}
		}

		// shouldn't apply if target player is immune or in home/tutorial cave
		if (!TargetBaseCharacter->IsHomecaveBuffActive() && !TargetBaseCharacter->GetGodmode() && !IsValid(TargetBaseCharacter->GetCurrentInstance()))
		{
			UPOTAbilitySystemComponent* DamageSourceASC = Cast<UPOTAbilitySystemComponent>(SourceAbilitySystemComponent);
			const UPOTGameplayAbility* SourcePOTAbility = Cast<UPOTGameplayAbility>(POTEffectContext->GetAbility());
			if (DamageSourceASC && SourcePOTAbility)
			{
				for (TSubclassOf<UGameplayEffect> EffectClass : SourcePOTAbility->EffectsAppliedToSelfAfterDamage)
				{
					DamageSourceASC->ApplyPostDamageEffect(EffectClass, DamageSum);
				}
			}
		}
	}

	if (DamageSum > 0.f)
	{
		OutExecutionOutput.
		AddOutputModifier(
			FGameplayModifierEvaluatedData(
				DamageStatics.IncomingDamageProperty, 
				EGameplayModOp::Additive, 
				DamageSum));
		
		if (POTEffectContext->GetDamageType() != EDamageType::DT_TRAMPLE)
		{
			POTEffectContext->SetDamageType(EDamageType::DT_ATTACK);
		}
	}
}

