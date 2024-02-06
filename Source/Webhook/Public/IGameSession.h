// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameSession.h"
#include "Online.h"
#include "IOnlineGameSettings.h"
#include "ITypes.h"
#include "IGameSession.generated.h"

DECLARE_DELEGATE(FGameSessionReady);

DECLARE_DELEGATE_OneParam(FPlayerExtraInfoRefreshed, AIPlayerState*);

UENUM(BlueprintType)
enum class ENetworkPortType : uint8
{
	// Game Port
	Primary		UMETA(DisplayName = "Primary"),
	// Query Port
	Secondary	UMETA(DisplayName = "Secondary"),
	// Rcon Port
	Rcon	UMETA(DisplayName = "Rcon"),
	// Stats Port
	Stats	UMETA(DisplayName = "Stats"),
	// Stats Port
	Unknown		UMETA(DisplayName = "Unknown")
};

struct FIGameSessionParams
{
	/** Name of session settings are stored with */
	FName SessionName;
	/** LAN Match */
	bool bIsLAN;
	/** Presence enabled session */
	bool bIsPresence;
	/** Id of player initiating lobby */
	TSharedPtr<const FUniqueNetId> UserId;
	/** Current search result choice to join */
	int32 BestSessionIdx;

	FIGameSessionParams()
		: SessionName(NAME_None)
		, bIsLAN(false)
		, bIsPresence(false)
		, BestSessionIdx(0)
	{
	}
};

// Not using USTRUCT for speed
class FPlayerBan
{
public:
	FAlderonPlayerID PlayerId;
	FString IPAddress = "";
	uint64 BanExpiration = 0;
	FString AdminReason = "";
	FString UserReason = "";

	bool IsValid() const;
	bool IsExpired() const;
	FString ToString() const;
	void FromString(const FString Line);
};

class FPlayerMute
{
public:
	FAlderonPlayerID PlayerId;
	uint64 BanExpiration = 0;
	FString AdminReason = "";
	FString UserReason = "";

	bool IsValid() const;
	bool IsExpired() const;
	FString ToString() const;
	void FromString(const FString Line);
};

// Old - Planned on being removed
USTRUCT(BlueprintType)
struct FBanInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FString UserName;

	UPROPERTY()
	FString UniqueID;

	FBanInfo() : UserName(TEXT("")), UniqueID(TEXT(""))
	{
	}

	FBanInfo(const FString& inUserName, const FString& inUniqueID) : UserName(inUserName), UniqueID(inUniqueID)
	{
	}
};

UCLASS(BlueprintType)
class UWebhook : public UObject
{
	GENERATED_BODY()

public:
	FString WebhookKey;
	TMap<FString, TSharedPtr<FJsonValue>> Properties;

	UFUNCTION(BlueprintCallable, Category = Webhook)
	static UWebhook* CreateWebhook(const FString& Key);

	UFUNCTION(BlueprintCallable, Category = Webhook)
	void AddPropertyString(const FString& Name, const FString& Property);

	UFUNCTION(BlueprintCallable, Category = Webhook)
	void AddPropertyInt(const FString& Name, int Property);

	UFUNCTION(BlueprintCallable, Category = Webhook)
	void AddPropertyFloat(const FString& Name, float Property);

	UFUNCTION(BlueprintCallable, Category = Webhook)
	void AddPropertyBool(const FString& Name, bool Property);

	UFUNCTION(BlueprintCallable, Category = Webhook, meta = (WorldContext = "WorldContextObject"))
	void Send(const UObject* WorldContextObject);
};

UCLASS(config=Game)
class PATHOFTITANS_API AIGameSession : public AGameSession
{
	GENERATED_UCLASS_BODY()

public:
	// Server Bans
	void ConvertOldBans();
	void LoadBans();
	void SaveBans();
	void AppendBan(FPlayerBan Ban);
	void RemoveBan(FPlayerBan Ban);

