// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "StatusEffectEntry.generated.h"

class AIBaseCharacter;
class URetainerBox;
class UImage;
class UTextBlock;
class UStatusEffectBar;


UCLASS(Blueprintable, BlueprintType)
class PATHOFTITANS_API UStatusEffectDataProvider : public UObject
{
	GENERATED_BODY()

public:
	friend class UStatusEffectEntry;
	friend class UStatusEffectBar;

public:
	UStatusEffectDataProvider();

	TSoftObjectPtr<UTexture2D> GetIcon(const UStatusEffectEntry* StatusEffect) const;

	UFUNCTION(BlueprintNativeEvent)
	int32 GetStackCount(const UStatusEffectEntry* StatusEffect) const;
	UFUNCTION(BlueprintNativeEvent)
	float GetTimeRemaining(const UStatusEffectEntry* StatusEffect) const;
	UFUNCTION(BlueprintNativeEvent)
	bool ShouldRemove(const UStatusEffectEntry* StatusEffect) const;
	bool ShouldInvertFade(const UStatusEffectEntry* StatusEffect) const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> IconOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bRemoveOnInvalidEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTag CustomEffectTag;

};


/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UStatusEffectEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	friend class UStatusEffectBar;
	friend class UStatusEffectDataProvider;

public:

	static inline constexpr float TimeRemainingRemoveFromUI = 0.05f;

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	void RemoveEntry();
	FORCEINLINE bool IsRemovable() const { return bRemovable; }
	FORCEINLINE void SetRemovable(bool bInRemovable) { bRemovable = bInRemovable; }
	bool IsEffectInhibited();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	URetainerBox* IconRetainer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UImage* IconImage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UTextBlock* StackCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UTextBlock* TimeText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Stats, meta = (BindWidgetAnim = "true"))
	UWidgetAnimation* Appear;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = Stats, meta = (BindWidgetAnim = "true"))
	UWidgetAnimation* Disappear;

	UPROPERTY(meta = (ExposeOnSpawn = "true"), BlueprintReadOnly)
	UStatusEffectBar* ParentBar;

	UPROPERTY(meta = (ExposeOnSpawn = "true"), BlueprintReadOnly)
	FActiveGameplayEffectHandle EffectHandle;

	UPROPERTY(meta = (ExposeOnSpawn = "true"), BlueprintReadOnly)
	TSubclassOf<UGameplayEffect> EffectClass;

	UPROPERTY(meta = (ExposeOnSpawn = "true"), BlueprintReadOnly)
	TSubclassOf<UStatusEffectDataProvider> DataProvider;

	UFUNCTION()
	void OnEffectInhibitionChanged(FActiveGameplayEffectHandle Handle, bool bIsInhibited);
	
	bool bSlotInitialized = false;
protected:

	float Duration = 1.0f;
	FGameplayEffectContextHandle Context;
	bool bInhibited = false;
	bool bRemovable = true;

	TSoftObjectPtr<UTexture2D> ExtractIcon() const;
	float SetRemainingTime();
	int32 SetStackCount(int32 NewStacks = -1);

	void UpdateStatusEffect(int NewStacks);

	UFUNCTION(BlueprintNativeEvent)
	void OnDisappearAnimationEnd();

	UStatusEffectDataProvider* GetDataProvider() const;

	bool bIsFadedOut = false;
	bool bIsGettingRemoved = false;
	int32 Stacks = 0;
};

UCLASS()
class PATHOFTITANS_API UPausedStatusEffectEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	friend class UStatusEffectBar;

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	URetainerBox* IconRetainer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UImage* IconImage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Stats, meta = (BindWidget = "true"))
	UTextBlock* TimeText;

protected:
	float Duration = 1.0f;
	float RemainingTime = 0.f;
	FString EffectName;

protected:
	void SetRemainingTime();
};