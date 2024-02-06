// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "GameFramework/GameMode.h"
#include "ITypes.h"
#include "AgonesComponent.h"
#include "AlderonDatabaseBase.h"
#include "ChatCommands/IChatCommand.h"
#include "CaveSystem/IPlayerCaveMain.h"
#include "IGameMode.generated.h"

class AICharSelectPoint;
class AINest;
class AIBaseCharacter;
class AIDinosaurCharacter;
class AIPlayerController;
class UAgonesComponent;
class AIPlayerStart; 
class UAlderonMetricEvent;

UENUM(BlueprintType)
enum class EManagedServer : uint8
{
	None	UMETA(DisplayName = "None"),
	Edgegap	UMETA(DisplayName = "Edgegap"),
	Agones	UMETA(DisplayName = "Agones"),
	Reserved_1	UMETA(DisplayName = "Reserved (1)"),
	Reserved_2	UMETA(DisplayName = "Reserved (2)"),
	Reserved_3	UMETA(DisplayName = "Reserved (3)"),
	Reserved_4	UMETA(DisplayName = "Reserved (4)")
};

USTRUCT(BlueprintType)
struct FSpawnPointParameters
{
	GENERATED_BODY()

	// An array of tags to check against spawn points. 
	// If a tag is not mapped to any valid spawns, it will try the next.
	UPROPERTY(BlueprintReadWrite)
	TArray<FName> TargetTags{};
	
	// If non-zero, will be used to exclude spawn points within a radius of the previous death.
	UPROPERTY(BlueprintReadWrite)
	FVector PreviousDeathLocation = FVector::ZeroVector;
};

DECLARE_DELEGATE_OneParam(FAsyncOperationCompleted, bool);

DECLARE_DYNAMIC_DELEGATE_TwoParams(FAsyncCharacterCreated, const AIPlayerController*, PlayerController, FAlderonUID, CharacterUID);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAsyncCharacterSpawned, const AIPlayerController*, PlayerController, const AIBaseCharacter*, Character);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAsyncCharacterDeleted, const AIPlayerController*, PlayerController, bool, bSuccess);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAsyncCharactersLoaded, const AIPlayerController*, PlayerController, const TArray<FAlderonUID>&, Characters);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FAsyncCharactersMerged, const AIPlayerController*, PlayerController, bool, bSuccess);

DECLARE_MULTICAST_DELEGATE(FWaitForDatabaseWrite);