	// Server Mutes
	void LoadMutes();
	void SaveMutes();
	void AppendMute(FPlayerMute Mute);
	bool RemoveMute(FPlayerMute Mute);

	// Server Whitelisting
	void LoadWhitelist();
	void SaveWhitelist();
	void AppendWhitelist(const FAlderonPlayerID& AlderonId);
	bool RemoveWhitelist(const FAlderonPlayerID& AlderonId);
	bool IsWhitelistActive();

	FDateTime LastBansEditTime = FDateTime::MinValue();
	FDateTime LastMutesEditTime = FDateTime::MinValue();
	FDateTime LastWhitelistEditTime = FDateTime::MinValue();

	void SetupHotReloadTimer();
	void ClearHotReloadTimer();

	FTimerHandle TimerHandle_HotReload;

	// Hot Reloading of Bans, Mutes, Whitelist Updates
	void CheckForHotReload();

	// Reserved Slots System
	UPROPERTY(config)
	int32 ReservedSlots = 20;

	/** @return true if there is no room on the server for an additional player */
	virtual bool AtCapacity(bool bSpectator) override;

	int GetNetworkPort(ENetworkPortType PortType = ENetworkPortType::Primary);

	FString GetServerListIP();
	int GetServerListPort();

	void SetServerListIP(const FString& NewServerListIP) { OverrideServerListIP = NewServerListIP; }
	void SetServerListPort(int32 NewServerListPort) { OverrideServerListPort = NewServerListPort; }

	static bool UseWebHooks(const FString& WebhookKey = "");
	static void TriggerWebHookFromContext(UObject* ConextObject, const FString& WebhookKey, TMap<FString, TSharedPtr<FJsonValue>> Properties);

	void TriggerWebHook(const FString& WebhookKey, const TMap<FString, TSharedPtr<FJsonValue>>& Properties);
	void TriggerWebHook_Callback(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString WebHookKey);

	UFUNCTION(BlueprintCallable, Category = Webhook)
	void TriggerWebHook(const UWebhook* Webhook);

	// Webhook related functions called at server startup with loaded mod version info
	FTimerHandle TimerHandle_CheckVersionUpdate;

	TArray<FString> ListOfSkuToCheck{};

	void ServerStartupLoadedModWebHook();

	void UpdateLoadedModVersionWebHook();

	void ProcessModeVersion(const TMap<FString, TStrongObjectPtr<UAlderonUGCDetails>>& LoadedMods, TStrongObjectPtr<UAlderonUGCDetails> SkuModDetails, TMap<FString, TSharedPtr<FJsonValue>>& UpdatedWebHookProperties);

	// Called when the game session is ready and not busy (registered and all that)
	FGameSessionReady OnGameSessionReady;

	bool IsBusy() const;
	bool TargetMapLoadWait() const;

	virtual void CheckServerRegisterReady();
	virtual bool CheckServerLoadTargetMapReady();

	virtual void RegisterServer() override;
	virtual void UnRegisterServer(bool bShuttingDown);
	virtual void InitOptions(const FString& Options) override;

	UFUNCTION(BlueprintCallable)
	virtual void SaveString(const FString& Filename, const FString& String);

	virtual bool ProcessAutoLogin() override;
	virtual void OnAutoLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& Error) override;

	virtual FString ApproveLogin(const FString& Options) override;
	virtual void ValidatePlayer(const FString& Address, const TSharedPtr<const FUniqueNetId>& UniqueId, FString& ErrorMessage, bool bValidateAsSpectator);

	UFUNCTION()
	void OnServerOTPGenerated(const FString& Otp, const FString& Token, const FAlderonServerUserDetails& UserDetails, AIPlayerController* IPlayerController, AIPlayerState* IPlayerState);

	void OnOTPAdditionalInfo(const FAlderonServerUserExtraInfo& UserDetails, TWeakObjectPtr<AIPlayerController> IPlayerController, TWeakObjectPtr<AIPlayerState> IPlayerState);

	void RefreshPlayerStateExtraInfo(AIPlayerState* IPlayerState, FPlayerExtraInfoRefreshed OnComplete);

