// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "Engine/StreamableManager.h"
#include "GameplayTagContainer.h"
#include "StatusEffectBar.generated.h"

enum class EDamageEffectType : uint8;
class UHorizontalBox;
class AIBaseCharacter;
class UStatusEffectEntry;
class UPausedStatusEffectEntry;
class UStatusEffectDataProvider;
class UGameplayEffect;

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UStatusEffectBar : public UUserWidget
{
	GENERATED_BODY()

public:
	friend class UStatusEffectEntry;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ForceInlineRow))
	TMap<EDamageEffectType, TSubclassOf<UStatusEffectDataProvider>> EffectDataProviders;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UStatusEffectEntry> StatusEffectEntryClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UPausedStatusEffectEntry> PausedStatusEffectEntryClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UStatusEffectDataProvider> GrowthEffectDataProvider;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UStatusEffectDataProvider> WellRestedEffectDataProvider;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TMap<TSoftClassPtr<UGameplayEffect>, TSubclassOf<UStatusEffectDataProvider>> CustomEffectDataProviders;

public:
	virtual void NativeConstruct() override;
	//virtual void NativeDestruct() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UHorizontalBox* ContainerBox;

protected:
	UPROPERTY(Transient)
	TWeakObjectPtr<AIBaseCharacter> OwnerCharacter;

	UPROPERTY()
	TMap<TSubclassOf<UGameplayEffect>, TWeakObjectPtr<UStatusEffectEntry>> ActiveStatusEffects;
	UPROPERTY()
	TMap<FGameplayTag, TWeakObjectPtr<UStatusEffectEntry>> CustomStatusEffects;

protected:
	void UnbindEvents();
	void BindEvents();

	void ClearEffects();
	void AddInitialEffects();

	void OnStatusEffectBeginRemove(const UStatusEffectEntry* Entry);

	UFUNCTION()
	void OnUIPausedEffectRemove(const UGameplayEffect* Effect = nullptr);

	UFUNCTION()
	void OnUIPausedEffectCreate();

	UFUNCTION()
	void OnUIPausedEffectReload();

	UFUNCTION()
	void CreateStatusPausedEffectEntry(const UGameplayEffect* Effect, float EffectRemainingDuration);

	UFUNCTION()
	void OnUIEffectRemove(const FActiveGameplayEffect& ActiveEffect);
	UFUNCTION()
	void OnUIEffectUpdate(FActiveGameplayEffectHandle Handle, float Duration, int32 Stacks);
	UFUNCTION()
	void OnStatusStart(EDamageEffectType DamageEffectType);
	UFUNCTION()
	void OnGrowth();
	UFUNCTION()
	void OnWellRested();

	UStatusEffectEntry* GetExistingStatusEffectWidget(const TSubclassOf<UGameplayEffect> EffectClass) const;
	void CreateOrUpdateStatusEffect(const FActiveGameplayEffectHandle& Handle, const TSubclassOf<UStatusEffectDataProvider>& DataProviderClass, bool bRemovableWidget = true);

private:
	bool bPawnInitialized;
};