DECLARE_DELEGATE_OneParam(FInstancedTileSpawned, FInstancedTile);

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API AIGameMode : public AGameMode
{
	GENERATED_BODY()

	// Metrics
public:
	UFUNCTION(BlueprintCallable, Category = IGameMode)
	UAlderonMetricEvent* CreateMetricEventForPlayer(const FString& EventName, AIPlayerState* IPlayerState);

public:
	EManagedServer ManagedServerType = EManagedServer::None;
	bool bManagedServerConnected = false;
	bool bManagedServerRegister = false;
	bool bManagedServerReady = false;

	bool bLockingLevelLoadCompleted = false;

	void CheckServerRegisterReady();

protected:
	// Edgegap and Agones
	void Edgegap_ContextCallback(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

public:
	// Otp - Encryption Handling
	void CacheOTP(FString Otp, FString Token, FAlderonServerUserDetails UserInfo, int32 CacheExpire = 60);

	void TickOTPCache();
	static FString SanitizeStringForLogging(const FString& Options, FString& OutSanitized);

	FOTPCache FetchOtpCache(FString Token);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UAgonesComponent* AgonesSDK;

	UFUNCTION()
	void Agones_CallGameServer();

	UFUNCTION()
	void Agones_CallAllocate();

	UFUNCTION()
	void Agones_CallReady();

	UFUNCTION()
	void Agones_Allocated();

	UFUNCTION()
	void Agones_ReadySuccess(const FEmptyResponse& Response);

	UFUNCTION()
	void Agones_ReadyError(const FAgonesError& Error);

	UFUNCTION()
	void Agones_GameServerSuccess(const FGameServerResponse& Response);

	UFUNCTION()
	void Agones_GameServerError(const FAgonesError& Error);

public:

	/** The default pawn class used by the PlayerController for players when using admin tools. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Classes)
	TSoftClassPtr<class AIAdminCharacter> DefaultAdminClassRef;

protected:

	AIGameMode(const FObjectInitializer& ObjectInitializer);

	virtual void PreInitializeComponents() override;

	/** Returns game session class to use */
	virtual TSubclassOf<AGameSession> GetGameSessionClass() const override;

	virtual void StartPlay() override;

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void InitGameState();

	virtual void DefaultTimer();

	void MapReloadTimer();
	bool bMapLoaded = false;
	FTimerHandle TimerHandle_MapReloadTimer;

	UFUNCTION()
	virtual void ProcessBECommand(const FString& String);

	// Stats System
protected:

	uint8 StatPerformanceCount = 0;

	TArray<uint8, TFixedAllocator<60>> PerformanceSamples;

	virtual void StatsUpdate();
	virtual void StatsPrint();

	FTimerHandle TimerHandle_StatsUpdate;
	FTimerHandle TimerHandle_StatsPrint;

protected:

	static inline const TArray<FString> LogForbiddenFields = {TEXT("access_token"), TEXT("refresh_token"), TEXT("otp"), TEXT("token"), TEXT("EncryptionToken"), TEXT("Pass")};
	
	virtual void StartMatch();
	virtual void HandleMatchHasStarted() override;

	/* Handle for efficient management of DefaultTimer timer */
	FTimerHandle TimerHandle_DefaultTimer;

	/* Called once on every new player that enters the gamemode */
	virtual FString InitNewPlayer(class APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;

	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;

	/************************************************************************/
	/* Actor Interface                                                      */
	/************************************************************************/
public:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void BeginDestroy() override;

	/************************************************************************/
	/* Player Spawning                                                      */
	/************************************************************************/
protected:
	/* Don't allow spectating of bots */
	virtual bool CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget) override;

	/** returns default pawn class for given controller */
	//virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Player Spawning")
	bool GetSavedLocationRotation(const FString& ID, FVector& OutLocation, FRotator& OutRotation);
public:
	/************************************************************************/
	/* Damage & Killing                                                     */
	/************************************************************************/
	virtual void Killed(AController* const Killer, AController* VictimPlayer, APawn* const VictimPawn, const EDamageType DamageType);

	// Can the player deal damage according to gamemode rules (eg. friendly-fire disabled) 
	virtual bool CanDealDamage(class AIPlayerState* DamageCauser, class AIPlayerState* DamagedPlayer) const;

	virtual float ModifyDamage(float Damage, AActor* DamagedActor, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) const;

	// UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "Damage")
	// float GetModifiedDamageForWeightClasses(const EDinosaurWeightClass DamagedDinoWeightClass, const EDinosaurWeightClass DamageCauserDinoWeightClass, const float Damage) const;
	// virtual float GetModifiedDamageForWeightClasses_Implementation(const EDinosaurWeightClass DamagedDinoWeightClass, const EDinosaurWeightClass DamageCauserDinoWeightClass, const float Damage) const;

	/************************************************************************/
	/* Game Mode Settings                                                   */
	/************************************************************************/
public:
	// Database
	UPROPERTY()
	class UIAlderonDatabaseBase* DatabaseEngine;

protected:
	UPROPERTY(BlueprintReadOnly, Category = Database)
	bool bDatabaseSetup;

	UPROPERTY(config)
	FString DatabaseHost;

	UPROPERTY(config)
	int DatabasePort;

	UPROPERTY(config)
	FString DatabaseUser;

	UPROPERTY(config)
	FString DatabasePass;

	UPROPERTY(config)
	bool Multithreading;

	/************************************************************************/
	/* New Database Engine                                                  */
	/************************************************************************/
	// Setup Database / Connect
public:
	void SetupDatabase();

protected:

	/************************************************************************/
	/* UWS                                                                  */
	/************************************************************************/

	UFUNCTION(BlueprintCallable, Category = UWS)
	bool IsWebServerEnabled();

	UFUNCTION(BlueprintCallable, Category = UWS)
	int GetWebServerPort();

	UFUNCTION(BlueprintCallable, Category = UWS)
	FString GetWebServerPassword();

	UPROPERTY(BlueprintReadOnly, Category = UWS)
	FString WebServerDocumentRoot = TEXT("WebServer");

	UPROPERTY(BlueprintReadWrite, Category = UWS)
	class UWebServer* UWSInstance = nullptr;

	UFUNCTION(BlueprintCallable, Category = UWS)
	void HandleWebServerConnection(class UConnection* Connection);

	bool IsWebServerConnectionValidated(class UConnection* Connection);

	FString WebServerGeneratedToken = TEXT("");


	// Load Nests
	void LoadAllNests();

	// Player Login
	virtual void PostLogin(APlayerController* NewPlayer) override;

	UFUNCTION()
	void HandleSpawnCharacter(AIPlayerController* PlayerController, const FCharacterSelectRow& CharacterData);

public:
	AIPlayerState* FindPlayerStateViaAGID(const FAlderonPlayerID& AlderonId);

	UFUNCTION(BlueprintNativeEvent, Category = Player)
	void OnPlayerReady(AIPlayerController* ReadyPlayer);

	UFUNCTION(BlueprintImplementableEvent, Category = Player)
	void OnPlayerLeave(AIPlayerController* ReadyPlayer);

	UFUNCTION(BlueprintImplementableEvent, Category = Player)
	void OnPlayerJoin(AIPlayerController* ReadyPlayer);

	UFUNCTION(BlueprintNativeEvent, Category = Player)
	void OnPlayerSpawn(AIPlayerController* Player, AIBaseCharacter* Character, bool bOverrideSpawn, const FTransform& Transform);

	UFUNCTION(BlueprintNativeEvent, Category = Player)
	void OnPlayerSpawnInstance(AIPlayerController* Player, AIBaseCharacter* Character, bool bOverrideSpawn, const FTransform& Transform, AIPlayerCaveBase* Instance);

	UFUNCTION(BlueprintNativeEvent, Category = Player)
	void OnPlayerSpawnWaypoint(AIPlayerController* Player, const FAlderonUID& CharacterID, const FTransform& Transform, const FWaystoneInvite& WaystoneInvite);

	// @brief A hook to character spawning.
	// @return True if the character should continue spawning. False if the character spawn should be stopped.
	UFUNCTION(BlueprintNativeEvent, Category = Player)
	bool OnPlayerTrySpawn(AIPlayerController* Player, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, const FAsyncCharacterSpawned& OnSpawn);

	UFUNCTION(BlueprintNativeEvent, Category = Player)
	bool OnCharacterTakeDamage(AIBaseCharacter* Target, AIBaseCharacter* Source, EDamageType DamageType, float Damage, TMap<EDamageEffectType, float>& OtherDamage);

	UFUNCTION(BlueprintImplementableEvent, Category = Player)
	void OnCharacterDie(AIBaseCharacter* Target, AIBaseCharacter* Source, float Damage);

	UFUNCTION(BlueprintImplementableEvent, Category = Player)
	void OnCharacterSpawn(AIPlayerController* Player, AIBaseCharacter* Character);

	virtual void RestartGame() override;

	bool ShouldSpawnCombatLogAI();

	UFUNCTION(BlueprintCallable, Category = GameMode)
	void CreateCharacter(AIPlayerController* PlayerController, FString Name, const UCharacterDataAsset* CharacterDataAsset, const USkinDataAsset* SkinDataAsset, bool bGender, float Growth, FAsyncCharacterCreated OnCompleted);

	void CreateCharacterAsync(AIPlayerController* PlayerController, FCharacterData CharacterData, FAsyncCharacterCreated OnCompleted = FAsyncCharacterCreated());
    
	void EditCharacterAsync(AIPlayerController* PlayerController, FCharacterData CharacterData, FAsyncCharacterCreated OnCompleted = FAsyncCharacterCreated());
	void FinishEditingCharacter(AIPlayerController* PlayerController, UCharacterDataAsset* CharacterDataAsset, USkinDataAsset* SkinDataAsset, TSharedPtr<FStreamableHandle> Handle, FCharacterData CharacterData, FAsyncCharacterCreated OnCreateCompleted = FAsyncCharacterCreated());

	void FinishCreatingCharacter(AIPlayerController* PlayerController, UCharacterDataAsset* CharacterDataAsset, USkinDataAsset* SkinDataAsset, FCharacterData CharacterData, TSharedPtr<FStreamableHandle> Handle = TSharedPtr<FStreamableHandle>(), FAsyncCharacterCreated OnCreateCompleted = FAsyncCharacterCreated());

	UFUNCTION(BlueprintCallable, Category = GameMode)
	void SpawnCharacterForPlayer(AIPlayerController* PlayerController, FAlderonUID CharacterUID, const FTransform& Transform, FAsyncCharacterSpawned OnSpawn);

	// Calls SpawnCharacter_Internal, wrapped in OnPlayerTrySpawn to allow mods to hook spawning.
	void SpawnCharacter(AIPlayerController* PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn = false, FTransform OverrideTransform = FTransform(), FAsyncCharacterSpawned OnSpawn = FAsyncCharacterSpawned());

	// Spawns character, does not call OnPlayerTrySpawn
	UFUNCTION(BlueprintCallable, Category = GameMode)
	void SpawnCharacter_Internal(AIPlayerController* PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn);

	void SpawnCharacter_Stage1_DBWrite(TWeakObjectPtr<AIBaseCharacter> Character, FAlderonUID CharacterUID, FDatabaseLoadCharacterCompleted OnCompleted);

	void SpawnCharacter_Stage1(bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset, TArray<FPrimaryAssetId> SkinAssetIds, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle, TWeakObjectPtr<AIPlayerController> PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn = FAsyncCharacterSpawned());
	void SpawnCharacter_Stage2(const FDatabaseLoadCharacter& Data, TWeakObjectPtr<AIBaseCharacter> Character, TWeakObjectPtr<AIPlayerController> PlayerController, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn = FAsyncCharacterSpawned());

	// Added option to supply custom AdminClassRef. Set to nullptr to use the default.
	void LoadAdminAsync(AIPlayerController* PlayerController, TSoftClassPtr<AIAdminCharacter> AdminClassRef);
	void SpawnAdmin(AIPlayerController* PlayerController, TSoftClassPtr<AIAdminCharacter> AdminClassRef);

	UFUNCTION(BlueprintCallable, Category = GameMode)
	void GetAllCharacters(const AIPlayerController* PlayerController, FAsyncCharactersLoaded OnComplete);
	void GetAllCharacters_Callback(const FDatabaseLoadCharacters& DatabaseLoadCharacters, const AIPlayerController* PlayerController, FAsyncCharactersLoaded OnComplete);

	UFUNCTION(BlueprintCallable, Category = GameMode, DisplayName = "Delete Character")
	void DeleteCharacterBP(AIPlayerController* PlayerController, FAlderonUID CharacterUID, FAsyncCharacterDeleted OnCompleted);
	void DeleteCharacter(AIPlayerController* PlayerController, FAlderonUID CharacterUID, FAsyncOperationCompleted OnCompleted);
	void DeleteCharacter_Callback(const FDatabaseOperationData& Data, TWeakObjectPtr<AIPlayerState> WeakPlayerStatePtr, FAlderonUID CharacterUID, FAsyncOperationCompleted OnCompleted);

	void MergeCharacters(AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnComplete);
	void MergeCharacters_CharacterToDeleteLoaded(const FDatabaseLoadCharacter& CharacterToDeleteLoadData, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted);
	void MergeCharacters_CharacterToReceiveLoaded(const FDatabaseLoadCharacter& CharacterToReceiveLoadData, int MarksToTransfer, TArray<FHomeCaveDecorationPurchaseInfo> DecorationsToTransfer, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted);
	void MergeCharacters_CharacterToReceiveSaved(const FDatabaseOperationData& SaveOperationData, AIDinosaurCharacter* Character, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted);
	void MergeCharacters_CharacterToDeleteDeleted(const FDatabaseOperationData& DeleteOperationData, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted);

	UFUNCTION()
	void LoadPlayerStateCompleted(const FDatabaseOperationData& Data, AIPlayerController* IPlayerController);

	UFUNCTION(BlueprintCallable, Category = Database)
	void LoadCharacter(AIPlayerController* PlayerController);

	void LoadCharacter_Stage1(const FDatabaseLoadCharacters& Data, TWeakObjectPtr<AIPlayerState> IPlayerStateWeakPtr, TArray<FAlderonUID> LoadTimeRevengeKillFlags);
	void LoadCharacter_Stage2(bool bSuccess, FPrimaryAssetId LoadedAssetId, UCharacterDataAsset* LoadedAsset, TWeakObjectPtr<AIPlayerState> IPlayerStateWeakPtr, FAlderonUID CharacterUID, FCharacterData CharacterData);

	void SubmitCharacterData(AIPlayerState* IPlayerState);
	void OnCharacterDataLoaded(AIPlayerState* IPlayerState, UCharacterDataAsset* CharacterDataAsset, FAlderonUID CharacterUID, FCharacterData CharacterData);

	UFUNCTION(BlueprintCallable, Category = Database)
	void SaveAll(AIPlayerController* PlayerController, const ESavePriority Priority = ESavePriority::Low);
	void SaveAllAsync(AIPlayerController* PlayerController, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority = ESavePriority::Low);

	void PrepareCharacterForSave(AIBaseCharacter* TargetCharacter);

	void SaveCombatLogAI(AIBaseCharacter* CombatLogAI, bool bDestroyWhenDone = false);

	TMap<FAlderonUID, FWaitForDatabaseWrite> CharacterSavesInProgress;
	TMap<FAlderonUID, FWaitForDatabaseWrite> PendingRevengeKillFlags;

	UFUNCTION(BlueprintCallable, Category = Database)
	void SaveCharacter(AIBaseCharacter* TargetCharacter, const ESavePriority Priority = ESavePriority::Low);

	void SaveCharacterAsync(AIBaseCharacter* TargetCharacter, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority = ESavePriority::Low);

	// if bSaveAllCreatorModeObjects = true, then all creator objects will be saved regardless of being dirty
	UPROPERTY(Config, BlueprintReadOnly)
	bool bSaveAllCreatorModeObjects = false;
	UPROPERTY(Config, BlueprintReadOnly)
	FString DefaultCreatorModeSave;

	void SaveCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback);
	void LoadCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback = FAsyncChatCommandCallback());
	void ResetCreatorModeObjects();
	void RemoveCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback);
	void ListCreatorModeSaves(FAsyncChatCommandCallback Callback);

	void SpawnCreatorModeActor(TSoftClassPtr<AActor> ClassSoftPtr, TSharedPtr<FJsonObject> JsonObject);
	void GetDirtyCreatorModeObjects(TArray<FDatabaseBunchEntry>& Entries, bool bGetAll = false);
	void RestoreOriginalCreatorModeObject(class UICreatorModeObjectComponent* CMOComp);

	static const int MaxCreatorSaves = 10;

