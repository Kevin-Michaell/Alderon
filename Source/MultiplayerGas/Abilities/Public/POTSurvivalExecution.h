// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectExecutionCalculation.h"
#include "POTAbilityTypes.h"
#include "POTSurvivalExecution.generated.h"




/**
 * A damage execution, which allows doing damage by combining a raw Damage number with AttackPower and DefensePower
 * Most games will want to implement multiple game-specific executions
 */
UCLASS()
class PATHOFTITANS_API UPOTSurvivalExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	// Constructor and overrides
	UPOTSurvivalExecution();
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

private:
	//Processes a single survival attribute. Returns the damage to the attribute, if any.
	float ProcessAttribute(
		const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		const FGameplayEffectAttributeCaptureDefinition& BaseAttribute,
		const FGameplayEffectAttributeCaptureDefinition& MaxValueAttribute,
		const FGameplayEffectAttributeCaptureDefinition& AttributeDepletion,
		const FGameplayEffectAttributeCaptureDefinition& AttributeDamage,
		//const FGameplayEffectAttributeCaptureDefinition& IncomingAttributeModification,
		const FAggregatorEvaluateParameters& EvaluationParameters,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput,
		EDamageType DamageType,
		FPOTGameplayEffectContext* POTEffectContext,
		bool bIsRecovery = false) const;
};