private:
	void RefreshPlayerStateExtraInfoCallback(const FAlderonServerUserExtraInfo& UserDetails, TWeakObjectPtr<AIPlayerState> IPlayerState, FPlayerExtraInfoRefreshed OnComplete);

public:
	UFUNCTION()
	void GenerateReferFriendRewards(const FAlderonServerUserExtraInfo& UserDetails, AIPlayerController* IPlayerController, AIPlayerState* IPlayerState);

	virtual void RegisterPlayer(APlayerController* NewPlayer, const TSharedPtr<const FUniqueNetId>& UniqueId, bool bWasFromInvite) override;
	virtual void UnregisterPlayer(const APlayerController* ExitingPlayer) override;

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void NotifyLogout(const APlayerController* PC) override;

	bool AllowServerRegistration();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsPlayerBanned(const FString& UniqueID);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsPlayerServerMuted(const FString& UniqueID);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsPlayerWhitelisted(const FString& UniqueID);

	bool IsPlayerBanned(const FAlderonPlayerID& AlderonId);

	bool IsPlayerServerMuted(const FAlderonPlayerID& AlderonId);

	bool IsPlayerWhitelisted(const FAlderonPlayerID& AlderonId);

	FPlayerBan GetBanInformation(const FAlderonPlayerID& AlderonId);

	FPlayerMute GetServerMuteInformation(const FAlderonPlayerID& AlderonId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsIPAddressBanned(const FString& IPAddress);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool KickPlayer(APlayerController* KickedPlayer, const FText& KickReason) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool BanPlayer(APlayerController* BannedPlayer, const FText& BanReason) override;
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool BanId(const FString& AlderonId, const FText& BanReason);	
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool UnbanId(const FString& AlderonId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual void AddAdmin(APlayerController* AdminPlayer) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual void RemoveAdmin(APlayerController* AdminPlayer) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsAdmin(const APlayerController* AdminPlayer) const;
	
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual void AddGameDev(const APlayerController* const DevPlayer);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsDev(APlayerController* DevPlayer);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsAdminID(const FString& UniqueID) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsDevID(const FString& UniqueID);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool IsServer(APlayerController* DevPlayer);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = Session)
	virtual bool AllowAdminRequest(APlayerController* Player);

	UFUNCTION(BlueprintCallable, Category = Session)
	void RegisterListenServer();

	UFUNCTION(BlueprintCallable, Category = Session)
	void SetServerOptions(FServerOptions Options);

	UFUNCTION(BlueprintPure, Category = Session)
	FString GetServerName();
    
	UFUNCTION(BlueprintPure, Category = Session)
	FString GetServerMOTD();

	void LoadServerMOTD();
	void LoadServerRules();

	UFUNCTION(BlueprintPure, Category = Session)
	FString GetServerRules();

	uint32 GetServerRulesCrcHash();

	UFUNCTION(BlueprintPure, Category = Session)
	bool DatabaseEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	bool GlobalChatEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	bool NameTagsEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	bool ExperimentalEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	bool AllowChat();

	UFUNCTION(BlueprintPure, Category = Session)
	bool AllowProximityVoiceChat();

	UFUNCTION(BlueprintPure, Category = Session)
	bool AllowPartyVoiceChat();

	UFUNCTION(BlueprintPure, Category = Session)
	bool Allow3DMapMarkers();

	UFUNCTION(BlueprintPure, Category = Session)
	bool AIEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	bool FishEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	int AIMax();

	UFUNCTION(BlueprintPure, Category = Session)
	float AIRate();

	UFUNCTION(BlueprintPure, Category = Session)
	bool AIPlayerSpawns();

	UFUNCTION(BlueprintPure, Category = Session)
	bool GrowthEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	int GetCombatDeathMarksPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetCombatDeathGrowthPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetFallDeathMarksPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetFallDeathGrowthPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetSurvivalDeathMarksPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetSurvivalDeathGrowthPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	int GetChangeSubspeciesGrowthPenaltyPercent() const;

	UFUNCTION(BlueprintPure, Category = Session)
	FString GetServerDiscord();

	UFUNCTION(BlueprintPure, Category = Session)
	const bool IsAutoRestartEnabled();

	UFUNCTION(BlueprintPure, Category = Session)
	const int32 GetAutoRestartLength();

	UFUNCTION(BlueprintPure, Category = Session)
	const bool IsUsingScheduledRestartTimes();

	UFUNCTION(BlueprintPure, Category = Session)
	const TArray<int32>& GetScheduledRestartTimes();

	UFUNCTION(BlueprintPure, Category = Session)
	const int32 GetNextScheduledRestartTime();

	UFUNCTION(BlueprintPure, Category = Session)
	const TArray<int32>& GetRestartNotificationTimestamps();

	void SetServerRules(const FString& Rules);
	void SetServerMotd(const FString& Motd);

	UFUNCTION(BlueprintPure, Category = Session)
	float GetMinGrowthAfterDeath() const;

protected:
	UPROPERTY(config)
	FString ServerName;

	UPROPERTY(config)
	FString ServerPassword;
    
	FString ServerMOTD;

	FString ServerRules;

	uint32 ServerRulesCrcHash;

	UPROPERTY(config)
	FString ServerDiscord;

	bool bServerReloaded = false;

public:
	UPROPERTY(config)
	TArray<FString> AllowedCharacters;

	UPROPERTY(config)
	TArray<FString> DisallowedCharacters;

	UPROPERTY(config, BlueprintReadOnly)
	bool bEnforceWhitelist = false;

	UPROPERTY(config, BlueprintReadOnly)
	bool bFamilySharing;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerDatabase;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerGlobalChat;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowChat;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowProximityVoiceChat;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowPartyVoiceChat;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerPrivate;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerPaidUsersOnly;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerNameTags;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerExperimental;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerExperimentalOptimizations;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowMap;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowMinimap;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllow3DMapMarkers;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAI;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerFish;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAnimationManager;

	// Anti Revenge Kill System
	UPROPERTY(config, BlueprintReadWrite)
	bool bServerAntiRevengeKill;

	UPROPERTY(config, BlueprintReadWrite)
	float RevengeKillDistance;

	// Water Quality System
	UPROPERTY(config, BlueprintReadOnly)
	bool bServerWaterQualitySystem;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideWaterRegeneration;

	UPROPERTY(config, BlueprintReadOnly)
	bool bEnableWaterRegeneration;

	UPROPERTY(config, BlueprintReadOnly)
	int32 WaterRegenerationRateMultiplierUpdate;

	UPROPERTY(config, BlueprintReadOnly)
	int32 WaterRegenerationRate;

	UPROPERTY(config, BlueprintReadOnly)
	int32 WaterRegenerationValue;

	UPROPERTY(config, BlueprintReadOnly)
	float WaterRainRegenerationIncrement;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerLocalWorldQuests;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerMinTimeBetweenExplorationQuest;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerWaystones;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowInGameWaystone;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerWaystoneCooldownRemoval;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideQuestContributionCleanup;

	UPROPERTY(config, BlueprintReadOnly)
	float QuestContributionCleanup;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideLocalQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	float LocalQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideLocationQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	float LocationQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideGroupQuestCleanup;

	UPROPERTY(config, BlueprintReadOnly)
	float GroupQuestCleanup;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideGroupMeetQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	float GroupMeetQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideTrophyQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	float TrophyQuestCooldown;

	UPROPERTY(config, BlueprintReadOnly)
	int32 MaxGroupQuests;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideMaxCompleteQuestsInLocation;

	UPROPERTY(config, BlueprintReadOnly)
	int32 MaxCompleteQuestsInLocation;

	UPROPERTY(config, BlueprintReadOnly)

	bool bEnableMaxUnclaimedRewards = true;
	
	// How many unclaimed quests a player can have before not being able to recieve more quests
	UPROPERTY(config, BlueprintReadOnly)
	int32 MaxUnclaimedRewards = 10;

	UPROPERTY(config, BlueprintReadOnly)
	bool bLoseUnclaimedQuestsOnDeath;

	UPROPERTY(config, BlueprintReadOnly)
	bool bTrophyQuests = true;

	UPROPERTY(config, BlueprintReadOnly)
	float HatchlingCaveExitGrowth;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerHomecaveCampingDebuff;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideHomecaveCampingDistance;

	UPROPERTY(config, BlueprintReadOnly)
	float HomecaveCampingDistance;

	UPROPERTY(config, BlueprintReadOnly)
	bool bOverrideHomecaveCampingDelay;

	UPROPERTY(config, BlueprintReadOnly)
	int32 HomecaveCampingDelay;

	// -1 (INDEX_NONE) - Cooldown uses defaults
	// 0 - No Cooldown
	// 1 - Override Cooldown
	UPROPERTY(config, BlueprintReadOnly)
	int OverrideWaystoneCooldown = INDEX_NONE;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerAIMax;

	UPROPERTY(config, BlueprintReadOnly)
	float ServerAIRate;

	// Maximum count of critters in the world.
	UPROPERTY(config, BlueprintReadOnly)
	float ServerMaxCritters;

	UPROPERTY(config, BlueprintReadOnly)
	float QuestGrowthMultiplier = 1.f;

	UPROPERTY(config, BlueprintReadOnly)
	float QuestMarksMultiplier = 1.f;

	UPROPERTY(config, BlueprintReadOnly)
	float GlobalPassiveGrowthPerMinute = 0.f;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerCritters;

	// The density of critters that will attempt to spawn around players.
	// Still limited by ServerMaxCritters
	UPROPERTY(config, BlueprintReadOnly)
	float ServerAICritterDensity;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerCritterWorldSpawning;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerCritterBurrowSpawning;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAIPlayerSpawns;

	UPROPERTY(config, BlueprintReadOnly)
	bool bPermaDeath;

	UPROPERTY(config, BlueprintReadOnly)
	bool bDeathInfo;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerGrowth;

	UPROPERTY(config)
	bool bLoseGrowthPastGrowthStages;

private:
	// See GetMinGrowthAfterDeath() implementation for private reason
	UPROPERTY(config)
	float MinGrowthAfterDeath;

public:
	UPROPERTY(config, BlueprintReadOnly)
	bool bServerWellRestedBuff;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerNesting;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerHatchlingCaves;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerHungerThirstInCaves;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerHealingInHomeCave;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerHomeCaves;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerEditAbilitiesInHomeCaves;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerDeadBodyTime;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerRespawnTime;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerLogoutTime;

	UPROPERTY(config, BlueprintReadOnly)
	int ServerFootprintLifetime;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerFallDamage;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowReplayRecording;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAutoRestart;

	UPROPERTY(config, BlueprintReadOnly)
	int32 RestartLengthInSeconds;

	UPROPERTY(config, BlueprintReadOnly)
	bool bUseScheduledRestartTimes;
	
	UPROPERTY(config, BlueprintReadOnly)
	TArray<int32> ScheduledRestartTimes;
	
	UPROPERTY(config, BlueprintReadOnly)
	TArray<int32> RestartNotificationTimestamps;

	UPROPERTY(config, BlueprintReadOnly)
	TArray<FCurveOverrideData> CurveOverrides;

	UPROPERTY(config, BlueprintReadOnly)
	int32 MaxCharactersPerPlayer;

	UPROPERTY(config, BlueprintReadOnly)
	int32 MaxCharactersPerSpecies;

	UPROPERTY(config, BlueprintReadOnly)
	float AFKDisconnectTime;

	// 0 - None, 1 - Log, 2 - Kick, 3 - Ban
	UPROPERTY(Config)
	int32 SpeedhackDetection = 1;

	// How many speedhack detections can be made per minute until an action is taken. Useful to ignore false positives
	UPROPERTY(Config)
	int32 SpeedhackThreshold = 10;
	
	// The lower limit of acceptable lifetime discrepancy. Players lower than this will cause reports. Set to 0.0 to remove 
	UPROPERTY(Config)
	float SpeedhackMinimumLifetimeDiscrepancy = -50.0f;
	
	// The upper limit of acceptable lifetime discrepancy. Players higher than this will cause reports. Set to 0.0 to remove 
	UPROPERTY(Config)
	float SpeedhackMaximumLifetimeDiscrepancy = 20.0f;
	
	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowChangeSubspecies;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerAllowAnselMultiplayerPausing;

	UPROPERTY(config, BlueprintReadOnly)
	int32 ServerAnselCameraConstraintDistance = 500;

	UPROPERTY(config, BlueprintReadOnly)
	bool bMustCollectItemsWithinPOI;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerCombatTimerAppliesToGroup;

	// Min/Max Time in minutes that the weather will stay active (0.5, 3 = minimum of 30 SECONDS and maximum of 3 minutes till the next weather type is chosen)
	UPROPERTY(config, BlueprintReadOnly)
	FVector2D WeatherLengthVariation;

	// Min/Max Time in minutes that the weather will take to blend to its target (0.5, 3 = minimum of 30 SECONDS and maximum of 3 minutes till it fully blends too target)
	UPROPERTY(config, BlueprintReadOnly)
	FVector2D WeatherBlendVariation;

	UFUNCTION(BlueprintPure)
	const TArray<FAttributeCapData>& GetAttributeCapsConfig() const;

	UPROPERTY(config, BlueprintReadOnly)
	TMap<EDamageEffectType, int32> MaximumDamageEffectsPerType{
			{ EDamageEffectType::BLEED, 5 },
			{ EDamageEffectType::VENOM, 5 },
			{ EDamageEffectType::POISONED, 5 },
			{ EDamageEffectType::BROKENBONE, 1 }
	};
protected:
	UPROPERTY(Config)
	TArray<FString> ServerAdmins;

	// New Bans, Server Mutes, Whitelisting
	TArray<FPlayerBan> Bans;
	TArray<FPlayerMute> ServerMutes;
	TArray<FAlderonPlayerID> ServerWhitelist;

	// Old
	UPROPERTY(Config)
	TArray<FBanInfo> BannedUsers;

	// Users in this array are not saved.  It's used when kicking a player from an instance.  They don't get to come back.
	UPROPERTY()
	TArray<FBanInfo> InstanceBannedUsers;

	// Admins in this array are not saved. It's used to adding temp people when they are a admin of a steam group
	UPROPERTY()
	TArray<FString> InstanceServerAdmins;

	// Devs in this array are not saved. It's used when doing dev functionality from a instance. Dev Steam IDs are hard coded into the game source
	UPROPERTY()
	TArray<FString> InstanceGameDevs;

protected:
	UPROPERTY(config)
	int CombatDeathMarksPenaltyPercent = 25;

	UPROPERTY(config)
	int CombatDeathGrowthPenaltyPercent = 10;

	UPROPERTY(config)
	int FallDeathMarksPenaltyPercent = 5;

	UPROPERTY(config)
	int FallDeathGrowthPenaltyPercent = 2;

	UPROPERTY(config)
	int SurvivalDeathMarksPenaltyPercent = 10;

	UPROPERTY(config)
	int SurvivalDeathGrowthPenaltyPercent = 5;

	UPROPERTY(config, BlueprintReadOnly)
	int ChangeSubspeciesGrowthPenaltyPercent = 25;
	
	UPROPERTY(BlueprintReadOnly)
	TArray<FAttributeCapData> AttributeCapsConfig
	{
		{ "BleedingRate", 3.f },
		{ "VenomRate", 2.f },
		{ "PoisonRate", 2.f },
		{ "LegDamage", 30.f }
	};
	
	// Vanilla UE4 Sessions
protected:
	/** Delegate for creating a new session */
	FOnCreateSessionCompleteDelegate OnCreateSessionCompleteDelegate;
	/** Delegate after starting a session */
	FOnStartSessionCompleteDelegate OnStartSessionCompleteDelegate;
	/** Delegate for destroying a session */
	FOnDestroySessionCompleteDelegate OnDestroySessionCompleteDelegate;
	/** Delegate for searching for sessions */
	FOnFindSessionsCompleteDelegate OnFindSessionsCompleteDelegate;
	/** Delegate after joining a session */
	FOnJoinSessionCompleteDelegate OnJoinSessionCompleteDelegate;

	/** Transient properties of a session during game creation/matchmaking */
	FIGameSessionParams CurrentSessionParams;
	/** Current host settings */
	TSharedPtr<class FIOnlineSessionSettings> HostSettings;
	/** Current search settings */
	TSharedPtr<class FIOnlineSearchSettings> SearchSettings;

	/**
	 * Delegate fired when a session create request has completed
	 *
	 * @param SessionName the name of the session this callback is for
	 * @param bWasSuccessful true if the async action completed without error, false if there was an error
	 */
	virtual void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * Delegate fired when a session start request has completed
	 *
	 * @param SessionName the name of the session this callback is for
	 * @param bWasSuccessful true if the async action completed without error, false if there was an error
	 */
	void OnStartOnlineGameComplete(FName SessionName, bool bWasSuccessful);

	/**
	 * Delegate fired when a session search query has completed
	 *
	 * @param bWasSuccessful true if the async action completed without error, false if there was an error
	 */
	void OnFindSessionsComplete(bool bWasSuccessful);

	/**
	 * Delegate fired when a session join request has completed
	 *
	 * @param SessionName the name of the session this callback is for
	 * @param bWasSuccessful true if the async action completed without error, false if there was an error
	 */
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	/**
	 * Delegate fired when a destroying an online session has completed
	 *
	 * @param SessionName the name of the session this callback is for
	 * @param bWasSuccessful true if the async action completed without error, false if there was an error
	 */
	virtual void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	/*
	 * Event triggered when a presence session is created
	 *
	 * @param SessionName name of session that was created
	 * @param bWasSuccessful was the create successful
	 */
	DECLARE_EVENT_TwoParams(AIGameSession, FOnCreatePresenceSessionComplete, FName /*SessionName*/, bool /*bWasSuccessful*/);
	FOnCreatePresenceSessionComplete CreatePresenceSessionCompleteEvent;

	/*
	 * Event triggered when a session is joined
	 *
	 * @param SessionName name of session that was joined
	 * @param bWasSuccessful was the create successful
	 */
	DECLARE_EVENT_OneParam(AIGameSession, FOnJoinSessionComplete, EOnJoinSessionCompleteResult::Type /*Result*/);
	FOnJoinSessionComplete JoinSessionCompleteEvent;

	/*
	 * Event triggered after session search completes
	 */
	DECLARE_EVENT_OneParam(AIGameSession, FOnFindSessionsComplete, bool /*bWasSuccessful*/);
	FOnFindSessionsComplete FindSessionsCompleteEvent;

public:
	/** Default number of players allowed in a game */
	static const int32 DEFAULT_NUM_PLAYERS = 8;

	/**
	 * Host a new online session
	 *
	 * @param UserId user that initiated the request
	 * @param SessionName name of session
	 * @param bIsLAN is this going to hosted over LAN
	 * @param bIsPresence is the session to create a presence session
	 * @param MaxNumPlayers Maximum number of players to allow in the session
	 *
	 * @return bool true if successful, false otherwise
	 */
	bool HostSession(TSharedPtr<const FUniqueNetId> UserId, FName SessionName, const FString& GameType, const FString& MapName, bool bIsLAN, bool bIsPresence, int32 MaxNumPlayers, FString ServerKey = "");

	/**
	 * Host a new online session with specified settings
	 *
	 * @param UserId user that initiated the request
	 * @param SessionName name of session
	 * @param SessionSettings settings to create session with
	 *
	 * @return bool true if successful, false otherwise
	 */
	bool HostSession(const TSharedPtr<const FUniqueNetId> UserId, const FName SessionName, const FOnlineSessionSettings& SessionSettings);

	/**
	 * Find an online session
	 *
	 * @param UserId user that initiated the request
	 * @param SessionName name of session this search will generate
	 * @param bIsLAN are we searching LAN matches
	 * @param bIsPresence are we searching presence sessions
	 */
	void FindSessions(TSharedPtr<const FUniqueNetId> UserId, FName SessionName, bool bIsLAN, bool bIsPresence, FString OverrideKeyword = "");

	/**
	 * Joins one of the session in search results
	 *
	 * @param UserId user that initiated the request
	 * @param SessionName name of session
	 * @param SessionIndexInSearchResults Index of the session in search results
	 *
	 * @return bool true if successful, false otherwise
	 */
	bool JoinSession(TSharedPtr<const FUniqueNetId> UserId, FName SessionName, int32 SessionIndexInSearchResults);

	/**
	 * Joins a session via a search result
	 *
	 * @param SessionName name of session
	 * @param SearchResult Session to join
	 *
	 * @return bool true if successful, false otherwise
	 */
	bool JoinSession(TSharedPtr<const FUniqueNetId> UserId, FName SessionName, const FOnlineSessionSearchResult& SearchResult);

	/**
	 * Get the search results found and the current search result being probed
	 *
	 * @param SearchResultIdx idx of current search result accessed
	 * @param NumSearchResults number of total search results found in FindGame()
	 *
	 * @return State of search result query
	 */
	EOnlineAsyncTaskState::Type GetSearchResultStatus(int32& SearchResultIdx, int32& NumSearchResults);

	/**
	 * Get the search results.
	 *
	 * @return Search results
	 */
	const TArray<FOnlineSessionSearchResult> & GetSearchResults() const;

	/** @return the delegate fired when creating a presence session */
	FOnCreatePresenceSessionComplete& OnCreatePresenceSessionComplete() { return CreatePresenceSessionCompleteEvent; }

	/** @return the delegate fired when joining a session */
	FOnJoinSessionComplete& OnJoinSessionComplete() { return JoinSessionCompleteEvent; }

	/** @return the delegate fired when search of session completes */
	FOnFindSessionsComplete& OnFindSessionsComplete() { return FindSessionsCompleteEvent; }

	/** Handle starting the match */
	virtual void HandleMatchHasStarted() override;

	/** Handles when the match has ended */
	virtual void HandleMatchHasEnded() override;

	/**
	 * Travel to a session URL (as client) for a given session
	 *
	 * @param ControllerId controller initiating the session travel
	 * @param SessionName name of session to travel to
	 *
	 * @return true if successful, false otherwise
	 */
	bool TravelToSession(int32 ControllerId, FName SessionName);

	/** Handles to various registered delegates */
	FDelegateHandle OnStartSessionCompleteDelegateHandle;
	FDelegateHandle OnCreateSessionCompleteDelegateHandle;
	FDelegateHandle OnDestroySessionCompleteDelegateHandle;
	FDelegateHandle OnFindSessionsCompleteDelegateHandle;
	FDelegateHandle OnJoinSessionCompleteDelegateHandle;

private:
	int32 PIDCount;

public:
	FString OverrideServerListIP = "";
	int32 OverrideServerListPort = 0;
};