private:
	bool bCreatorModePendingSave = false;

	void RemoveCreatorModeObjects_Stage2(const FDatabaseLoad& Data, FString SaveName, FAsyncChatCommandCallback Callback);
	void SaveCreatorModeObjects_Stage2(const FDatabaseLoad& Data, FString SaveName, FAsyncChatCommandCallback Callback);
	void ListCreatorModeSaves_Stage2(const FDatabaseLoad& Data, FAsyncChatCommandCallback Callback);
	void SaveCreatorModeSaveList(TSharedPtr<FJsonObject> SaveList);
	// Add Login Effect
	void AddLoginDebuffToCharacter(const AIBaseCharacter& IBaseCharacter, bool bServerAntiRevengeKill, bool bWasPreviouslyAdminCharacter);

public:
	// Save the default properties of all the creator mode actors on the map so we can restore them later if needed
	TMap<FString, TSharedPtr<FJsonObject>> DefaultCreatorModeActors;

	FString GetCreatorModeSavePath();

	UFUNCTION(BlueprintCallable, Category = Database)
	void SavePlayerState(AIPlayerState* TargetPlayerState, const ESavePriority Priority = ESavePriority::Low);

	void SavePlayerStateAsync(AIPlayerState* TargetPlayerState, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority = ESavePriority::Low);

	UFUNCTION(BlueprintCallable, Category = Database)
	void SaveNest(AINest* Nest);

	UFUNCTION(BlueprintCallable, Category = Database)
	void DeleteNest(AINest* Nest);

	// Called on Shutdown to save all the players in the game
	bool SaveAllPlayers();
	bool SaveAllNests();

	void ShutdownDatabase();

	void PreLogout(APlayerController* Controller, APawn* Pawn);

	virtual void Logout(AController* Exiting) override;

	UFUNCTION(BlueprintCallable)
	virtual void AddInactivePlayerBlueprint(APlayerState* PlayerState, APlayerController* PC);

	virtual void AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC);

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	FString DisplayName;

	/************************************************************************/
	/* Anti Revenge Killing                                                 */
	/************************************************************************/
public:
	void FlagRevengeKill(const FAlderonUID& SkipCharacterId, AIPlayerState* IPlayerState, FVector RevengeKillLocation);

	/************************************************************************/
	/* Combat Logging                                                       */
	/************************************************************************/
public:
	void AddCombatLogAI(AIBaseCharacter* Character, const FAlderonUID& CharacterId);
	void RemoveCombatLogAI(const FAlderonUID& CharacterId, bool bDestroy = true, bool bRemoveTimestamp = true, bool bSave = false);

	void RemoveCombatLogTimestamp(const FAlderonUID& CharacterId);

	AIBaseCharacter* GetCombatLogAI(const FAlderonUID& CharacterId);

	TMap<FAlderonUID, AIBaseCharacter*> GetCombatLogAIs() { return CombatLogAI; };

protected:
	UPROPERTY()
	TMap<FAlderonUID, AIBaseCharacter*> CombatLogAI;

	// Dynamic Time of Day Settings
public:
	UPROPERTY(Config, BlueprintReadOnly)
	float ServerStartingTime;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerDynamicTimeOfDay;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerRestrictCarnivoreGrouping;

	UPROPERTY(config, BlueprintReadOnly)
	bool bServerRestrictHerbivoreGrouping;

	UPROPERTY(config, BlueprintReadOnly)
	float ServerDayLength;

	UPROPERTY(config, BlueprintReadOnly)
	float ServerNightLength;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = GameMode)
	EGameModes GameModeType;

	/************************************************************************/
	/* Dinosaur Server Options                                              */
	/************************************************************************/
public:
	UPROPERTY(config, BlueprintReadOnly)
	TArray<FString> DisabledDinosaurs;

	// Max slots a group can have before being considered full
	UPROPERTY(config, BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	int32 MaxGroupSize;

	// Only group members within this distance from the group leader can see where other members are
	UPROPERTY(config, BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	int32 MaxGroupLeaderCommunicationDistance;

	// Max radius for randomly picking a spawn point from furthest spawn location from other players
	UPROPERTY(config, BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	float FurthestSpawnInclusionRadius;

	// Min radius, for randomly selected spawn points, within which there are no other players
	UPROPERTY(config, BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	float PotentialSpawnExclusionRadius;

	// Don't spawn within 1km of last death.
	UPROPERTY(config, BlueprintReadOnly, EditDefaultsOnly, Category = GameMode)
	float ExcludeSpawnNearDeathRadius = 100000.0f; 

	/************************************************************************/
	/* Dinosaur Server Options                                              */
	/************************************************************************/
	UPROPERTY(BlueprintReadOnly, Category = GameMode)
	TArray<FVector> GenericSpawnPoints;

	UPROPERTY(BlueprintReadOnly, Category = GameMode)
	TArray<class APlayerStart*> CustomSpawnPoints;

	UFUNCTION(BlueprintCallable, Category = GameMode)
	TArray<class APlayerStart*> GetSpawnPointsWithTag(FName SpawnTag) const;

	float GetDistanceToClosestPlayer(FVector TargetLocation);
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindRandomSpawnPointWithParams(const FSpawnPointParameters& Params);
	virtual FTransform FindRandomSpawnPointWithParams_Implementation(const FSpawnPointParameters& Params);

	UFUNCTION(BlueprintCallable, Category = GameMode)
	FTransform FindRandomSpawnPoint();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindRandomSpawnPointForTag(FName SpawnTag = NAME_None, FName FallbackTag = NAME_None);
	FTransform FindRandomSpawnPointForTag_Implementation(FName SpawnTag, FName FallbackTag);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindRandomSpawnPointForCharacter(const AIBaseCharacter* Character, const AIPlayerState* PlayerState, const AIPlayerController* PendingController = nullptr);
	FTransform FindRandomSpawnPointForCharacter_Implementation(const AIBaseCharacter* Character, const AIPlayerState* PlayerState, const AIPlayerController* PendingController = nullptr);
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindRandomGenericSpawnPoint();
	virtual FTransform FindRandomGenericSpawnPoint_Implementation();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindGenericSpawnPointFurthestFromPlayers();
	virtual FTransform FindGenericSpawnPointFurthestFromPlayers_Implementation();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	FTransform FindGenericSpawnPointWithLessPlayersNearby();
	virtual FTransform FindGenericSpawnPointWithLessPlayersNearby_Implementation();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = GameMode)
	bool ShouldUseRandomSpawnPoint() const;
	virtual bool ShouldUseRandomSpawnPoint_Implementation() const;

private:
	bool ValidatePotentialSpawnPoint(const APlayerStart* PlayerStart, const APlayerStart* FurthestPlayerStart);

protected:
	UPROPERTY()
	AICharSelectPoint* CharSelectPoint;

private:
	bool bRelaunchRequired;

public:
	AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

public:
	void SaveDataForShutdown();

protected:
	FTimerHandle TimerHandle_RestartDelay;
	FTimerHandle TimerHandle_RestartNoticeDelay;
	int32 LastRestartNoticeTimestamp = -1;
	
	bool bSavedDataForShutdown = false;

	void ProcessRestart();
	void SendRestartNotices(bool bForce = false);
	void StartRestartNoticeTimer();

public:

	void StartRestartTimer(int32 Seconds);
	void StopAutoRestart();

protected:
	UPROPERTY(EditDefaultsOnly, Category = Group)
	TSubclassOf<class AIPlayerGroupActor> PlayerGroupActor;

public:
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Group)
	EAcceptInviteResult CanJoinGroup(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember, bool bNewGroup = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Group)
	EAcceptInviteResult CanAcceptWaystone(AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Group)
	bool EnoughSlotsInGroup(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember, bool bNewGroup = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Group)
	bool GroupAllowsCharacterType(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember);

	UFUNCTION(BlueprintCallable, Category = Group)
	void ProcessGroupInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState, bool bAccepted);

	void SendJoinOrLeaveGroupWebhook(const AIPlayerState* const MovedPlayerState, const AIPlayerState* const GroupLeaderPlayerState, const bool bJoined) const;

	UFUNCTION(BlueprintCallable, Category = Group)
	void ProcessWaystoneInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState, bool bAccepted, bool bTimedOut, FName WaystoneTag);

	void RejectWaystoneInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState);
	void RejectGroupInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState);
	void CreateNewGroup(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState);

	UFUNCTION(BlueprintCallable, Category = Group)
	void DisbandGroup(AIPlayerState* SourcePlayerState, AIPlayerGroupActor* CurrentPlayerGroupActor);

public:

	void AsyncSpawnInstancedTile(AActor* InOwner, FPrimaryAssetId TileId, FInstancedTileSpawned Delegate);
private:
	TSharedPtr<FStreamableHandle> SpawnInstancedTile(AActor* InOwner, const class UInstancedTileDataAsset* TileAsset, FPrimaryAssetId TileId, FInstancedTileSpawned Delegate);
	FTransform GetTileSpawnTransform() const;

public:
	UPROPERTY(EditDefaultsOnly, Category = HatchlingCaves)
	FPrimaryAssetId CarnivoreHatchlingCave;

	UPROPERTY(EditDefaultsOnly, Category = HatchlingCaves)
	FPrimaryAssetId HerbivoreHatchlingCave;

	UPROPERTY(EditDefaultsOnly, Category = HatchlingCaves)
	FPrimaryAssetId CarnivoreAquaticHatchlingCave;

	UFUNCTION(BlueprintNativeEvent)
	FPrimaryAssetId GetHatchlingCaveForCharacter(AIBaseCharacter* IBaseCharacter);

	virtual void GenericPlayerInitialization(AController* C) override;

public:
	// Server Replay
	UFUNCTION(BlueprintCallable)
	void SetServerAutoRecord(bool bAutoRecord);

	virtual bool IsHandlingReplays() override;
private:
	// Since bHandleDedicatedServerReplays can change at runtime, we need to cache the config value at startup
	// Our override of IsHandlingReplays() will use this value
	bool bWasHandlingDedicatedServerReplays = false;
};
