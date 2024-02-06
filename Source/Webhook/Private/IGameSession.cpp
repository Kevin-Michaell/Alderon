// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "Online/IGameSession.h"
#include "Online/IPlayerState.h"
#include "GameMode/IGameMode.h"
#include "IGameEngine.h"
#include "Engine/NetConnection.h"
#include "Net/DataChannel.h"
#include "Kismet/GameplayStatics.h"
#include "Player/IPlayerController.h"
#include "GameMode/IGameMode.h"
#include "Online/IGameState.h"
#include "IGameplayStatics.h"
#include "Misc/Crc.h"
#include "AlderonCommon.h"
#include "OnlineSubsystemSessionSettings.h"
#include "OnlineSubsystemUtils.h"
#include "HAL/ConsoleManager.h"
#include "IGameInstance.h"
#include "AlderonRemoteConfig.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "IWorldSettings.h"
#include "AlderonSentry.h"
#include "AlderonContent.h"
#include "Quests/IQuestManager.h"
#include "Online/OnlineSessionNames.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Physics/PhysicsInterfaceScene.h"
#include "PBDRigidsSolver.h"

#if WITH_BATTLEYE_SERVER
	#include "IBattlEyeServer.h"
#endif

static TAutoConsoleVariable<int32> CVarOverrideGrowthEnabled(
    TEXT("pot.OverrideGrowthEnabled"),
    0,
    TEXT("0 uses config setting, 1 enables growth, 2 disables growth, other values use config setting\n"),
    ECVF_Cheat);

namespace
{
	const FString CustomMatchKeyword("Custom");
}

AIGameSession::AIGameSession(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PIDCount = 0;
	HatchlingCaveExitGrowth = 0.25f;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		OnCreateSessionCompleteDelegate = FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreateSessionComplete);
		OnDestroySessionCompleteDelegate = FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroySessionComplete);

		OnFindSessionsCompleteDelegate = FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFindSessionsComplete);
		OnJoinSessionCompleteDelegate = FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoinSessionComplete);

		OnStartSessionCompleteDelegate = FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnStartOnlineGameComplete);
	}
}

void AIGameSession::CheckForHotReload()
{
	FDateTime FailedAccess = FDateTime::MinValue();

	// bans.txt
	{
		FString FileName = TEXT("bans.txt");
		FString FileLocation = FPaths::Combine(FPaths::ProjectSavedDir(), FileName);
		if (FPaths::FileExists(FileLocation))
		{
			FDateTime LastEditTime = IFileManager::Get().GetTimeStamp(*FileLocation);
			if (LastEditTime != FailedAccess && LastEditTime > LastBansEditTime)
			{
				LoadBans();
				UE_LOG(TitansNetwork, Warning, TEXT("IGameSession::CheckForHotReload: Reloading %s due to modified time being newer. %s > %s"), *FileName, *LastEditTime.ToString(), *LastBansEditTime.ToString())
			} else {
				//UE_LOG(TitansNetwork, Log, TEXT("IGameSession::CheckForHotReload: Skipping Reload of %s LastEditTime: %s LastBansEditTime:  %s"), *FileName, *LastEditTime.ToString(), *LastBansEditTime.ToString())
			}
		}
	}

	// mutes.txt
	{
		FString FileName = TEXT("mutes.txt");
		FString FileLocation = FPaths::Combine(FPaths::ProjectSavedDir(), FileName);
		if (FPaths::FileExists(FileLocation))
		{
			FDateTime LastEditTime = IFileManager::Get().GetTimeStamp(*FileLocation);
			if (LastEditTime != FailedAccess && LastEditTime > LastMutesEditTime)
			{
				LoadMutes();
				UE_LOG(TitansNetwork, Warning, TEXT("IGameSession::CheckForHotReload: Reloading %s due to modified time being newer. %s > %s"), *FileName, *LastEditTime.ToString(), *LastMutesEditTime.ToString())
			} else {
				//UE_LOG(TitansNetwork, Log, TEXT("IGameSession::CheckForHotReload: Skipping Reload of %s LastEditTime: %s LastMutesEditTime:  %s"), *FileName, *LastEditTime.ToString(), *LastMutesEditTime.ToString())
			}
		}
	}

	// whitelist.txt
	{
		FString FileName = TEXT("whitelist.txt");
		FString FileLocation = FPaths::Combine(FPaths::ProjectSavedDir(), FileName);
		if (FPaths::FileExists(FileLocation))
		{
			FDateTime LastEditTime = IFileManager::Get().GetTimeStamp(*FileLocation);
			if (LastEditTime != FailedAccess && LastEditTime > LastWhitelistEditTime)
			{
				LoadWhitelist();
				UE_LOG(TitansNetwork, Warning, TEXT("IGameSession::CheckForHotReload: Reloading %s due to modified time being newer. %s > %s"), *FileName, *LastEditTime.ToString(), *LastWhitelistEditTime.ToString())
			} else {
				//UE_LOG(TitansNetwork, Log, TEXT("IGameSession::CheckForHotReload: Skipping Reload of %s LastEditTime: %s LastWhitelistEditTime:  %s"), *FileName, *LastEditTime.ToString(), *LastWhitelistEditTime.ToString())
			}
		}
	}
}

void AIGameSession::SetupHotReloadTimer()
{
	GetWorldTimerManager().SetTimer(TimerHandle_HotReload, this, &AIGameSession::CheckForHotReload, 60.0f, true);
}

void AIGameSession::ClearHotReloadTimer()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_HotReload);
}

bool FPlayerBan::IsValid() const
{
	return ((PlayerId.IsValid() || !IPAddress.IsEmpty()) && !IsExpired());
}

bool FPlayerBan::IsExpired() const
{
	if (BanExpiration > 0)
	{
		const FDateTime CurrentTime = FDateTime::UtcNow();
		const FDateTime BanExpiredAt = FDateTime::FromUnixTimestamp(BanExpiration);

		if (CurrentTime > BanExpiredAt)
		{
			return true;
		}
	}

	return false;
}

FString FPlayerBan::ToString() const
{
	FString Line = "";

	// AGID or IP
	if (PlayerId.IsValid())
	{
		Line += PlayerId.ToDisplayString();
	}
	else
	{
		Line += IPAddress;
	}

	// Ban Duration
	Line += TEXT(":");
	Line += FString::FromInt(BanExpiration);

	// Admin Reason
	if (!AdminReason.IsEmpty())
	{
		Line += TEXT(":");
		Line += AdminReason;
	}

	// User Reason
	if (!UserReason.IsEmpty())
	{
		Line += TEXT(":");
		Line += UserReason;
	}

	return Line;
};

void FPlayerBan::FromString(FString Line)
{
	TArray<FString> BanLineArray;
	BanLineArray.Reserve(4);

	Line.ParseIntoArray(BanLineArray, TEXT(":"), true);

	// AGID or IP (Required)
	if (BanLineArray.IsValidIndex(0))
	{
		FString BanTarget = BanLineArray[0];

		// AGID
		if (BanTarget.Contains(TEXT("-")))
		{
			PlayerId = FAlderonPlayerID(BanTarget);
		}
		// IP
		else if (BanTarget.Contains(TEXT("-")))
		{
			IPAddress = BanTarget;
		}
		// Invalid
		else
		{
			return;
		}
	} else {
		return;
	}

	// Unix Timestamp
	if (BanLineArray.IsValidIndex(1))
	{
		BanExpiration = FCString::Atoi64(*BanLineArray[1]);
		if (BanExpiration < 0)
		{
			BanExpiration = 0;
		}
	}
	else
	{
		BanExpiration = 0;
		return;
	}

	// Admin Reason
	if (BanLineArray.IsValidIndex(2))
	{
		AdminReason = BanLineArray[2];
	}
	else
	{
		return;
	}

	// User Reason
	if (BanLineArray.IsValidIndex(3))
	{
		UserReason = BanLineArray[3];
	}
	else
	{
		return;
	}
}

bool FPlayerMute::IsValid() const
{
	return (PlayerId.IsValid() && !IsExpired());
}

bool FPlayerMute::IsExpired() const
{
	if (BanExpiration > 0)
	{
		const FDateTime CurrentTime = FDateTime::UtcNow();
		const FDateTime BanExpiredAt = FDateTime::FromUnixTimestamp(BanExpiration);

		if (CurrentTime > BanExpiredAt)
		{
			return true;
		}
	}

	return false;
}

FString FPlayerMute::ToString() const
{
	FString Line = "";

	// AGID
	Line += PlayerId.ToDisplayString();

	// Ban Duration
	Line += TEXT(":");
	Line += FString::FromInt(BanExpiration);

	// Admin Reason
	if (!AdminReason.IsEmpty())
	{
		Line += TEXT(":");
		Line += AdminReason;
	}

	// User Reason
	if (!UserReason.IsEmpty())
	{
		Line += TEXT(":");
		Line += UserReason;
	}

	return Line;
};

void FPlayerMute::FromString(FString Line)
{
	TArray<FString> BanLineArray;
	BanLineArray.Reserve(4);

	Line.ParseIntoArray(BanLineArray, TEXT(":"), true);

	// AGID or IP (Required)
	if (BanLineArray.IsValidIndex(0))
	{
		FString BanTarget = BanLineArray[0];

		// AGID
		if (BanTarget.Contains(TEXT("-")))
		{
			PlayerId = FAlderonPlayerID(BanTarget);
		}
		// Invalid
		else
		{
			return;
		}
	} else {
		return;
	}

	// Unix Timestamp
	if (BanLineArray.IsValidIndex(1))
	{
		BanExpiration = FCString::Atoi64(*BanLineArray[1]);
		if (BanExpiration < 0)
		{
			BanExpiration = 0;
		}
	}
	else
	{
		BanExpiration = 0;
		return;
	}

	// Admin Reason
	if (BanLineArray.IsValidIndex(2))
	{
		AdminReason = BanLineArray[2];
	}
	else
	{
		return;
	}

	// User Reason
	if (BanLineArray.IsValidIndex(3))
	{
		UserReason = BanLineArray[3];
	}
	else
	{
		return;
	}
}

void AIGameSession::ConvertOldBans()
{
	if (BannedUsers.Num() > 0)
	{
		for (const FBanInfo& BanInfo : BannedUsers)
		{
			FPlayerBan Ban;
			Ban.PlayerId = BanInfo.UniqueID;
			Ban.BanExpiration = 0;
			Bans.Add(Ban);
		}

		SaveBans();

		BannedUsers.Empty();
		SaveConfig();
	}
}

void AIGameSession::LoadBans()
{
	FString BansLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("bans.txt"));
	if (!FPaths::FileExists(BansLocation))
	{
		if (IsRunningDedicatedServer())
		{
			UE_LOG(TitansNetwork, Error, TEXT("IGameSession::LoadBans: File doesn't exist: %s"), *BansLocation)
			SaveBans();
		}
		return;
	}

	TArray<FString> Lines;

	FFileHelper::LoadFileToStringArray(Lines, *BansLocation);
	LastBansEditTime = FDateTime::UtcNow();

	int LineNumber = 0;

	TArray<FString> BanLineArray;
	BanLineArray.Reserve(4);

	Bans.Empty(Lines.Num());

	for (const FString& Line : Lines)
	{
		LineNumber++;

		if (Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT(";")))
		{
			continue;
		}

		FPlayerBan Ban;
		Ban.FromString(Line);

		UE_LOG(TitansNetwork, Log, TEXT("IGameSession::LoadBans: Ban Id: %s IP: %s"), *Ban.PlayerId.ToDisplayString(), *Ban.IPAddress)
		Bans.Add(Ban);
	}

	Bans.Shrink();

	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState)
	{
		for (APlayerState* Player : GameState->PlayerArray)
		{
			AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player);
			check(IPlayerState);
			if (IPlayerState && IsPlayerBanned(IPlayerState->GetAlderonID()))
			{
				if (AIPlayerController* IPlayerController = IPlayerState->GetOwner<AIPlayerController>())
				{
					KickPlayer(IPlayerController, FText::FromString(GetBanInformation(IPlayerState->GetAlderonID()).UserReason));
				}
				break;
			}
		}
	}
	
}

void AIGameSession::SaveBans()
{
	FString BansLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("bans.txt"));

	TArray<FString> Lines;
	Lines.Add(TEXT("// Ban List for Path of Titans"));
	Lines.Add(TEXT("// Format Information"));
	Lines.Add(TEXT("// Alderon Id or IP Address (required)"));
	Lines.Add(TEXT("// UnixTimestamp - Ban expiration (eg epochconverter.com) - 0 = forever. If not specified, 0 is assumed"));
	Lines.Add(TEXT("// AdminReason - Reason the user was banned for admin purposes (Optional)"));
	Lines.Add(TEXT("// UserReason - Reason the user was banned (displayed to the user) (Optional)"));
	Lines.Add(TEXT("//(EXAMPLE) 525-053-709:0:Reason for admins here:Stay out of my server"));
	Lines.Add(TEXT("//(EXAMPLE) 127.0.0.1:0:KOSER:Killing people on sight"));

	Lines.Reserve(Bans.Num());

	for (const FPlayerBan& Ban : Bans)
	{
		FString Line = Ban.ToString();
		Lines.Add(Line);
	}

	FFileHelper::SaveStringArrayToFile(Lines, *BansLocation);
	LastBansEditTime = FDateTime::UtcNow();
}

// Append a ban without re-writing the entire file
void AIGameSession::AppendBan(FPlayerBan Ban)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Ban")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Add")))},
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(Ban.PlayerId.ToDisplayString()))},
			{ TEXT("IPAddress"), MakeShareable(new FJsonValueString(Ban.IPAddress)) },
			{ TEXT("Expiration"), MakeShareable(new FJsonValueNumber(Ban.BanExpiration)) },
			{ TEXT("AdminReason"), MakeShareable(new FJsonValueString(Ban.AdminReason)) },
			{ TEXT("UserReason"), MakeShareable(new FJsonValueString(Ban.UserReason)) }
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	Bans.Add(Ban);
	SaveBans();

	AGameStateBase* GameState = GetWorld()->GetGameState();
	check(GameState);
	if (!GameState)
	{
		return;
	}

	for (APlayerState* Player : GameState->PlayerArray)
	{
		AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player);
		check(IPlayerState);
		if (IPlayerState && IPlayerState->GetAlderonID() == Ban.PlayerId)
		{
			if (AIPlayerController* IPlayerController = IPlayerState->GetOwner<AIPlayerController>())
			{
				KickPlayer(IPlayerController, FText::FromString(Ban.UserReason));
			}
			break;
		}
	}
}

// Remove a ban and re-write the whole file
void AIGameSession::RemoveBan(FPlayerBan Ban)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Ban")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Remove")))},
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(Ban.PlayerId.ToDisplayString())) },
			{ TEXT("IPAddress"), MakeShareable(new FJsonValueString(Ban.IPAddress)) },
			{ TEXT("BanExpiration"), MakeShareable(new FJsonValueNumber(Ban.BanExpiration)) },
			{ TEXT("AdminReason"), MakeShareable(new FJsonValueString(Ban.AdminReason)) },
			{ TEXT("UserReason"), MakeShareable(new FJsonValueString(Ban.UserReason)) }
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	for (int i = Bans.Num(); i-- > 0;)
	{
		bool bRemove = false;
		const FPlayerBan& PlayerBan = Bans[i];

		if (!PlayerBan.IPAddress.IsEmpty() && !Ban.IPAddress.IsEmpty() && PlayerBan.IPAddress == Ban.IPAddress)
		{
			bRemove = true;
		}
		else if (PlayerBan.PlayerId == Ban.PlayerId)
		{
			bRemove = true;
		}

		if (bRemove)
		{
			Bans.RemoveAt(i);
			break;
		}
	}
	
	SaveBans();
}

void AIGameSession::LoadMutes()
{
	FString MutesLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("mutes.txt"));
	if (!FPaths::FileExists(MutesLocation))
	{
		if (IsRunningDedicatedServer())
		{
			UE_LOG(TitansNetwork, Error, TEXT("IGameSession::LoadMutes: File doesn't exist: %s"), *MutesLocation)
			SaveMutes();
		}
		return;
	}

	TArray<FString> Lines;

	FFileHelper::LoadFileToStringArray(Lines, *MutesLocation);
	LastMutesEditTime = FDateTime::UtcNow();

	int LineNumber = 0;

	TArray<FString> BanLineArray;
	BanLineArray.Reserve(4);

	ServerMutes.Empty(Lines.Num());

	for (const FString& Line : Lines)
	{
		LineNumber++;

		if (Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT(";")))
		{
			continue;
		}

		FPlayerMute Mute;
		Mute.FromString(Line);

		if (Mute.IsValid())
		{
			UE_LOG(TitansNetwork, Log, TEXT("IGameSession::LoadMutes: Mute Id: %s Line: %i"), *Mute.PlayerId.ToDisplayString(), LineNumber)
			ServerMutes.Add(Mute);
		}
		else
		{
			UE_LOG(TitansNetwork, Log, TEXT("IGameSession::LoadMutes: Skipping Mute on line %i due to it being invalid / expired."), LineNumber)
		}
	}

	ServerMutes.Shrink();

	AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState)
	{
		for (APlayerState* Player : GameState->PlayerArray)
		{
			AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player);
			check(IPlayerState);
			if (IPlayerState)
			{
				IPlayerState->SetIsServerMuted(IsPlayerServerMuted(IPlayerState->GetAlderonID()));
			}
		}
	}
}

void AIGameSession::SaveMutes()
{
	FString MutesLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("mutes.txt"));

	TArray<FString> Lines;
	Lines.Add(TEXT("// Mute List for Path of Titans"));
	Lines.Add(TEXT("// Format Information"));
	Lines.Add(TEXT("// Alderon Id (required)"));
	Lines.Add(TEXT("// UnixTimestamp - Mute expiration (eg epochconverter.com) - 0 = forever. If not specified, 0 is assumed"));
	Lines.Add(TEXT("// AdminReason - Reason the user was muted for admin purposes (Optional)"));
	Lines.Add(TEXT("// UserReason - Reason the user was muted (displayed to the user) (Optional)"));
	Lines.Add(TEXT("//(EXAMPLE) 525-053-709:0:Reason for admins here:Stay out of my server"));

	Lines.Reserve(ServerMutes.Num());

	for (const FPlayerMute& Mute : ServerMutes)
	{
		FString Line = Mute.ToString();
		Lines.Add(Line);
	}

	FFileHelper::SaveStringArrayToFile(Lines, *MutesLocation);
	LastMutesEditTime = FDateTime::UtcNow();
}

// Append a ban without re-writing the entire file
void AIGameSession::AppendMute(FPlayerMute Mute)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Mute")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Add"))) },
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(Mute.PlayerId.ToDisplayString())) },
			{ TEXT("Expiration"), MakeShareable(new FJsonValueNumber(Mute.BanExpiration)) },
			{ TEXT("AdminReason"), MakeShareable(new FJsonValueString(Mute.AdminReason)) },
			{ TEXT("UserReason"), MakeShareable(new FJsonValueString(Mute.UserReason)) }
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	ServerMutes.Add(Mute);
	SaveMutes();

	AGameStateBase* GameState = GetWorld()->GetGameState();
	check(GameState);
	if (!GameState)
	{
		return;
	}

	for (APlayerState* Player : GameState->PlayerArray)
	{
		AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player);
		check(IPlayerState);
		if (IPlayerState && IPlayerState->GetAlderonID() == Mute.PlayerId)
		{
			IPlayerState->SetIsServerMuted(true);
			IPlayerState->SetMuteExpirationUnix(Mute.BanExpiration);
			break;
		}
	}
}

// Remove a ban and re-write the whole file
bool AIGameSession::RemoveMute(FPlayerMute Mute)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Mute")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Remove")))},
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(Mute.PlayerId.ToDisplayString())) },
			{ TEXT("Expiration"), MakeShareable(new FJsonValueNumber(Mute.BanExpiration)) },
			{ TEXT("AdminReason"), MakeShareable(new FJsonValueString(Mute.AdminReason)) },
			{ TEXT("UserReason"), MakeShareable(new FJsonValueString(Mute.UserReason)) }
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	bool bRemove = false;

	for (int i = ServerMutes.Num(); i-- > 0;)
	{
		const FPlayerMute& PlayerMute = ServerMutes[i];

		if (PlayerMute.PlayerId == Mute.PlayerId)
		{
			bRemove = true;
		}

		if (bRemove)
		{
			ServerMutes.RemoveAt(i);
			break;
		}
	}

	SaveMutes();

	AGameStateBase* GameState = GetWorld()->GetGameState();
	check(GameState);
	if (!GameState)
	{
		return bRemove;
	}

	for (APlayerState* Player : GameState->PlayerArray)
	{
		AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player);
		check(IPlayerState);
		if (IPlayerState && IPlayerState->GetAlderonID() == Mute.PlayerId)
		{
			IPlayerState->SetIsServerMuted(false);
			IPlayerState->SetMuteExpirationUnix(0);
			break;
		}
	}

	return bRemove;
}

void AIGameSession::LoadWhitelist()
{
	FString WhitelistLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("whitelist.txt"));
	if (!FPaths::FileExists(WhitelistLocation))
	{
		if (IsRunningDedicatedServer())
		{
			UE_LOG(TitansNetwork, Warning, TEXT("IGameSession::LoadWhitelist: File doesn't exist: %s"), *WhitelistLocation)
			SaveWhitelist();
		}
		return;
	}

	TArray<FString> Lines;

	FFileHelper::LoadFileToStringArray(Lines, *WhitelistLocation);
	LastWhitelistEditTime = FDateTime::UtcNow();

	ServerWhitelist.Empty(Lines.Num());

	int LineNumber = 0;

	for (const FString& Line : Lines)
	{
		LineNumber++;

		if (Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT(";")))
		{
			continue;
		}

		FAlderonPlayerID WhitelistPlayerId = FAlderonPlayerID(Line);
		if (WhitelistPlayerId.IsValid())
		{
			ServerWhitelist.Add(WhitelistPlayerId);
		}
		else
		{
			UE_LOG(TitansNetwork, Error, TEXT("IGameSession::LoadWhitelist: Skipping Line %i due to invalid Alderon Id."), LineNumber)
		}
	}

	ServerWhitelist.Shrink();
}

bool AIGameSession::IsWhitelistActive()
{
	return (ServerWhitelist.Num() > 0 && bEnforceWhitelist);
}

void AIGameSession::SaveWhitelist()
{
	FString WhitelistLocation = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("whitelist.txt"));

	TArray<FString> Lines;
	Lines.Add(TEXT("// Alderon Id Whitelist for Path of Titans"));
	Lines.Add(TEXT("// Format Information"));
	Lines.Add(TEXT("// Alderon Id (required)"));
	Lines.Add(TEXT("//(EXAMPLE) 525-053-709"));

	Lines.Reserve(ServerWhitelist.Num());

	for (const FAlderonPlayerID& AlderonId : ServerWhitelist)
	{
		if (AlderonId.IsValid())
		{
			Lines.Add(AlderonId.ToDisplayString());
		}
	}

	FFileHelper::SaveStringArrayToFile(Lines, *WhitelistLocation);
	LastWhitelistEditTime = FDateTime::UtcNow();
}

void AIGameSession::AppendWhitelist(const FAlderonPlayerID& AlderonId)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Whitelist")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Add"))) },
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(AlderonId.ToDisplayString())) },
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	ServerWhitelist.AddUnique(AlderonId);
	SaveWhitelist();
}

bool AIGameSession::RemoveWhitelist(const FAlderonPlayerID& AlderonId)
{
	if (AIGameSession::UseWebHooks(WEBHOOK_ServerModerate))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Type"), MakeShareable(new FJsonValueString(TEXT("Whitelist")))},
			{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Remove"))) },
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(AlderonId.ToDisplayString())) },
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerModerate, WebHookProperties);
	}

	int Count = ServerWhitelist.Remove(AlderonId);
	SaveWhitelist();
	return Count != 0;
}

bool AIGameSession::UseWebHooks(const FString& WebhookKey)
{
	bool bEnabled = false;
	GConfig->GetBool(TEXT("ServerWebhooks"), TEXT("bEnabled"), bEnabled, GGameIni);
	if (!bEnabled) return false;

	// If specified check a specific webhook key to see if its enabled to avoid code being run unnecessarily
	if (!WebhookKey.IsEmpty())
	{
		FString WebHookUrl = "";
		GConfig->GetString(TEXT("ServerWebhooks"), *WebhookKey, WebHookUrl, GGameIni);
		const bool bHasValidWebhookKey = !WebhookKey.IsEmpty();
		return bHasValidWebhookKey;
	}

	return true;
}

void AIGameSession::TriggerWebHookFromContext(UObject* ConextObject, const FString& WebhookKey, TMap<FString, TSharedPtr<FJsonValue>> Properties)
{
	check(ConextObject);
	AIGameMode* IGameMode = UIGameplayStatics::GetIGameMode(ConextObject);
	if (IGameMode)
	{
		AIGameSession* IGameSession = Cast<AIGameSession>(IGameMode->GameSession);
		if (IGameSession)
		{
			IGameSession->TriggerWebHook(WebhookKey, Properties);
		}
	}
}

void AIGameSession::TriggerWebHook(const UWebhook* Webhook)
{
	TriggerWebHook(Webhook->WebhookKey, Webhook->Properties);
}

void AIGameSession::TriggerWebHook(const FString& WebhookKey, const TMap<FString, TSharedPtr<FJsonValue>>& Properties)
{
	FHttpRequestPtr HttpRequest = IAlderonCommon::CreateRequest(EAlderonWebRequestVerb::POST);

	HttpRequest->OnProcessRequestComplete().BindUObject(this, &AIGameSession::TriggerWebHook_Callback, WebhookKey);

	FString WebHookUrl, WebhookFormat;
	GConfig->GetString(TEXT("ServerWebhooks"), *WebhookKey, WebHookUrl, GGameIni);
	GConfig->GetString(TEXT("ServerWebhooks"),TEXT("Format"), WebhookFormat, GGameIni);

	UE_LOG(TitansNetwork, Log, TEXT("TriggerWebhook: Key: %s Url %s"), *WebhookKey, *WebHookUrl)

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	if (!WebHookUrl.IsEmpty())
	{
		if (WebhookFormat.ToLower() == TEXT("discord"))
		{
			FString DiscordMessage = "";

			for (auto& Elem : Properties)
			{
				// **PropertyName:** 
				DiscordMessage += TEXT("**") + Elem.Key + TEXT(":** ");
				
				// String
				FJsonValue* ValuePtr = Elem.Value.Get();
				if (FJsonValueString* StringValue = (FJsonValueString*)(ValuePtr))
				{
					FString String = StringValue->AsString();
					// add backslash discord format characters to prevent unintentional formatting
					String = String.Replace(TEXT("*"), TEXT("\\*"));
					String = String.Replace(TEXT("~"), TEXT("\\~"));
					String = String.Replace(TEXT("_"), TEXT("\\_"));
					String = String.Replace(TEXT("`"), TEXT("\\`"));
					String = String.Replace(TEXT(">"), TEXT("\\>"));
					String = String.Replace(TEXT("|"), TEXT("\\|"));
					DiscordMessage += String;
				}
				// Number
				else if (FJsonValueNumber* NumValue = (FJsonValueNumber*)(ValuePtr))
				{
					DiscordMessage += FString::FromInt(StringValue->AsNumber());
				}
				// Boolean
				else if (FJsonValueBoolean* BoolValue = (FJsonValueBoolean*)(ValuePtr))
				{
					DiscordMessage += (StringValue->AsBool()) ? TEXT("true") : TEXT("false");
				}
				else
				{
					DiscordMessage += TEXT("InvalidType");
				}

				// Line Terminator
				DiscordMessage += LINE_TERMINATOR;
			}

			RootObject->SetStringField(TEXT("content"), "");
			RootObject->SetStringField(TEXT("username"), ServerName);
			//RootObject->SetStringField(TEXT("username"), DisplayName + TEXT(" (") + AlderonId + TEXT(")"));
			//RootObject->SetStringField(TEXT("avatar_url"), AvatarUrl);
			RootObject->SetBoolField(TEXT("tts"), false);

			TSharedPtr<FJsonObject> Embed = MakeShareable(new FJsonObject);
			Embed->SetStringField(TEXT("title"), WebhookKey);
			Embed->SetStringField(TEXT("type"), TEXT("rich"));
			Embed->SetStringField(TEXT("description"), DiscordMessage);
			// date("c", strtotime("now"));
			//Embed->SetStringField(TEXT("timestamp"), "");

			TArray<TSharedPtr<FJsonValue>> Embeds;
			Embeds.Add(MakeShared<FJsonValueObject>(Embed));
			RootObject->SetArrayField(TEXT("embeds"), Embeds);
		}
		else
		{
			for (auto& Elem : Properties)
			{
				RootObject->SetField(Elem.Key, Elem.Value);
			}
		}

		FString JsonResult;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonResult);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

		UE_LOG(TitansLogParse, Verbose, TEXT("Webhook: Url: %s Json: %s"), *WebHookUrl, *JsonResult)

		HttpRequest->SetContentAsString(JsonResult);
		HttpRequest->SetURL(WebHookUrl);
		HttpRequest->ProcessRequest();
	}

	// Log Information
	{
		FString LogLine = "";

		for (auto& Elem : Properties)
		{
			// **PropertyName:** 
			LogLine += Elem.Key + TEXT(": ");

			// String
			FJsonValue* ValuePtr = Elem.Value.Get();
			if (FJsonValueString* StringValue = (FJsonValueString*)(ValuePtr))
			{
				LogLine += StringValue->AsString();
			}
			// Number
			else if (FJsonValueNumber* NumValue = (FJsonValueNumber*)(ValuePtr))
			{
				LogLine += FString::FromInt(StringValue->AsNumber());
			}
			// Boolean
			else if (FJsonValueBoolean* BoolValue = (FJsonValueBoolean*)(ValuePtr))
			{
				LogLine += (StringValue->AsBool()) ? TEXT("1") : TEXT("0");
			}
			else
			{
				LogLine += TEXT("InvalidType");
			}

			// Line Terminator
			LogLine += TEXT(" ");
		}

		UE_LOG(TitansLogParse, Log, TEXT("%s"), *LogLine)
	}
}

void AIGameSession::TriggerWebHook_Callback(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful, FString WebHookKey)
{
	FString ResponseJson = TEXT("");
	int32 ResponseCode = 0;

	if (Response.IsValid())
	{
		ResponseCode = Response->GetResponseCode();
		ResponseJson = Response->GetContentAsString();
		UE_LOG(TitansNetwork, Log, TEXT("AIGameSession:TriggerWebHook_Callback: (%i) Key: %s Json: %s"), ResponseCode, *WebHookKey, *ResponseJson);
	}

	if (!bWasSuccessful || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession:TriggerWebHook_Callback() Failed"));
		return;
	}

	/*
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);
	if (FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		if (JsonObject->HasField("data"))
		{
			TSharedPtr<FJsonObject> DataObject = JsonObject->GetObjectField("data");

			FString ResultOtp = DataObject->GetStringField("otp");
			FString ResultToken = DataObject->GetStringField("token");

#if !UE_BUILD_SHIPPING
			UE_LOG(AlderonLog, Log, TEXT("IAlderonAuth:ClientOTPCallback: Otp: %s Token: %s"), *ResultOtp, *ResultToken);
#endif
			OnCompleted.ExecuteIfBound(ResultOtp, ResultToken);
			return;
		}
	}
	*/
}

bool AIGameSession::AtCapacity(bool bSpectator)
{
	if (GetNetMode() == NM_Standalone)
	{
		return false;
	}

	AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
	check(GameMode);

	if (bSpectator)
	{
		return ((GameMode->GetNumSpectators() >= MaxSpectators) && ((GetNetMode() != NM_ListenServer) || (GameMode->GetNumPlayers() > 0)));
	} else {
		IConsoleVariable* CVarMaxPlayersOverride = FConsoleManager::Get().FindConsoleVariable(TEXT("net.MaxPlayersOverride"), false);
		int32 MaxPlayersToUse = (CVarMaxPlayersOverride != nullptr && CVarMaxPlayersOverride->GetInt() > 0) ? CVarMaxPlayersOverride->GetInt() : MaxPlayers;
		MaxPlayersToUse += ReservedSlots;
		return ((MaxPlayersToUse > 0) && (GameMode->GetNumPlayers() >= MaxPlayersToUse));
	}
}

void AIGameSession::InitOptions(const FString& Options)
{
	// Bans
	LoadBans();
	ConvertOldBans();

	// Mutes
	LoadMutes();

	// Whitelisting
	LoadWhitelist();

	// Hot Reload on file change
	if (IsRunningDedicatedServer())
	{
		SetupHotReloadTimer();
	}

#if UE_BUILD_DEVELOPMENT
	// Test 1
	InstanceGameDevs.Add("048-236-424");
	// Test 2
	InstanceGameDevs.Add("748-333-694");
#endif

	if (CVarOverrideGrowthEnabled->GetInt() == 1)
	{
		UE_LOG(TitansLog, Log, TEXT("Growth forced enabled via pot.OverrideGrowthEnabled"))
		bServerGrowth = true;
	}
	if (CVarOverrideGrowthEnabled->GetInt() == 2)
	{
		UE_LOG(TitansLog, Log, TEXT("Growth forced disabled via pot.OverrideGrowthEnabled"))
		bServerGrowth = false;
	}

	//DeathMarksPenaltyPercent = UGameplayStatics::GetIntOption(Options, TEXT("DeathMarksPenaltyPercent"), DeathMarksPenaltyPercent);
	//DeathGrowthPenaltyPercent = UGameplayStatics::GetIntOption(Options, TEXT("DeathGrowthPenaltyPercent"), DeathGrowthPenaltyPercent);

	// Hotfix: Load Max Players Directly from Config
	int32 MaxPlayersIni;
	bool bFound = GConfig->GetInt(TEXT("/Script/PathOfTitans.IGameSession"), TEXT("MaxPlayers"), MaxPlayersIni, GGameIni);
	if (bFound)
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession: Using Detected Max Players: %i from IGameSession Configuration"), MaxPlayersIni);
		MaxPlayers = MaxPlayersIni;
	}

	// Unreal Default Functionality replaced from parent class
	UWorld* World = GetWorld();

	AGameMode* const GameMode = Cast<AGameMode>(World->GetAuthGameMode());
	MaxPlayers = UGameplayStatics::GetIntOption(Options, TEXT("MaxPlayers"), MaxPlayers);
	MaxSpectators = UGameplayStatics::GetIntOption(Options, TEXT("MaxSpectators"), MaxSpectators);
	SessionName = GetDefault<APlayerState>(GameMode->PlayerStateClass)->SessionName;

	// Get Optional Server Name
	if (UGameplayStatics::HasOption(Options, TEXT("ServerName")))
	{
		ServerName = UGameplayStatics::ParseOption(Options, TEXT("ServerName"));
	}
	
	// Server Name Character Limit
	ServerName = ServerName.Left(SERVER_NAME_CHAR_LIMIT);

	// Replace Underscores with spaces
	ServerName = ServerName.Replace(TEXT("_"), TEXT(" "), ESearchCase::IgnoreCase);

	// Auto set a server name
	if (ServerName.IsEmpty())
	{
		ServerName = TEXT("Server");
	}

	// Command Line Args to Override Server Password
	FString OverrideServerName;
	FParse::Value(FCommandLine::Get(), TEXT("ServerName="), OverrideServerName);
	if (!OverrideServerName.IsEmpty())
	{
		ServerName = OverrideServerName;
	}

	// Get Optional Server Password
	if (UGameplayStatics::HasOption(Options, TEXT("ServerPassword")))
	{
		ServerPassword = UGameplayStatics::ParseOption(Options, TEXT("ServerPassword"));
	}

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	FString Branch = AlderonCommon.GetAuthInterface().GetBranchKey();

#if !WITH_EDITOR
	bool bDisableUnreleasedFeatures = true;

	// Allow Demo Unstable
	if (Branch == TEXT("demo-unstable"))
	{
		bDisableUnreleasedFeatures = false;
	}
	// Allow Demo Public Test but on dedicated servers only!
	// this prevents someone modifying a single player config file and getting growth etc
	else if (Branch == TEXT("demo-public-test"))
	{
		if (IsRunningDedicatedServer())
		{
			bDisableUnreleasedFeatures = false;
		}
	}

	if (bDisableUnreleasedFeatures)
	{	
		bServerAI = false;
	}
#endif

	// Command Line Arg to force enable growth
	if (FParse::Param(FCommandLine::Get(), TEXT("growth")))
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession: Detected -growth command line argument. Forcing Growth booleans enabled."));
		bServerGrowth = true;
		QuestGrowthMultiplier = 1.f;
		QuestMarksMultiplier = 1.f;
		bServerHatchlingCaves = true;
	}

	// Command Line Arg to force disable growth
	if (FParse::Param(FCommandLine::Get(), TEXT("nogrowth")))
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession: Detected -nogrowth command line argument. Forcing Growth booleans disabled."));
		bServerGrowth = false;
		QuestGrowthMultiplier = 0.0f;
		bServerHatchlingCaves = false;
	}

    // Command Line Args to Override Server Password
    FString OverrideServerPassword;
    FParse::Value(FCommandLine::Get(), TEXT("ServerPassword="), OverrideServerPassword);
    if (!OverrideServerPassword.IsEmpty())
    {
        ServerPassword = OverrideServerPassword;
    }

	if (ServerPassword.IsEmpty() || ServerPassword == TEXT(" ") || ServerPassword.Len() <= 2)
	{
		ServerPassword = FString();
	}

	// Ensure MaxPlayers is not above the hard coded limit.
	if (MaxPlayers > MAX_PLAYER_LIMIT) MaxPlayers = MAX_PLAYER_LIMIT;

	// Get Server MOTD
	LoadServerMOTD();

	// Get Server Rules
	LoadServerRules();

	FString AutoRestartStringValue;
	FParse::Value(FCommandLine::Get(), TEXT("AutoRestart="), AutoRestartStringValue);

	if (AutoRestartStringValue.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession: -AutoRestart=false. Forcing AutoRestart timer to stop."));

		if (AIGameMode* const IGameMode = Cast<AIGameMode>(GameMode))
		{
			IGameMode->StopAutoRestart();
		}
	}
	else
	{
		if (IsAutoRestartEnabled() || AutoRestartStringValue.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			if (AIGameMode* const IGameMode = Cast<AIGameMode>(GameMode))
			{
				const int32 RestartTimeSeconds = IsUsingScheduledRestartTimes() ? GetNextScheduledRestartTime() : GetAutoRestartLength();
				
				const FTimespan RestartTimeSpan = FTimespan::FromSeconds(RestartTimeSeconds);
				const FDateTime RestartLocalTime = FDateTime::Now() + RestartTimeSpan;

				const FString RestartTimeSpanString = FString::Printf(TEXT("%d Days, %02d Hours and %02d Minutes"), RestartTimeSpan.GetDays(), RestartTimeSpan.GetHours() % 24, RestartTimeSpan.GetMinutes() % 60);
				const FString RestartLocalTimeString = FString::Printf(TEXT("%02d:%02d %s"), RestartLocalTime.GetHour12(), RestartLocalTime.GetMinute(), RestartLocalTime.GetHour() >= 12 ? TEXT("PM") : TEXT("AM"));
				
				UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::InitOptions() - Called StartRestartTimer. Server will restart in %s at %s"), *RestartTimeSpanString, *RestartLocalTimeString);
				IGameMode->StartRestartTimer(RestartTimeSeconds);
			}
		}
		else
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::InitOptions() - Auto Restart isn't enabled in config!"));
		}
	}
}

void AIGameSession::LoadServerMOTD()
{
	FString PathMOTD = FPaths::ProjectSavedDir() + TEXT("/MOTD.txt");
	if (FPaths::FileExists(PathMOTD))
	{
		TWeakObjectPtr<AIGameSession> ThisPtr = this;
		
		FString TempMOTD;
		FFileHelper::LoadFileToString(TempMOTD, *PathMOTD);

		IAlderonCommon::Get().AsyncStringFilter(TempMOTD, FAlderonStringFilterResult::CreateLambda([ThisPtr](const FString& Result)
		{
			if (!ThisPtr.Get()) return;
			ThisPtr->SetServerMotd(Result);
		}), false, true);
	}
}

void AIGameSession::LoadServerRules()
{
	FString PathRules = FPaths::ProjectSavedDir() + TEXT("/Rules.txt");
	if (FPaths::FileExists(PathRules))
	{
		TWeakObjectPtr<AIGameSession> ThisPtr = this;

		FString TempRules;
		FFileHelper::LoadFileToString(TempRules, *PathRules);

		IAlderonCommon::Get().AsyncStringFilter(TempRules, FAlderonStringFilterResult::CreateLambda([ThisPtr](const FString& Result)
		{
			if (!ThisPtr.Get()) return;
			ThisPtr->SetServerRules(Result);
		}), false, true);
	}
}

void AIGameSession::SaveString(const FString& Filename, const FString& String)
{
	FString Path = FPaths::ProjectSavedDir() + Filename;
	FFileHelper::SaveStringToFile(String, *Path);
}

FString AIGameSession::GetServerName()
{
	return ServerName;
}

FString AIGameSession::GetServerMOTD()
{
	return ServerMOTD;
}

FString AIGameSession::GetServerRules()
{
	return ServerRules;
}

uint32 AIGameSession::GetServerRulesCrcHash()
{
	return ServerRulesCrcHash;
}

bool AIGameSession::DatabaseEnabled()
{
	return bServerDatabase;
}

bool AIGameSession::GlobalChatEnabled()
{
	return bServerGlobalChat;
}

bool AIGameSession::NameTagsEnabled()
{
	return bServerNameTags;
}

bool AIGameSession::ExperimentalEnabled()
{
	return bServerExperimental;
}

bool AIGameSession::AllowChat()
{
	return bServerAllowChat;
}

bool AIGameSession::AllowProximityVoiceChat() {
	return bServerAllowProximityVoiceChat;
}

bool AIGameSession::AllowPartyVoiceChat() {
	return bServerAllowPartyVoiceChat;
}

bool AIGameSession::Allow3DMapMarkers()
{
	return bServerAllow3DMapMarkers;
}

bool AIGameSession::AIEnabled()
{
	return bServerAI;
}

bool AIGameSession::FishEnabled()
{
	return bServerFish;
}

int AIGameSession::AIMax()
{
	return ServerAIMax;
}

float AIGameSession::AIRate()
{
	return ServerAIRate;
}

bool AIGameSession::AIPlayerSpawns()
{
	return bServerAIPlayerSpawns;
}

bool AIGameSession::GrowthEnabled()
{
	return bServerGrowth;
}

int AIGameSession::GetCombatDeathMarksPenaltyPercent() const
{
	return CombatDeathMarksPenaltyPercent;
}

int AIGameSession::GetCombatDeathGrowthPenaltyPercent() const
{
	return CombatDeathGrowthPenaltyPercent;
}

int AIGameSession::GetFallDeathMarksPenaltyPercent() const
{
	return FallDeathMarksPenaltyPercent;
}

int AIGameSession::GetFallDeathGrowthPenaltyPercent() const
{
	return FallDeathGrowthPenaltyPercent;
}

int AIGameSession::GetSurvivalDeathMarksPenaltyPercent() const
{
	return SurvivalDeathMarksPenaltyPercent;
}

int AIGameSession::GetSurvivalDeathGrowthPenaltyPercent() const
{
	return SurvivalDeathGrowthPenaltyPercent;
}

int AIGameSession::GetChangeSubspeciesGrowthPenaltyPercent() const
{
	return ChangeSubspeciesGrowthPenaltyPercent;
}

FString AIGameSession::GetServerDiscord()
{
    return ServerDiscord;
}

const bool AIGameSession::IsAutoRestartEnabled()
{
	return bServerAutoRestart;
}

const int32 AIGameSession::GetAutoRestartLength()
{
	return RestartLengthInSeconds;
}

const bool AIGameSession::IsUsingScheduledRestartTimes()
{
	return bUseScheduledRestartTimes;
}

const TArray<int32>& AIGameSession::GetScheduledRestartTimes()
{
	// Sort the array to prevent user error
	ScheduledRestartTimes.Sort();
	return ScheduledRestartTimes;
}

const int32 AIGameSession::GetNextScheduledRestartTime()
{
	const TArray<int32>& RestartTimes = GetScheduledRestartTimes();
	
	if (RestartTimes.IsEmpty())
	{
		return GetAutoRestartLength();
	}
	
	const FDateTime ServerTime = FDateTime::Now();
	int32 FoundRestartTime = -1;

	for (int32 RestartTime : RestartTimes)
	{
		if (RestartTime > ServerTime.GetHour() * 100 + ServerTime.GetMinute())
		{
			FoundRestartTime = RestartTime;
			break;
		}
	}

	// If no restart time is found for today, set the next restart time to the first time in the array for tomorrow
	if (FoundRestartTime == -1 && RestartTimes.Num() > 0)
	{
		FoundRestartTime = RestartTimes[0];
	}

	// Calculate the number of seconds until the next restart time
	const int32 NextRestartHour = FoundRestartTime / 100;
	const int32 NextRestartMinute = FoundRestartTime % 100;
	FDateTime NextRestartDateTime = FDateTime(ServerTime.GetYear(), ServerTime.GetMonth(), ServerTime.GetDay(), NextRestartHour, NextRestartMinute, 0);

	// If the next restart time is earlier than the current time, add one day to represent tomorrow
	if (NextRestartDateTime < ServerTime)
	{
		NextRestartDateTime += FTimespan::FromDays(1);
	}

	// Get the number of seconds between the current time and the next restart time
	return (NextRestartDateTime - ServerTime).GetTotalSeconds();
}

const TArray<int32>& AIGameSession::GetRestartNotificationTimestamps()
{
	return RestartNotificationTimestamps;
}

void AIGameSession::SetServerRules(const FString& Rules)
{
	ServerRules = Rules;
	ServerRules = ServerRules.Left(RULES_CHAR_LIMIT);
	ServerRulesCrcHash = FCrc::MemCrc32(*ServerRules, ServerRules.Len());
}

void AIGameSession::SetServerMotd(const FString& Motd)
{
	ServerMOTD = Motd;
	ServerMOTD = ServerMOTD.Left(MOTD_CHAR_LIMIT);
}

float AIGameSession::GetMinGrowthAfterDeath() const
{
	// This has to be clamped due to various checks we have where if growth == 0 we make various assumptions (such as death, initialization, growth penalty etc)
	// Made private to force use of getter as we need to clamp this and we can't do that in the CDO constructor because the config system will write over it when
	// initializing our instance of IGameSession,
	// and BeginPlay is too late because IGameState reads from our instance before BeginPlay is called.

	return FMath::Max(MinGrowthAfterDeath, 0.01f);
}

void AIGameSession::SetServerOptions(FServerOptions Options)
{
	ServerName = Options.ServerName;
	MaxPlayers = Options.MaxPlayers;
	ServerPassword = Options.ServerPassword;
	bServerDatabase = Options.bServerDatabase;
	bServerGlobalChat = Options.bServerGlobalChat;
	bServerNameTags = Options.bServerNameTags;
	bServerExperimental = Options.bServerExperimental;
	bServerAI = Options.bServerAI;
	bServerCritters = Options.bServerCritters;
	bServerFish = Options.bServerFish;
	bServerWaterQualitySystem = Options.bServerWaterQualitySystem;
	bServerLocalWorldQuests = Options.bServerLocalWorldQuests;
	bServerWaystoneCooldownRemoval = Options.bServerWaystoneCooldownRemoval;
	bServerWaystones = Options.bServerWaystones;
	bServerAllowInGameWaystone = Options.bServerAllowInGameWaystone;
	bServerGrowth = Options.bServerGrowth;
	bPermaDeath = Options.bPermaDeath;
	bDeathInfo = Options.bDeathInfo;
	QuestGrowthMultiplier = Options.QuestGrowthMultiplier;
	QuestMarksMultiplier = Options.QuestMarksMultiplier;
	GlobalPassiveGrowthPerMinute = Options.GlobalPassiveGrowthPerMinute;
	bLoseGrowthPastGrowthStages = Options.bLoseGrowthPastGrowthStages;
	MinGrowthAfterDeath = Options.MinGrowthAfterDeath;
	bServerNesting = Options.bServerNesting;
	bServerHatchlingCaves = Options.bServerHatchlingCaves;
	bServerHomeCaves = Options.bServerHomeCaves;
	bServerEditAbilitiesInHomeCaves = Options.bServerEditAbilitiesInHomeCaves;
	bServerHungerThirstInCaves = Options.bServerHungerThirstInCaves;
	bServerAllowMap = Options.bServerAllowMap;
	bServerAllowMinimap = Options.bServerAllowMinimap;
	bServerPaidUsersOnly = Options.bServerPaidUsersOnly;
	bServerFallDamage = Options.bServerFallDamage;
	bServerAllowChat = Options.bServerAllowChat;
	bServerAllowProximityVoiceChat = Options.bServerAllowProximityVoiceChat;
	bServerAllowPartyVoiceChat = Options.bServerAllowPartyVoiceChat;
	bServerPrivate = Options.bServerPrivate;
	AFKDisconnectTime = Options.AFKDisconnectTime;
	bOverrideQuestContributionCleanup = Options.bOverrideQuestContributionCleanup;
	bServerHealingInHomeCave = Options.bServerHealingInHomeCave;
	QuestContributionCleanup = Options.QuestContributionCleanup;
	bOverrideLocalQuestCooldown = Options.bOverrideLocalQuestCooldown;
	LocalQuestCooldown = Options.LocalQuestCooldown;
	bOverrideLocationQuestCooldown = Options.bOverrideLocationQuestCooldown;
	LocationQuestCooldown = Options.LocationQuestCooldown;
	bOverrideMaxCompleteQuestsInLocation = Options.bOverrideMaxCompleteQuestsInLocation;
	MaxCompleteQuestsInLocation = Options.MaxCompleteQuestsInLocation;
	bLoseUnclaimedQuestsOnDeath = Options.bLoseUnclaimedQuestsOnDeath;
	bTrophyQuests = Options.bTrophyQuests;
	WaterRegenerationValue = Options.WaterRegenerationValue;
	WaterRainRegenerationIncrement = Options.WaterRainRegenerationIncrement;
	bOverrideWaterRegeneration = Options.bOverrideWaterRegeneration;
	WaterRegenerationRate = Options.WaterRegenerationRate;
	bEnableWaterRegeneration = Options.bEnableWaterRegeneration;
	WaterRegenerationRateMultiplierUpdate = Options.WaterRegenerationRateMultiplierUpdate;
	HatchlingCaveExitGrowth = Options.HatchlingCaveExitGrowth;
	bServerWellRestedBuff = Options.bServerWellRestedBuff;
	bServerAllowChangeSubspecies = Options.bServerAllowChangeSubspecies;

	MaxCharactersPerPlayer = Options.MaxCharactersPerPlayer;
	MaxCharactersPerSpecies = Options.MaxCharactersPerSpecies;

	CombatDeathMarksPenaltyPercent = Options.CombatDeathMarksPenaltyPercent;
	CombatDeathGrowthPenaltyPercent = Options.CombatDeathGrowthPenaltyPercent;
	FallDeathMarksPenaltyPercent = Options.FallDeathMarksPenaltyPercent;
	FallDeathGrowthPenaltyPercent = Options.FallDeathGrowthPenaltyPercent;
	SurvivalDeathMarksPenaltyPercent = Options.SurvivalDeathMarksPenaltyPercent;
	SurvivalDeathGrowthPenaltyPercent = Options.SurvivalDeathGrowthPenaltyPercent;
	ChangeSubspeciesGrowthPenaltyPercent = Options.ChangeSubspeciesGrowthPenaltyPercent;
	bServerAllowAnselMultiplayerPausing = Options.bServerAllowAnselMultiplayerPausing;
	ServerAnselCameraConstraintDistance = Options.ServerAnselCameraConstraintDistance;
	//CurveOverrides = Options.CurveOverrides;

	MaxUnclaimedRewards = Options.MaxUnclaimedRewards;
	bEnableMaxUnclaimedRewards = Options.bEnableMaxUnclaimedRewards;

	bMustCollectItemsWithinPOI = Options.bMustCollectItemsWithinPOI;

	bServerCombatTimerAppliesToGroup = Options.bServerCombatTimerAppliesToGroup;
	
	WeatherLengthVariation = Options.WeatherLengthVariation;
	WeatherBlendVariation = Options.WeatherBlendVariation;
	
	SaveConfig();
}

FString AIGameSession::ApproveLogin(const FString& Options)
{
	UWorld* const World = GetWorld();

	AGameMode* const GameMode = Cast<AGameMode>(World->GetAuthGameMode());
	if (AtCapacity(false)) return TEXT("SERVER_FULL");

	// Check for Passworded Server
	if (!ServerPassword.IsEmpty())
	{
		FString UserPassword = UGameplayStatics::ParseOption(Options, TEXT("Pass"));
		if (ServerPassword != UserPassword)
		{
			return TEXT("INVALID_PASSWORD");
		}
	}

	return TEXT("");
}

void AIGameSession::ValidatePlayer(const FString& Address, const TSharedPtr<const FUniqueNetId>& UniqueId, FString& ErrorMessage, bool bValidateAsSpectator)
{
	// Get World
	//UWorld* World = GetWorld();
	//check(World);
	
	// Get Network Driver
	//UNetDriver* NetDriver = GetWorld()->GetNetDriver();
	//check(NetDriver);

	// Server Local Address (Steam ID)
	//FString LocalAddress = NetDriver->LowLevelGetNetworkNumber();
}

bool AIGameSession::IsPlayerBanned(const FString& UniqueID)
{
	return IsPlayerBanned(FAlderonPlayerID(UniqueID));
}

bool AIGameSession::IsPlayerServerMuted(const FString& UniqueID)
{
	return IsPlayerServerMuted(FAlderonPlayerID(UniqueID));
}

bool AIGameSession::IsPlayerWhitelisted(const FString& UniqueID)
{
	return IsPlayerWhitelisted(FAlderonPlayerID(UniqueID));
}

FPlayerBan AIGameSession::GetBanInformation(const FAlderonPlayerID& AlderonId)
{
	for (const FPlayerBan& Ban : Bans)
	{
		if (!Ban.PlayerId.IsValid()) continue;

		if (Ban.PlayerId == AlderonId && !Ban.IsExpired())
		{
			return Ban;
		}
	}

	return FPlayerBan();
}

FPlayerMute AIGameSession::GetServerMuteInformation(const FAlderonPlayerID& AlderonId)
{
	for (const FPlayerMute& Mute : ServerMutes)
	{
		if (!Mute.PlayerId.IsValid()) continue;

		if (Mute.PlayerId == AlderonId && !Mute.IsExpired())
		{
			return Mute;
		}
	}

	return FPlayerMute();
}

bool AIGameSession::IsPlayerBanned(const FAlderonPlayerID& AlderonId)
{
	if (IsAdminID(AlderonId.ToDisplayString()) || IsDevID(AlderonId.ToDisplayString()) || GetNetMode() == NM_Standalone)
	{
		return false;
	}

	for (const FPlayerBan& Ban : Bans)
	{
		if (!Ban.PlayerId.IsValid()) continue;

		if (Ban.PlayerId == AlderonId && !Ban.IsExpired())
		{
			return true;
		}
	}

	return false;
}

bool AIGameSession::IsPlayerWhitelisted(const FAlderonPlayerID& AlderonId)
{
	return ServerWhitelist.Contains(AlderonId);
}

bool AIGameSession::IsPlayerServerMuted(const FAlderonPlayerID& AlderonId)
{
	for (const FPlayerMute& Mute : ServerMutes)
	{
		if (!Mute.PlayerId.IsValid()) continue;

		if (Mute.PlayerId == AlderonId && !Mute.IsExpired())
		{
			return true;
		}
	}

	return false;
}

bool AIGameSession::IsIPAddressBanned(const FString& IPAddress)
{
	for (const FPlayerBan& Ban : Bans)
	{
		if (Ban.IPAddress.IsEmpty()) continue;

		if (Ban.IPAddress == IPAddress && !Ban.IsExpired())
		{
			return true;
		}
	}

	return false;
}

bool AIGameSession::ProcessAutoLogin()
{
	// Default Online Subsystem Login Handling
	// We don't want to invoke this delegate as it could be called before Alderon Games login is complete
	FOnlineAutoLoginComplete CompletionDelegate;// = FOnlineAutoLoginComplete::CreateUObject(this, &ThisClass::OnAutoLoginComplete);
	bool bDefaultAsyncLoginStarted = UOnlineEngineInterface::Get()->AutoLogin(GetWorld(), 0, CompletionDelegate);

	// Alderon Games Login Handling
	IAlderonCommon& AlderonModule = IAlderonCommon::Get();
	IAlderonAuth& AlderonAuth = AlderonModule.GetAuthInterface();

	if (AlderonAuth.GetAuthResult() != EAlderonAuthResult::Success || AlderonAuth.GetAuthToken().IsEmpty())
	{
		AlderonAuth.GameSessionLoginCompleteDelegates = FOnlineAutoLoginComplete::CreateUObject(this, &ThisClass::OnAutoLoginComplete);
		return true;
	}
	
	// Not waiting for async login
	return false;
}

void AIGameSession::OnAutoLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& Error)
{
	IAlderonCommon& AlderonModule = IAlderonCommon::Get();
	IAlderonAuth& AlderonAuth = AlderonModule.GetAuthInterface();

	EAlderonAuthResult AuthResult = AlderonAuth.GetAuthResult();
	FString AuthToken = AlderonAuth.GetAuthToken();

	if (AuthResult != EAlderonAuthResult::Success)
	{
		UE_LOG(TitansNetwork, Error, TEXT("OnAutoLoginComplete: AuthResult is %s"), *IAlderonAuth::AuthResultToText(AuthResult).ToString());
	}

#if !WITH_EDITOR
	if (AuthToken.IsEmpty())
	{
		UE_LOG(TitansNetwork, Error, TEXT("OnAutoLoginComplete: AuthToken is empty"));
	}
#endif

#if UE_SERVER
	UWorld* World = GetWorld();
	if (World)
	{
		AIGameMode* IGameMode = Cast<AIGameMode>(World->GetAuthGameMode());
		if (IGameMode)
		{
			if (IGameMode->ManagedServerType != EManagedServer::None && !IGameMode->bManagedServerConnected)
			{
				// Skipping server registration until managed connection is complete (eg agones or edgegap)
				UE_LOG(TitansNetwork, Log, TEXT("IGameSession: OnAutoLoginComplete: Skipping server registration until managed service is connected."));
				IGameMode->bManagedServerRegister = true;
				return;
			}
		}
	}
#endif

	CheckServerRegisterReady();

	//UWorld* World = GetWorld();
	//if (UOnlineEngineInterface::Get()->IsLoggedIn(World, LocalUserNum))
	//{
	//	RegisterServer();
	//} else {
	//	RegisterServerFailed();
	//}
}

void AIGameSession::CheckServerRegisterReady()
{
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	
	if (AlderonCommon.IsWaitingForServerRegisterCallback())
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession:CheckServerRegisterReady: Still waiting for server register callback"));
		return;
	}

	if (AlderonCommon.HasServerRegisteredSuccessfully())
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession:CheckServerRegisterReady: Already registered server"));
		return;
	}
	
	UE_LOG(TitansNetwork, Log, TEXT("IGameSession:CheckServerRegisterReady"));
	
	if (IsBusy())
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession:CheckServerRegisterReady: Still busy"));
		return;
	}

	// Start Server Registration
	RegisterServer();

	// Flag other subscribers that it's ready
	OnGameSessionReady.ExecuteIfBound();
}

bool AIGameSession::CheckServerLoadTargetMapReady()
{
	if (bServerReloaded) return true;
	if (TargetMapLoadWait())
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameSession:CheckServerLoadTargetMapReady: Still busy"));
		return false;
	}
	
	const UWorld* const World = GetWorld();
	if (!World) return false;

	const UIGameInstance* const IGameInstance = UIGameplayStatics::GetIGameInstance(World);
	if (!IGameInstance) return false;

	AsyncTask(ENamedThreads::GameThread, []()
		{
		
		IAlderonUGC& AlderonUGC = IAlderonCommon::Get().GetUGCInterface();
		AlderonUGC.ServerReloadMap();
		
		});
	
	bServerReloaded = true;
	return true;
}

void AIGameSession::RegisterListenServer()
{
	UE_LOG(TitansNetwork, Log, TEXT("--------------[REGISTER LISTEN SERVER]----------------"));

	ServerStartupLoadedModWebHook();
}

bool AIGameSession::AllowServerRegistration()
{
#if UE_SERVER
	return true;
#else
	#if WITH_EDITOR || PLATFORM_CONSOLE
		// Registering Servers is disabled in the editor, consoles
		return false;
	#else
		bool bListenServer = IAlderonCommon::Get().GetRemoteConfig().GetRemoteConfigFlag(TEXT("bListenServer"));
		return (bListenServer);
	#endif
#endif

}

int AIGameSession::GetNetworkPort(ENetworkPortType PortType)
{
	int SelectedPort = 7777;

	FString PortString = "";
	if (FParse::Value(FCommandLine::Get(), TEXT("Port="), PortString))
	{
		SelectedPort = FCString::Atoi(*PortString);
	} else {
		SelectedPort = FCString::Atoi(*GConfig->GetStr(TEXT("URL"), TEXT("Port"), GEngineIni));
	}

	switch(PortType)
	{
		case ENetworkPortType::Primary:
		{
			return SelectedPort;
		}
		case ENetworkPortType::Secondary:
		{
			return SelectedPort + 1;
		}
		case ENetworkPortType::Rcon:
		{
			return SelectedPort + 2;
		}
		case ENetworkPortType::Stats:
		{
			return SelectedPort + 3;
		}
		default:
		{
			return 0;
		}
	}
}

FString AIGameSession::GetServerListIP()
{
	FString ServerListIP = "";

	// Command Line Arg
	FString CommandLineServerListIP = "";
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerListIP="), CommandLineServerListIP))
	{
		ServerListIP = CommandLineServerListIP;
	}
	else
	{
		FString ServerListIPEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("SERVER_IP"));
		ServerListIPEnv.TrimQuotesInline();

		if (!ServerListIPEnv.IsEmpty())
		{
			return ServerListIPEnv;
		}
	}

	if (OverrideServerListIP != "")
	{
		ServerListIP = OverrideServerListIP;
	}

	return ServerListIP;
}

int AIGameSession::GetServerListPort()
{
	int ServerListPort = GetNetworkPort(ENetworkPortType::Primary);

	// Allows the command line to override the server list port
	FString ServerListPortStr = TEXT("");
	if (FParse::Value(FCommandLine::Get(), TEXT("ServerListPort="), ServerListPortStr))
	{
		ServerListPort = FCString::Atoi(*ServerListPortStr);
	}
	else
	{
		FString ServerListPortEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("SERVER_PORT"));
		ServerListPortEnv.TrimQuotesInline();

		if (!ServerListPortEnv.IsEmpty())
		{
			return FCString::Atoi(*ServerListPortEnv);
		}
	}

	// Override Server List Port
	if (OverrideServerListPort != 0)
	{
		ServerListPort = OverrideServerListPort;
	}

	return ServerListPort;
}

void AIGameSession::RegisterServer()
{	
	// Wait until we are ready to register the server (The Engine can call this function)
	if (IsBusy()) return;

	IAlderonCommon& AlderonModule = IAlderonCommon::Get();
	IAlderonRemoteConfig& AlderonRemoteConfig = AlderonModule.GetRemoteConfig();

	if (!AllowServerRegistration())
	{
		UE_LOG(TitansNetwork, Warning, TEXT("---------[SERVER REGISTRATION IS DISABLED]--------"));
#if !UE_SERVER
		bool bListenServer = AlderonModule.GetRemoteConfig().GetRemoteConfigFlag(TEXT("bListenServer"));
		UE_LOG(TitansNetwork, Log, TEXT("RemoteConfig: bListenServer: %i"), bListenServer);
#endif
		return;
	}

	UE_LOG(TitansNetwork, Log, TEXT("--------------[REGISTER SERVER]----------------"));

	// Fetch game mode
	AIGameMode* IGameMode = Cast<AIGameMode>(GetWorld()->GetAuthGameMode());

	// Map Name
	FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);

	bool bFoundMapName = false;

	// Check Maps against Remote Config to prevent unreleased maps from registering
	for (const FMapInfo& MapInfo : IGameInstance->Maps)
	{
		if (MapInfo.Map == GetWorld()->GetPrimaryAssetId())
		{
			MapName = MapInfo.DisplayName.ToString();
			bFoundMapName = true;

			if (!MapInfo.RequiredRemoteConfig.IsEmpty())
			{
				bool bFlag = AlderonRemoteConfig.GetRemoteConfigFlag(MapInfo.RequiredRemoteConfig);
				if (!bFlag)
				{
					UE_LOG(TitansNetwork, Log, TEXT("Failed to register server as this map is not available: Flag %s"), *MapInfo.RequiredRemoteConfig);
					return;
				}
			}

			break;
		}
	}

	// Attempt to find a more friendly map name
	if (!bFoundMapName)
	{
		AIWorldSettings* IWorldSettings = AIWorldSettings::Get(GetWorld());
		if (IWorldSettings)
		{
			MapName = IWorldSettings->MapName;
		}
	}

	// Game Version
	FString GameVersion = AlderonModule.GetFullVersion();

	// Game Mode
	FString GameMode = (IGameMode != nullptr) ? IGameMode->DisplayName : TEXT("Unknown");

	// Validate Discord Setting to Prevent Abuse
	if (ServerDiscord.Contains(TEXT("/")) || ServerDiscord.Contains(TEXT("\\")) || ServerDiscord.Contains(TEXT(";")) ||
		ServerDiscord.Contains(TEXT(".")) || ServerDiscord.Contains(TEXT("http")) || ServerDiscord.Contains(TEXT("www")))
	{
		ServerDiscord = TEXT("");
		UE_LOG(TitansNetwork, Error, TEXT("IGameSession: Discord Invite Tag is Invalid"));
	}
	
	// Log Server Registration
	UE_LOG(TitansNetwork, Verbose, TEXT("AIGameSession::RegisterServer(%s,%s,%s)"), *ServerName, *MapName, *GameVersion);

	// Alderon Games - Register Server

	// Determine Server Port
	FString InstanceId = TEXT("0");
	FAlderonServerSettings ServerSettings{};

	IAlderonUGC& AlderonUGC = AlderonModule.GetUGCInterface();

	const TMap<FString, TStrongObjectPtr<UAlderonUGCDetails>> LoadedMods = AlderonUGC.GetLoadedMods();

	for (const TPair<FString, TStrongObjectPtr<UAlderonUGCDetails>>& LoadedMod : LoadedMods)
	{
		const FString& ModSku = LoadedMod.Key;
		const TStrongObjectPtr<UAlderonUGCDetails>& LoadedDetails = LoadedMod.Value;

		FAlderonModServerItem ModServerItem;
		ModServerItem.Sku = ModSku;
		if (LoadedDetails.IsValid())
		{
			ModServerItem.BuildSku = LoadedDetails->BuildSkuToDownload;
			ModServerItem.Id = LoadedDetails->UniqueID;
			FAlderonUGCBuild Build = LoadedDetails->Release.Build;
			ModServerItem.Version = Build.Id;
		}
		else
		{
			UE_LOG(TitansNetwork, Error, TEXT("IGameSession: ModId: %s UGCDetails is nullptr"), *ModSku);
		}

		ServerSettings.Mods.Add(ModServerItem);
	}

	ServerSettings.ContentPatches = AlderonUGC.GetContentPatchIDs();

	if (FParse::Value(FCommandLine::Get(), TEXT("InstanceId="), InstanceId) == false)
	{
		ServerSettings.InstanceID = FCString::Atoi(*InstanceId);
	}
	else
	{
		ServerSettings.InstanceID = FMath::RandRange(0, 999999);
	}

	ServerSettings.Name = ServerName;
	ServerSettings.Map = MapName;

	ServerSettings.IPAddress = GetServerListIP();
	ServerSettings.Port = GetServerListPort();

	if (!AlderonModule.IsDevBuild())
	{
		ServerSettings.Version = FCString::Atoi(*AlderonModule.GetInstalledRevision());
		ServerSettings.Branch = AlderonModule.GetAuthInterface().GetBranchKey();
	}
	else
	{
		ServerSettings.Version = 1;
		ServerSettings.Branch = TEXT("dev-build");
	}

	// Blank Region by default as its auto detected by server api
	FString SelectedRegion = TEXT("");

	// Determine Region by Command Line if passed
	FString CmdLineRegion;
	if (FParse::Value(FCommandLine::Get(), TEXT("Region="), CmdLineRegion))
	{
		SelectedRegion = CmdLineRegion;
	}

	ServerSettings.Region = SelectedRegion;
	ServerSettings.GameMode = GameMode;
	ServerSettings.PlayerLimit = MaxPlayers;
	ServerSettings.bPasswordProtected = (!ServerPassword.IsEmpty());
	ServerSettings.bDedicated = (GetNetMode() == ENetMode::NM_DedicatedServer);

	bool bSecure = false;

#if WITH_BATTLEYE_SERVER
	if (IsRunningDedicatedServer())
	{
		if (IBattlEyeServer::IsAvailable())
		{
			bSecure = IBattlEyeServer::Get().IsEnforcementEnabled();
		}
	}
#endif

	if (bSecure)
	{
		UE_LOG(TitansNetwork, Log, TEXT("---------[BATTLEYE IS ENABLED]--------"));
	} else {
		UE_LOG(TitansNetwork, Error, TEXT("---------[BATTLEYE IS DISABLED]--------"));
	}

	ServerSettings.bSecure = bSecure;

	TArray<FString> Platforms;

	// These platforms require a plugin for hosting to work properly
	// ps4 ps5 xb1 xsx switch

	FString PlatformsCmdLineArgs;
	FParse::Value(FCommandLine::Get(), TEXT("Platforms="), PlatformsCmdLineArgs);

	// Mods override configured list in config file
	if (!PlatformsCmdLineArgs.IsEmpty())
	{
		TArray<FString> CmdLinePlatforms;
		PlatformsCmdLineArgs.ParseIntoArray(CmdLinePlatforms, TEXT("+"));

		for (const FString& Platform : CmdLinePlatforms)
		{
			Platforms.AddUnique(Platform);
		}
	}

	if (Platforms.Num() == 0)
	{
		Platforms = { TEXT("windows"), TEXT("mac"), TEXT("linux"), TEXT("ios"), TEXT("android"), TEXT("ps4"), TEXT("ps5"), TEXT("xb1"), TEXT("xsx"), TEXT("switch")};
	}

	ServerSettings.Platforms = Platforms;

	AIGameState* IGameState = UIGameplayStatics::GetIGameState(this);
	if (IGameState)
	{
		ServerSettings.PerformanceCurrent = IGameState->GetServerTickInfo().CurrentTickRate;
		ServerSettings.PerformanceMax = IGameState->GetServerTickInfo().MaxTickRate;
		ServerSettings.PerformanceAverage = IGameState->GetServerTickInfo().AverageTickRate;
	}

	ServerSettings.DiscordInvite = ServerDiscord;

	AlderonModule.RegisterServer(ServerSettings);

#if UE_SERVER
	if (IGameMode->AgonesSDK)
	{
		IGameMode->AgonesSDK->SetPlayerCapacity(MaxPlayers, {}, {});
	}
#endif

	// Register Online Subsystem Server (Null)
#if UE_BUILD_DEVELOPMENT && false
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr SessionInt = OnlineSub->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			TSharedPtr<FOnlineSessionSettings> NewHostSettings = MakeShareable(new FOnlineSessionSettings());
			NewHostSettings->bIsDedicated = true;
			NewHostSettings->NumPrivateConnections = 0;
			NewHostSettings->NumPublicConnections = MaxPlayers;
			NewHostSettings->Set(SETTING_MAPNAME, MapName, EOnlineDataAdvertisementType::ViaOnlineService);
			NewHostSettings->Set(SETTING_GAMEMODE, GameMode, EOnlineDataAdvertisementType::ViaOnlineService);

			//TSharedPtr<class FIOnlineSessionSettings> ShooterHostSettings = MakeShareable(new FIOnlineSessionSettings(false, false, 16));
			//ShooterHostSettings->Set(SETTING_MATCHING_HOPPER, FString("TeamDeathmatch"), EOnlineDataAdvertisementType::DontAdvertise);
			//ShooterHostSettings->Set(SETTING_MATCHING_TIMEOUT, 120.0f, EOnlineDataAdvertisementType::ViaOnlineService);
			//ShooterHostSettings->Set(SETTING_SESSION_TEMPLATE_NAME, FString("GameSession"), EOnlineDataAdvertisementType::DontAdvertise);
			//ShooterHostSettings->Set(SETTING_GAMEMODE, FString("TeamDeathmatch"), EOnlineDataAdvertisementType::ViaOnlineService);
			//ShooterHostSettings->Set(SETTING_MAPNAME, GetWorld()->GetMapName(), EOnlineDataAdvertisementType::ViaOnlineService);
			//ShooterHostSettings->bAllowInvites = true;
			//ShooterHostSettings->bIsDedicated = true;
			//if (FParse::Param(FCommandLine::Get(), TEXT("forcelan")))
			//{
			//	UE_LOG(LogOnlineGame, Log, TEXT("Registering server as a LAN server"));
			//	//ShooterHostSettings->bIsLANMatch = true;
			//}
			//HostSettings = ShooterHostSettings;

			OnCreateSessionCompleteDelegateHandle = SessionInt->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);
			SessionInt->CreateSession(0, NAME_GameSession, *NewHostSettings);
		}
	}
#endif

	ServerStartupLoadedModWebHook();
}

void AIGameSession::ServerStartupLoadedModWebHook()
{
	IAlderonUGC& UGCInterface = IAlderonCommon::Get().GetUGCInterface();
	const TMap<FString, TStrongObjectPtr<UAlderonUGCDetails>> ServerMods = UGCInterface.GetLoadedMods();

	TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{};

	for (const auto& [Key, ModDetails] : ServerMods)
	{
		if (!ModDetails.IsValid())
		{
			continue;
		}

		WebHookProperties.Reserve(WebHookProperties.Num() + 6);

		WebHookProperties.Add(TEXT("ModName"), MakeShareable(new FJsonValueString(ModDetails->Name)));
		WebHookProperties.Add(TEXT("ModId"), MakeShareable(new FJsonValueString(ModDetails->UniqueID.ToString())));
		WebHookProperties.Add(TEXT("BuildId"), MakeShareable(new FJsonValueString(FString::FromInt(ModDetails->UnstableBuild.Id))));
		WebHookProperties.Add(TEXT("BuildSku"), MakeShareable(new FJsonValueString(ModDetails->UnstableBuild.Sku)));
		WebHookProperties.Add(TEXT("ModVersion"), MakeShareable(new FJsonValueString(FString::FromInt(ModDetails->Release.Build.Id))));
		WebHookProperties.Add(TEXT("DownloadedVersion"), MakeShareable(new FJsonValueString(ModDetails->BuildSkuToDownload)));

		ListOfSkuToCheck.Add(ModDetails->Sku);
	}

	/*for (const TPair<FString, TStrongObjectPtr<UAlderonUGCDetails>>& Mod : ServerMods)
	{
		const TStrongObjectPtr<UAlderonUGCDetails> ModDetails = Mod.Value;

		if (!ModDetails.IsValid())
		{
			continue;
		}

		WebHookProperties.Reserve(WebHookProperties.Num() + 6);

		WebHookProperties.Add(TEXT("ModName"), MakeShareable(new FJsonValueString(ModDetails->Name)));
		WebHookProperties.Add(TEXT("ModId"), MakeShareable(new FJsonValueString(ModDetails->UniqueID.ToString())));
		WebHookProperties.Add(TEXT("BuildId"), MakeShareable(new FJsonValueString(FString::FromInt(ModDetails->UnstableBuild.Id))));
		WebHookProperties.Add(TEXT("BuildSku"), MakeShareable(new FJsonValueString(ModDetails->UnstableBuild.Sku)));
		WebHookProperties.Add(TEXT("ModVersion"), MakeShareable(new FJsonValueString(FString::FromInt(ModDetails->Release.Build.Id))));
		WebHookProperties.Add(TEXT("DownloadedVersion"), MakeShareable(new FJsonValueString(ModDetails->BuildSkuToDownload)));

		ListOfSkuToCheck.Add(ModDetails->Sku);
	}*/

	TriggerWebHook(WEBHOOK_ServerLoadedMod, WebHookProperties);

	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle_CheckVersionUpdate,
		this,
		&AIGameSession::UpdateLoadedModVersionWebHook,
		3.f,
		false,
		0.f);
}

void AIGameSession::UpdateLoadedModVersionWebHook()
{
	FString RequestString = FString::Printf(TEXT("mods?limit=%i&filter[sku]="), ListOfSkuToCheck.Num());

	for (const FString& Sku : ListOfSkuToCheck)
	{
		RequestString.Append(Sku.ToUpper());
		RequestString.Append(TEXT(","));
	}
	RequestString.RemoveFromEnd(TEXT(","));

	FHttpRequestPtr HttpRequest = IAlderonCommon::CreateAuthRequest(EAlderonWebRequestVerb::GET);

	HttpRequest->OnProcessRequestComplete().BindLambda([&, HttpRequest](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		if (!bSucceeded || !HttpResponse.IsValid() || !EHttpResponseCodes::IsOk(HttpResponse->GetResponseCode()))
		{
			return;
		}

		const FString JsonResult = HttpResponse->GetContentAsString();
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonResult);

		if (!FJsonSerializer::Deserialize(Reader, JsonObject))
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> DataArray = JsonObject->GetArrayField(TEXT("data"));
		TMap<FString, TSharedPtr<FJsonValue>> UpdatedWebHookProperties{};

		for (const TSharedPtr<FJsonValue>& Data : DataArray)
		{
			TSharedPtr<FJsonObject> Result = Data->AsObject();
			const FString Sku = Result->GetStringField(TEXT("sku"));

			IAlderonUGC& UGCInterface = IAlderonCommon::Get().GetUGCInterface();
			const TStrongObjectPtr<UAlderonUGCDetails> SkuModDetails = UGCInterface.GetUGCDetails(Sku);

			if (!SkuModDetails.IsValid())
			{
				continue;
			}

			ProcessModeVersion(UGCInterface.GetLoadedMods(), SkuModDetails, UpdatedWebHookProperties);
		}
	});

	FString GameName = FString(); // eg path-of-titans
	GConfig->GetString(TEXT("AlderonGames"), TEXT("GameName"), GameName, GGameIni);

	HttpRequest->SetURL(TEXT("https://alderongames.com/api/ugc") / GameName / RequestString);
	HttpRequest->ProcessRequest();
}

void AIGameSession::ProcessModeVersion(const TMap<FString, TStrongObjectPtr<UAlderonUGCDetails>>& LoadedMods, TStrongObjectPtr<UAlderonUGCDetails> SkuModDetails, TMap<FString, TSharedPtr<FJsonValue>>& UpdatedWebHookProperties)
{
	// for (const auto& [Key, ModDetails] : ServerMods)
	for (const TPair<FString, TStrongObjectPtr<UAlderonUGCDetails>>& Mod : LoadedMods)
	{
		const TStrongObjectPtr<UAlderonUGCDetails> ModDetails = Mod.Value;

		// if mod details already exist in LoadedMods & compare BuildSku (version)
		const bool bModIdExist = ModDetails->UniqueID == SkuModDetails->UniqueID;
		const bool bModWithDiffVersion = ModDetails->Release.Build.Id != SkuModDetails->Release.Build.Id;

		if (!ModDetails.IsValid() && !bModIdExist && !bModWithDiffVersion)
		{
			continue;
		}

		// create a latest webhook
		UpdatedWebHookProperties.Reserve(UpdatedWebHookProperties.Num() + 7);

		UpdatedWebHookProperties.Add(TEXT("ModName"), MakeShareable(new FJsonValueString(SkuModDetails->Name)));
		UpdatedWebHookProperties.Add(TEXT("ModId"), MakeShareable(new FJsonValueString(SkuModDetails->UniqueID.ToString())));
		UpdatedWebHookProperties.Add(TEXT("BuildId"), MakeShareable(new FJsonValueString(FString::FromInt(SkuModDetails->UnstableBuild.Id))));
		UpdatedWebHookProperties.Add(TEXT("BuildSku"), MakeShareable(new FJsonValueString(SkuModDetails->UnstableBuild.Sku)));
		UpdatedWebHookProperties.Add(TEXT("CurrentModVersion"), MakeShareable(new FJsonValueString(FString::FromInt(ModDetails->Release.Build.Id))));
		UpdatedWebHookProperties.Add(TEXT("LatestModVersion"), MakeShareable(new FJsonValueString(FString::FromInt(SkuModDetails->Release.Build.Id))));
		UpdatedWebHookProperties.Add(TEXT("DownloadedVersion"), MakeShareable(new FJsonValueString(ModDetails->BuildSkuToDownload)));

		break;
	}

	TriggerWebHook(WEBHOOK_ServerUpdatedMod, UpdatedWebHookProperties);
}

void AIGameSession::UnRegisterServer(bool bShuttingDown)
{
	if (!AllowServerRegistration())
	{
#if !WITH_EDITOR
		UE_LOG(TitansNetwork, Warning, TEXT("---------[SERVER UN-REGISTRATION IS DISABLED ON THIS PLATFORM]--------"));
		return;
#endif
	}

	UE_LOG(TitansNetwork, Log, TEXT("--------------[UN-REGISTER SERVER]----------------"));

	IAlderonCommon& AlderonModule = IAlderonCommon::Get();
	AlderonModule.UnregisterServer();
}

bool AIGameSession::IsBusy() const
{
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	IAlderonAuth& AlderonAuth = AlderonCommon.GetAuthInterface();
	IAlderonRemoteConfig& AlderonRemoteConfig = AlderonCommon.GetRemoteConfig();
	EAlderonAuthResult AuthResult = AlderonAuth.GetAuthResult();
	IAlderonUGC& AlderonUGC = AlderonCommon.GetUGCInterface();
	UAlderonContent& AlderonContent = AlderonCommon.GetContentInterface();

	if (AlderonAuth.GetAuthToken().IsEmpty())
	{
		UE_LOG(TitansNetwork, Warning, TEXT("IGameSession - Waiting for login to finish: AuthToken is Empty"));
		return true;
	}

	if (AuthResult != EAlderonAuthResult::Success)
	{
		UE_LOG(TitansNetwork, Warning, TEXT("IGameSession - AuthResult != Success"));
		if (AuthResult != EAlderonAuthResult::Waiting)
		{
			UE_LOG(TitansNetwork, Warning, TEXT("IGameSession - Scheduling another auth attempt due to error"));
#if WITH_DEMO
			AlderonAuth.StartupAuth(TEXT("demo-public-test"));
#else
			AlderonAuth.StartupAuth(TEXT("production"));
#endif
		}
		return true;
	}

#if UE_SERVER
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(TitansNetwork, Warning, TEXT("Server Registration - Waiting for valid UWorld"));
		return true;
	}

	// Server default map is ServerEmptyLevel, so we dont want to register until we leave that map
	const UGameMapsSettings* GameMapsSettings = GetDefault<UGameMapsSettings>();
	FString DefaultMap = GameMapsSettings->GetGameDefaultMap();
	if (DefaultMap.Contains(World->GetMapName()))
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for map load"));
		return true;
	}

	UIGameInstance* const IGameInstance = UIGameplayStatics::GetIGameInstance(World);
	if (!IGameInstance)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for valid IGameInstance"));
		return true;
	}

	FPhysScene* PhysScene = World->GetPhysicsScene();
	if (!PhysScene)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for valid Physics Scene"));
		return true;
	}

	Chaos::FPBDRigidsSolver* Solver = PhysScene->GetSolver();
	if (!Solver)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for valid Physics Solver"));
		return true;
	}

	// We wait 20 frames to ensure that the physics scene is fully initialized, there is no way to check
	// init usually happens on frame 0 and frame 5/6 but added a buffer to be safe
	const int32 PhysicsInitWaitFrames = 20;
	if (Solver->GetCurrentFrame() < PhysicsInitWaitFrames)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for ensured full Physics Init"));
		return true;
	}

	const AIWorldSettings* const IWorldSettings = Cast<AIWorldSettings>(World->GetWorldSettings());
	if (!IWorldSettings)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for valid IWorldSettings"));
		return true;
	}

	const AIGameMode* const IGameMode = Cast<AIGameMode>(World->GetAuthGameMode());
	if (!IGameMode)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for valid IGameMode"));
		return true;
	}

	const AIQuestManager* const IQuestManager = Cast<AIQuestManager>(IWorldSettings->QuestManager);
	if (!IQuestManager || !IQuestManager->bQuestsLoaded)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for Quests to load"));
		return true;
	}

	if (IGameMode->ManagedServerType != EManagedServer::None && !IGameMode->bManagedServerConnected)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for Managed Server to finish connecting"));
		return true;
	}

	if (!IGameMode->bLockingLevelLoadCompleted)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for blocking level load"));
		return true;
	}

	if (!AlderonRemoteConfig.IsLoaded())
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for remote config"));
		return true;
	}

	// Waiting for hotpatch to finish updating
	if (AlderonContent.HasActiveDownload() || !AlderonContent.HasCompletedInitialDownload()) 
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for AlderonContent"));
		return true;
	}
	
	// Waiting for mods to finish updating
	if (!AlderonUGC.ModsWaitingUpdate.IsEmpty())
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for mod updates"));
		return true;
	}
	if (!AlderonUGC.GetDownloadingMods().IsEmpty())
	{
		UE_LOG(TitansNetwork, Log, TEXT("Server Registration - Waiting for mods to finish downloading"));
		return true;
	}
#endif

	return false;
}

bool AIGameSession::TargetMapLoadWait() const
{
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	IAlderonAuth& AlderonAuth = AlderonCommon.GetAuthInterface();
	IAlderonRemoteConfig& AlderonRemoteConfig = AlderonCommon.GetRemoteConfig();
	EAlderonAuthResult AuthResult = AlderonAuth.GetAuthResult();
	IAlderonUGC& AlderonUGC = AlderonCommon.GetUGCInterface();
	UAlderonContent& AlderonContent = AlderonCommon.GetContentInterface();

	if (AlderonAuth.GetAuthToken().IsEmpty())
	{
		// Waiting for login to finish
		UE_LOG(TitansNetwork, Warning, TEXT("AIGameSession::IsReadyForTargetMap(): Waiting for login to finish: AuthToken is Empty"));
		return true;
	}

	if (AuthResult != EAlderonAuthResult::Success)
	{
		UE_LOG(TitansNetwork, Warning, TEXT("AIGameSession::IsReadyForTargetMap(): AuthResult != Success"));
		return true;
	}

	UWorld* const World = GetWorld();
	if (!World) return true;

	const UIGameInstance* const IGameInstance = UIGameplayStatics::GetIGameInstance(World);
	if (!IGameInstance) return true;

	const AIWorldSettings* const IWorldSettings = Cast<AIWorldSettings>(World->GetWorldSettings());
	if (!IWorldSettings) return true;

	const AIGameMode* const IGameMode = Cast<AIGameMode>(World->GetAuthGameMode());
	if (!IGameMode) return true;

	// We are using a managed server type however its not connected yet, we are busy waiting for it
	if (IGameMode->ManagedServerType != EManagedServer::None && !IGameMode->bManagedServerConnected)
	{
		return true;
	}

	// Waiting for remote config to load
	if (!AlderonRemoteConfig.IsLoaded()) return true;

	// Waiting for hotpatch to finish updating
	if (AlderonContent.HasActiveDownload() || !AlderonContent.HasCompletedInitialDownload()) return true;

	// Waiting for mods to finish updating
	if (!AlderonUGC.ModsWaitingUpdate.IsEmpty()) return true;
	if (!AlderonUGC.GetDownloadingMods().IsEmpty()) return true;
	
	return false;
}

void AIGameSession::OnServerOTPGenerated(const FString& Otp, const FString& Token, const FAlderonServerUserDetails& UserDetails, AIPlayerController* IPlayerController, AIPlayerState* IPlayerState)
{
	check(IsInGameThread());

	if (!IPlayerController || !IPlayerController->IsValidLowLevel())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnServerOTPGenerated: IPlayerController is nullptr"));
		return;
	}

	if (!IPlayerState || !IPlayerState->IsValidLowLevel())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnServerOTPGenerated: IPlayerState is nullptr"));
		return;
	}

	UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::OnServerOTPGenerated: Otp: %s Token: %s PlayerId: %s"), TEXT("[Redacted]"), TEXT("[Redacted]"), *IPlayerState->GetAlderonID().ToDisplayString());

	IPlayerState->UserToken = UserDetails.UserToken;
	IPlayerState->UserRemoteConfigFlagsServerCache = UserDetails.UserRemoteConfigFlags;
	IPlayerState->UserRemoteConfigKeysServerCache = UserDetails.UserRemoteConfigKeys;

#if UE_SERVER
	if (UserDetails.UserToken.IsEmpty())
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: UserTokenIsEmpty"));
		FString SentryMsg = FString::Printf(TEXT("Client Id: %s OTP: %s Token: %s UserToken: %s RequestToken: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *Otp, *Token, *UserDetails.UserToken, *UserDetails.ServerRequestToken);

		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);

		// Intentionally no return out here
	}

	if (Otp.IsEmpty() || Token.IsEmpty())
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: OtpOrTokenIsEmpty"));
		FString SentryMsg = FString::Printf(TEXT("Client Id: %s OTP: %s Token: %s RequestToken: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *Otp, *Token, *UserDetails.ServerRequestToken);

		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);

		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerOTPTokenEmpty"));
		KickPlayer(IPlayerController, KickMessage);
		return;
	}

	if (IPlayerState->EncryptionToken != Token)
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: TokenMismatch Client Id: %s Server Id: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *UserDetails.ID.ToDisplayString());
		FString SentryMsg = FString::Printf(TEXT("OTP: %s Token: %s PlayerEncryptionToken: %s RequestToken: %s"), *Otp, *Token, *IPlayerState->EncryptionToken, *UserDetails.ServerRequestToken);

		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);

		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerOTPTokenMismatch"));
		KickPlayer(IPlayerController, KickMessage);
		return;
	}

	// Carry Over Entitlements to Player State
	IPlayerState->EntitlementAttributes.EntitlementAttributes = UserDetails.EntitlementAttributes.EntitlementAttributes;

	// Verify User Alderon ID is Valid
	if (!IPlayerState->GetAlderonID().IsValid())
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: ClientAlderonIdVerifyFail Client Id: %s Server Id: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *UserDetails.ID.ToDisplayString());
		FString SentryMsg = FString::Printf(TEXT("OTP: %s Token: %s"), *Otp, *Token);

		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);

		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerAGIDVerificationFailed"));
		KickPlayer(IPlayerController, KickMessage);

		return;
	}

	// Server User Alderon ID is Valid
	if (!UserDetails.ID.IsValid())
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: ServerAlderonIdVerifyFail Client Id: %s Server Id: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *UserDetails.ID.ToDisplayString());
		FString SentryMsg = FString::Printf(TEXT("OTP: %s Token: %s"), *Otp, *Token);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);
		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);

		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerAGIDVerificationFailed"));
		KickPlayer(IPlayerController, KickMessage);

		return;
	}

	// Verify User Alderon Games Id Spoofing
	if (IPlayerState->GetAlderonID() != UserDetails.ID)
	{
		FString SentryTitle = FString::Printf(TEXT("ServerOTP: AlderonIdMismatch Client Id: %s Server Id: %s"), *IPlayerState->GetAlderonID().ToDisplayString(), *UserDetails.ID.ToDisplayString());
		FString SentryMsg = FString::Printf(TEXT("OTP: %s Token: %s"), *Otp, *Token);
		IAlderonSentry::SendEvent(SentryTitle, SentryMsg, ESentryLevel::Error);
		UE_LOG(TitansNetwork, Error, TEXT("%s: %s"), *SentryTitle, *SentryMsg);

		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerAGIDVerificationFailed"));
		KickPlayer(IPlayerController, KickMessage);
		return;
	}

	// Verify User Display Name Spoofing
	if (IPlayerState->GetPlayerName() != UserDetails.DisplayName)
	{
		IPlayerState->SetPlayerName(UserDetails.DisplayName);
	}

	FString AlderonDisplayId = IPlayerState->GetAlderonID().ToDisplayString();

	// Admins from Control Panel / Admin Key Group
	if (UserDetails.bAdmin || UserDetails.EntitlementAttributes.HasEntitlementAttribute(TEXT("access"), TEXT("admin")))
	{
		IPlayerState->SetIsServerAdmin(true, IPlayerState->GetPlayerRole().bStealthServerAdmin);
		InstanceServerAdmins.AddUnique(AlderonDisplayId);
	}

	// Developer Key Group
	if (UserDetails.EntitlementAttributes.HasEntitlementAttribute(TEXT("access"), TEXT("developer")))
	{
		IPlayerState->SetIsGameDev(true, IPlayerState->GetPlayerRole().bStealthServerAdmin);
		InstanceGameDevs.AddUnique(AlderonDisplayId);
	} else {
		if (InstanceGameDevs.Contains(AlderonDisplayId))
		{
			InstanceGameDevs.Remove(AlderonDisplayId);
		}
		IPlayerState->SetIsGameDev(false, false);
	}

	// Player Login Webhook
	if (AIGameSession::UseWebHooks(WEBHOOK_PlayerLogin))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("ServerName"), MakeShareable(new FJsonValueString(ServerName))},
			{ TEXT("PlayerName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName()))},
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString()))},
			{ TEXT("BattlEyeGUID"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToBattlEyeGUID()))},
			{ TEXT("bServerAdmin"), MakeShareable(new FJsonValueBoolean(IPlayerState->IsServerAdmin()))},
		};

		TriggerWebHook(WEBHOOK_PlayerLogin, WebHookProperties);
	}
#endif

	// Request additional info about attributes and rewards
	if (!UserDetails.UserToken.IsEmpty())
	{
		IAlderonAuth& AlderonAuth = IAlderonCommon::Get().GetAuthInterface();
		FAlderonServerOTPExtraInfo OnCompleted = FAlderonServerOTPExtraInfo::CreateUObject(this, &AIGameSession::OnOTPAdditionalInfo, MakeWeakObjectPtr<AIPlayerController>(IPlayerController), MakeWeakObjectPtr<AIPlayerState>(IPlayerState));
		AlderonAuth.ServerOTPAdditionalInfo(OnCompleted, UserDetails.UserToken);
	}

	// The rest of the verification is moved to OnOTPAdditionalInfo, so that their inventory can be checked for ownership.
}

void AIGameSession::OnOTPAdditionalInfo(const FAlderonServerUserExtraInfo& UserDetails, TWeakObjectPtr<AIPlayerController> IPlayerController, TWeakObjectPtr<AIPlayerState> IPlayerState)
{
	check(IsInGameThread());

	if (!IPlayerController.IsValid())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnOTPAdditionalInfo: IPlayerController is nullptr"));
		return;
	}

	if (!IPlayerState.IsValid())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnOTPAdditionalInfo: IPlayerState is nullptr"));
		return;
	}

	UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::OnOTPAdditionalInfo: PlayerId: %s"), *IPlayerState->GetAlderonID().ToDisplayString());

	// Copy Reward Attributes
	IPlayerState->RewardAttributes = UserDetails.RewardAttributes;
	IPlayerState->ServerExtraUserInfo = UserDetails;

	const FString AlderonToken = FPlatformMisc::GetEnvironmentVariable(TEXT("AG_TOKENS_REWARDS"));
	const bool bOfficialServer = !AlderonToken.IsEmpty();

	if (AIGameState* const IGameState = UIGameplayStatics::GetIGameState(this))
	{
		IGameState->SetIsConnectedToOfficialServer(bOfficialServer);
	}

	EGameOwnership AuthoritiveGameOwnership = UIGameInstance::GetGameOwnership(IPlayerState.Get());
	if (IPlayerState->GetGameOwnership() > AuthoritiveGameOwnership)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnOTPAdditionalInfo: Ownership Verification Failed Server: %i Client: %i"), AuthoritiveGameOwnership, IPlayerState->GetGameOwnership());
		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerOwnershipVerificationFailed"));
		KickPlayer(IPlayerController.Get(), KickMessage);
		return;
	}

	if (!bOfficialServer && AuthoritiveGameOwnership != EGameOwnership::Full)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnOTPAdditionalInfo: Free client joining community server"));
		FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerCommunityOwnershipVerificationFailed"));
		KickPlayer(IPlayerController.Get(), KickMessage);
		return;
	}

	IPlayerState->SetPlayerValidated(true);

	if (IPlayerState->GetNetMode() != ENetMode::NM_Client)
	{
		IPlayerState->OnRep_PlayerValidated();
	}

#if UE_BUILD_DEVELOPMENT
	//AIPlayerState* PlayerState = IPlayerController->GetPlayerState<AIPlayerState>();
	//if (PlayerState && PlayerState != IPlayerState)
	//{
	//	UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnServerOTPGenerated: PlayerState: %s IPlayerState: %s"), *PlayerState->GetPlayerName(), *IPlayerState->GetPlayerName());
	//	UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnServerOTPGenerated: PlayerState: %s IPlayerState: %s"), *PlayerState->GetUniquePlayerId(), *IPlayerState->GetUniquePlayerId());
	//}

	//UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::OnServerOTPGenerated: bPlayerValidated: %s bPlayerValidated: %s"), IPlayerController->GetPlayerState<AIPlayerState>()->bPlayerValidated ? TEXT("true") : TEXT("false"), IPlayerState->bPlayerValidated ? TEXT("true") : TEXT("false"));
#endif

	// Register Player with Alderon Games
	if (AllowServerRegistration())
	{
		IAlderonCommon& AlderonModule = IAlderonCommon::Get();

		bool bIsReserved = IPlayerState->IsServerAdmin() || IPlayerState->IsGameDev();

		if (IPlayerState->GetPlayerRole().bAssigned)
		{
			if (IPlayerState->GetPlayerRole().bReservedSlot)
			{
				bIsReserved = true;
			}
		}

		// Set tracking flag on player state for changes in reserved status later
		IPlayerState->SetIsReserved(bIsReserved);

		const FUniqueNetIdRepl& UniqueId = IPlayerState->GetPlatformUniqueId();
		FString PlatformUserId = UniqueId.ToString();

		AlderonModule.RegisterPlayer(IPlayerState->GetAlderonID(), IPlayerController.Get(), PlatformToServerApiString(IPlayerState->GetPlatform()), PlatformUserId, bIsReserved);

		// Register Player with Session
		//NewPlayer->PlayerState->RegisterPlayerWithSession(bWasFromInvite);
	}

#if UE_SERVER
	UWorld* World = GetWorld();
	check(World);
	AIGameMode* IGameMode = Cast<AIGameMode>(World->GetAuthGameMode());
	check(IGameMode);
	if (IGameMode->AgonesSDK)
	{
		IGameMode->AgonesSDK->PlayerConnect(IPlayerState->GetAlderonID().ToString(), {}, {});
	}
#endif

	GenerateReferFriendRewards(UserDetails, IPlayerController.Get(), IPlayerState.Get());
}

void AIGameSession::RefreshPlayerStateExtraInfo(AIPlayerState* IPlayerState, FPlayerExtraInfoRefreshed OnComplete)
{
	IAlderonAuth& AlderonAuth = IAlderonCommon::Get().GetAuthInterface();
	FAlderonServerOTPExtraInfo OnCompleted = FAlderonServerOTPExtraInfo::CreateUObject(this, &AIGameSession::RefreshPlayerStateExtraInfoCallback, MakeWeakObjectPtr<AIPlayerState>(IPlayerState), OnComplete);
	AlderonAuth.ServerOTPAdditionalInfo(OnCompleted, IPlayerState->UserToken);
}

void AIGameSession::RefreshPlayerStateExtraInfoCallback(const FAlderonServerUserExtraInfo& UserDetails, TWeakObjectPtr<AIPlayerState> IPlayerState, FPlayerExtraInfoRefreshed OnComplete)
{
	if (!ensure(IPlayerState.Get())) return;

	IPlayerState->ServerExtraUserInfo = UserDetails;
	OnComplete.ExecuteIfBound(IPlayerState.Get());
}

void AIGameSession::GenerateReferFriendRewards(const FAlderonServerUserExtraInfo& UserDetails, AIPlayerController* IPlayerController, AIPlayerState* IPlayerState)
{
	FString AlderonToken = FPlatformMisc::GetEnvironmentVariable(TEXT("AG_TOKENS_REWARDS"));
	if (AlderonToken.IsEmpty())
	{
		//UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::GenerateReferFriendRewards: AG_TOKENS_REWARDS is empty. This must be set for official servers"));
		return;
	}

	FAlderonPlayerID TargetAlderonId = IPlayerState->GetAlderonID();

	if (!TargetAlderonId.IsValid())
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Invalid TargetAlderonId"));
		return;
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (!IGameInstance) return;

	UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: TargetAlderonId: %s"), *TargetAlderonId.ToDisplayString());

	// Home Cave Deco (Used Later)
	//FString HomecaveDecoKey = TEXT("refer-deco-statue-prowlingallos");
	//FString HomecaveDecoAttribute = TEXT("refer_deco_statue_prowlingallos");

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	IAlderonReferFriend& AlderonReferFriend = AlderonCommon.GetReferFriendInterface();

	// Pending players we need to add rewards for
	//TArray<FAlderonPlayerID> PendingRewardReferralPlayers;

	// All Referrals for group buff
	TArray<FAlderonPlayerID> AllReferrals;

	// Create list of all referrals combined
	TArray<FAlderonReferral> TotalReferrals = UserDetails.ReferralInfo.Active;

	// Person who referred you
	FAlderonReferral YourReferrer = UserDetails.ReferralInfo.Referrer;
	if (YourReferrer.User.ID.IsValid())
	{
		AllReferrals.Add(YourReferrer.User.ID);
		TotalReferrals.Add(YourReferrer);
	}

	for (const FAlderonReferral& Referral : TotalReferrals)
	{
		//UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Checking Player: %s Referral %s"), *TargetAlderonId.ToDisplayString(), *Referral.User.ID.ToDisplayString());

		AllReferrals.Add(Referral.User.ID);

		//TSharedPtr<FAlderonRewardReferral> Reward = AlderonReferFriend.GetRewardForReferral(UserDetails.ReferralRewards, Referral.User.ID, TArray<FString>{HomecaveDecoAttribute});
		//if (Reward.IsValid())
		//{
		//	UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s Referral %s already has reward with attribute: %s"), *TargetAlderonId.ToDisplayString(), *Referral.User.ID.ToDisplayString(), *Reward->Attribute);
		//} else {
		//	// We don't have a reward from this player yet, add to list
		//	PendingRewardReferralPlayers.Add(Referral.User.ID);
		//}
	}

	IPlayerState->GetReferrals_Mutable() = AllReferrals;

	/*
	if (PendingRewardReferralPlayers.Num() == 0)
	{
		// no referred players to unlock reward
		UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s no referred players to unlock reward"), *TargetAlderonId.ToDisplayString());
		return;
	}

	// Home cave Deco Reward
	{
		bool bUnlocked = UserDetails.RewardAttributes.HasAttribute(HomecaveDecoAttribute);
		if (!bUnlocked && AllReferrals.Num() > 0)
		{
			TSharedPtr<FAlderonRewardReferral> Reward = AlderonReferFriend.GetRewardByKey(UserDetails.ReferralRewards, HomecaveDecoKey);
			if (!Reward.IsValid())
			{
				// Generate Reward Struct and hand it to the API so the player can get their reward
				FAlderonRewardReferral RewardReferral;
				RewardReferral.TargetId = TargetAlderonId;
				RewardReferral.Attribute = HomecaveDecoAttribute;
				RewardReferral.Key = HomecaveDecoKey;
				RewardReferral.Referrer = AllReferrals[0];

				UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s Creating Reward: Key %s Attribute: %s Referrer: %s"),
					*TargetAlderonId.ToDisplayString(),
					*RewardReferral.Key,
					*RewardReferral.Attribute,
					*RewardReferral.Referrer.ToDisplayString()
				);

				AlderonReferFriend.GenerateReward(RewardReferral);
			}
		}
	}

	// Calculate Unearned rewards
	TArray<FReferFriendReward> UnearnedRewards;
	for (const FReferFriendReward& Reward : IGameInstance->ReferFriendRewards)
	{
		bool bUnlocked = UserDetails.RewardAttributes.HasAttribute(Reward.RewardAttribute);

		if (bUnlocked)
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: UnearnedRewardCalculation: Player %s already unlocked %s"), *TargetAlderonId.ToDisplayString(), *Reward.RewardAttribute);
		}
		else
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: UnearnedRewardCalculation: Player %s hasn't unlocked %s"), *TargetAlderonId.ToDisplayString(), *Reward.RewardAttribute);
			UnearnedRewards.Add(Reward);
		}
	}

	// Already have all the rewards you can earn, skip
	if (UnearnedRewards.Num() == 0)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s already earned all of the rewards"), *TargetAlderonId.ToDisplayString());
		return;
	}

	for (const FAlderonPlayerID& ReferRewardPlayerID : PendingRewardReferralPlayers)
	{
		if (!ReferRewardPlayerID.IsValid())
		{
			UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::GenerateReferFriendRewards: Invalid ReferRewardPlayerId: %s"), *TargetAlderonId.ToDisplayString());
			continue;
		}

		if (UnearnedRewards.Num() == 0)
		{
			// Unable to give a reward for %s as we have ran out of rewards
			UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s ran out of rewards to earn"), *TargetAlderonId.ToDisplayString());
			return;
		}

		// Fetch a remaining unclaimed reward
		FReferFriendReward UnclaimedReward = UnearnedRewards[0];

		// Generate Reward Struct and hand it to the API so the player can get their reward
		FAlderonRewardReferral RewardReferral;
		RewardReferral.TargetId = TargetAlderonId;
		RewardReferral.Attribute = UnclaimedReward.RewardAttribute;
		RewardReferral.Key = UnclaimedReward.RewardKey;
		RewardReferral.Referrer = ReferRewardPlayerID;
		//RewardReferral.Amount = UnclaimedReward.Amount;

		TSharedPtr<FAlderonRewardReferral> Reward = AlderonReferFriend.GetRewardByKey(UserDetails.ReferralRewards, UnclaimedReward.RewardKey);
		if (!Reward.IsValid())
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::GenerateReferFriendRewards: Player: %s Creating Reward: Key %s Attribute: %s Referrer: %s"), 
				*TargetAlderonId.ToDisplayString(), 
				*RewardReferral.Key,
				*RewardReferral.Attribute,
				*RewardReferral.Referrer.ToDisplayString()
			);

			AlderonReferFriend.GenerateReward(RewardReferral);
		}
		else
		{
			UE_LOG(TitansNetwork, Error, TEXT("AIGameSession::GenerateReferFriendRewards: Reward Object Already Exists! Player: %s Reward: Key %s Attribute: %s Referrer: %s"),
				*TargetAlderonId.ToDisplayString(),
				*RewardReferral.Key,
				*RewardReferral.Attribute,
				*RewardReferral.Referrer.ToDisplayString()
			);
		}
	}
	*/
}

void AIGameSession::RegisterPlayer(APlayerController* NewPlayer, const TSharedPtr<const FUniqueNetId>& UniqueId, bool bWasFromInvite)
{
	AIPlayerController* IPlayerController = Cast<AIPlayerController>(NewPlayer);
	if (!IPlayerController)
	{
		// This can happen in the main menu
		//UE_LOG(TitansLog, Error, TEXT("AIGameSession:RegisterPlayer: IPlayerController is nullptr."));
		return;
	}

	// Set Player's ID
	AIPlayerState* IPlayerState = NewPlayer->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		// This can happen in the main menu
		//UE_LOG(TitansLog, Error, TEXT("AIGameSession:RegisterPlayer: IPlayerState is nullptr."));
		return;
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	NewPlayer->PlayerState->SetPlayerId(GetNextPlayerID());
	NewPlayer->PlayerState->SetUniqueId(UniqueId);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (GetNetMode() == ENetMode::NM_ListenServer || GetNetMode() == ENetMode::NM_Standalone || WITH_EDITOR)
	{
		// If we are listen server then we want the first player, the "host", to be admin by default
		// If we are standalone then we can do the same, as it is always singleplayer
		// Also, in the editor, we can add the first player as an admin
		if (IPlayerController == UGameplayStatics::GetPlayerController(this, 0))
		{
			AddAdmin(NewPlayer);
		}
		// if we are standalone then we can validate
		IPlayerState->SetPlayerValidated(true);
	}

#if WITH_EDITOR
	// Increment PID Counter
	PIDCount++;

	IPlayerState->SetPIEID(PIDCount);

	if (!IPlayerState->GetAlderonID().IsValid())
	{
		IPlayerState->SetAlderonID(static_cast<uint32>(PIDCount));
	}

	// Always be validated in the editor
	IPlayerState->SetPlayerValidated(true);
#endif

	if (IPlayerState->EncryptionToken.IsEmpty())
	{
#if !WITH_EDITOR
		UE_LOG(TitansLog, Error, TEXT("AIGameSession:RegisterPlayer: EncryptionToken is Empty."));
#endif

		if (UE_BUILD_SHIPPING && UE_SERVER && !WITH_EDITOR)
		{
			FText KickMessage = FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerEmptyEncryptionToken"));
			KickPlayer(IPlayerController, KickMessage);
		}
	}
	else 
	{
		// Server Otp Handling
		AIGameMode* IGameMode = UIGameplayStatics::GetIGameMode(this);
		check(IGameMode);
		FOTPCache OtpCache = IGameMode->FetchOtpCache(IPlayerState->EncryptionToken);
		
		if (!OtpCache.Token.IsEmpty())
		{
			OnServerOTPGenerated(OtpCache.Otp, OtpCache.Token, OtpCache.UserInfo, IPlayerController, IPlayerState);
		}
		else
		{
			IAlderonAuth& AlderonAuth = IAlderonCommon::Get().GetAuthInterface();
			FAlderonServerOTPGenerated OnCompleted = FAlderonServerOTPGenerated::CreateUObject(this, &AIGameSession::OnServerOTPGenerated, IPlayerController, IPlayerState);
			AlderonAuth.ServerOTP(OnCompleted, IPlayerState->EncryptionToken);
		}
	}
}

void AIGameSession::UnregisterPlayer(const APlayerController* ExitingPlayer)
{
	if (!ExitingPlayer)
	{
		// This can happen on the main menu
		//UE_LOG(TitansLog, Error, TEXT("AIGameSession:UnregisterPlayer: Failed ExitingPlayer is nullptr."));
		return;
	}
	
	AIPlayerState* IPlayerState = ExitingPlayer->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		// This can happen in the main menu
		//UE_LOG(TitansLog, Error, TEXT("AIGameSession:UnregisterPlayer: Failed ExitingPlayer is nullptr."));
		return;
	}
	
	if (AllowServerRegistration())
	{
		// Register Player with Alderon Games
		if (!IPlayerState->GetAlderonID().IsValid())
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameSession:UnregisterPlayer: AlderonID is invalid."));
			return;
		}

		if (!IPlayerState->IsPlayerValidated())
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameSession:UnregisterPlayer: bPlayerValidated is false."));
			return;
		}

		// Player Logout Webhook
		if (AIGameSession::UseWebHooks(WEBHOOK_PlayerLogout))
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
			{
				{ TEXT("ServerName"), MakeShareable(new FJsonValueString(ServerName))},
				{ TEXT("PlayerName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName())) },
				{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString())) },
				{ TEXT("BattlEyeGUID"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToBattlEyeGUID())) },
			};
			
			TriggerWebHook(WEBHOOK_PlayerLogout, WebHookProperties);
		}

		IAlderonCommon& AlderonModule = IAlderonCommon::Get();

		const FUniqueNetIdRepl& UniqueId = IPlayerState->GetPlatformUniqueId();
		FString PlatformUserId = UniqueId.ToString();

		AlderonModule.UnregisterPlayer(IPlayerState->GetAlderonID(), ExitingPlayer, PlatformToServerApiString(IPlayerState->GetPlatform()), PlatformUserId);
	}

#if UE_SERVER
	UWorld* World = GetWorld();
	check(World);
	AIGameMode* IGameMode = Cast<AIGameMode>(World->GetAuthGameMode());
	check(IGameMode);
	if (IGameMode->AgonesSDK)
	{
		IGameMode->AgonesSDK->PlayerDisconnect(IPlayerState->GetAlderonID().ToString(), {}, {});
	}
#endif

	//if (GetNetMode() != NM_Standalone &&
	//	ExitingPlayer &&
	//	ExitingPlayer->PlayerState &&
	//	ExitingPlayer->PlayerState->UniqueId.IsValid() &&
	//	ExitingPlayer->PlayerState->UniqueId->IsValid())
	//{
	//
	//	Super::UnregisterPlayer(ExitingPlayer->PlayerState->SessionName, ExitingPlayer->PlayerState->UniqueId);
	//}
}

void AIGameSession::PostLogin(APlayerController* NewPlayer)
{
}

void AIGameSession::NotifyLogout(const APlayerController* PC)
{
	Super::NotifyLogout(PC);
}

bool AIGameSession::KickPlayer(APlayerController* KickedPlayer, const FText& KickReason)
{
	if (!IsValid(KickedPlayer)) return false;

	// Log Player Being Kicked and Why
	FString PlayerName;
	FString PlayerId;

	AIPlayerState* IPlayerState = KickedPlayer->GetPlayerState<AIPlayerState>();
	if (IsValid(IPlayerState))
	{
		PlayerName = IPlayerState->GetPlayerName();
		PlayerId = IPlayerState->UniqueID();
	}
	else {
		PlayerName = TEXT("Unknown");
		PlayerId = TEXT("000-000-000");
	}

	UE_LOG(TitansLog, Log, TEXT("IGameSession: Kicking Player %s (%s) Reason: %s"), *PlayerName, *PlayerId, *KickReason.ToString());

	UNetConnection* NetworkConnection = Cast<UNetConnection>(KickedPlayer->Player);
	if (!NetworkConnection)
	{
		// If net connection doesn't exist we will struggle to do rpcs at this point to disconnect the player
		// flag them to be kicked

		AIPlayerController* IPlayerController = Cast<AIPlayerController>(KickedPlayer);
		if (IPlayerController && IPlayerController->IsValidLowLevel())
		{
			IPlayerController->PendingKickText = KickReason;
		}
		else
		{
			return false;
		}
	}

	// Test: Custom Game Specific Control Messages
	// UNetConnection* NetConnection = Cast<UNetConnection>(KickedPlayer->Player);
	// uint8 MessageType;
	// FString Data;
	// FNetControlMessage<NMT_GameSpecific>::Send(NetConnection, MessageType, Data);

	// Handle Kicked Reason
	KickedPlayer->ClientWasKicked(KickReason);

	// Destroy Player Controller
	KickedPlayer->Destroy();

	return true;
}

bool AIGameSession::BanPlayer(APlayerController* BannedPlayer, const FText& BanReason)
{
	AIPlayerState* PS = Cast<AIPlayerState>(BannedPlayer->PlayerState);
	if (PS)
	{
		UE_LOG(TitansNetwork, Log, TEXT("Adding Ban for user '%s' (uid: %s) Reason '%s'"), *PS->GetPlayerName(), *PS->GetUniquePlayerId(), *BanReason.ToString());

		FPlayerBan Ban;
		Ban.PlayerId = PS->GetAlderonID();
		Ban.BanExpiration = 0;

		AppendBan(Ban);
	}

	KickPlayer(BannedPlayer, BanReason);

	return true;
}

bool AIGameSession::BanId(const FString& AlderonId, const FText& BanReason)
{
	if (IsPlayerBanned(AlderonId)) 
	{
		UE_LOG(TitansNetwork, Log, TEXT("Tried to ban already banned user (uid: %s) Reason '%s'"), *AlderonId, *BanReason.ToString());
		return false;
	}
	UE_LOG(TitansNetwork, Log, TEXT("Adding Ban for user (uid: %s) Reason '%s'"), *AlderonId, *BanReason.ToString());

	FPlayerBan Ban;
	Ban.PlayerId = AlderonId;
	Ban.BanExpiration = 0;
	AppendBan(Ban);

	return true;
}

bool AIGameSession::UnbanId(const FString& AlderonId)
{
	if (!IsPlayerBanned(AlderonId)) return false;

	FPlayerBan Ban;
	Ban.PlayerId = FAlderonPlayerID(AlderonId);

	RemoveBan(Ban);

	return true;
}

void AIGameSession::AddAdmin(APlayerController* AdminPlayer)
{
	if (!AdminPlayer)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameSession::AddAdmin() AdminPlayer is nullptr"));
		return;
	}

	AIPlayerState* const IPlayerState = Cast<AIPlayerState>(AdminPlayer->PlayerState);
	if (!IPlayerState)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameSession::AddAdmin() Invalid Player State"));
		return;
	}

	const FString AlderonID = IPlayerState->GetAlderonID().ToDisplayString();

	// Don't Add Player if he is already a server admin in the config
	if (!ServerAdmins.Contains(AlderonID))
	{
		InstanceServerAdmins.AddUnique(AlderonID);
	}

	IPlayerState->SetIsServerAdmin(true, IPlayerState->GetPlayerRole().bStealthServerAdmin);
}

void AIGameSession::AddGameDev(const APlayerController* const DevPlayer)
{
	if (!DevPlayer)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameSession::AddGameDev() DevPlayer is nullptr"));
		return;
	}

	AIPlayerState* const IPlayerState = Cast<AIPlayerState>(DevPlayer->PlayerState);
	if (!IPlayerState)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameSession::AddGameDev() Invalid Player State"));
		return;
	}

	const FString AlderonID = IPlayerState->GetAlderonID().ToDisplayString();

	InstanceGameDevs.AddUnique(AlderonID);

	IPlayerState->SetIsGameDev(true, IPlayerState->GetPlayerRole().bStealthServerAdmin);
}

void AIGameSession::RemoveAdmin(APlayerController* AdminPlayer)
{
	if (AdminPlayer)
	{
		// Fetch Player State Details
		AIPlayerState* PS = Cast<AIPlayerState>(AdminPlayer->PlayerState);
		if (PS)
		{
			FString AlderonID = PS->GetAlderonID().ToDisplayString();

			// Remove Player From Both Admin Lists
			ServerAdmins.Remove(AlderonID);
			InstanceServerAdmins.Remove(AlderonID);
			
			// This doesn't happen that often, so we can shrink the arrays to save memory
			ServerAdmins.Shrink();
			InstanceServerAdmins.Shrink();

			PS->SetIsServerAdmin(false, false);
		}
	}
}

bool AIGameSession::IsAdmin(const APlayerController* AdminPlayer) const
{
	if (!AdminPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::IsAdmin - !AdminPlayer"));
		return false;
	}

	const AIPlayerState* const IPlayerState = Cast<AIPlayerState>(AdminPlayer->PlayerState);
	if (!IPlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::IsAdmin - !IPlayerState"));
		return false;
	}

	return IsAdminID(IPlayerState->GetAlderonID().ToDisplayString());
}

bool AIGameSession::IsDev(APlayerController* DevPlayer)
{
	if (!DevPlayer)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::IsDev - !DevPlayer"));
		return false;
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(DevPlayer->PlayerState);
	if (!IPlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::IsDev - !IPlayerState"));
		return false;
	}

	return IsDevID(IPlayerState->GetAlderonID().ToDisplayString());
}

bool AIGameSession::IsAdminID(const FString& UniqueID) const
{
	return (ServerAdmins.Contains(UniqueID) || InstanceServerAdmins.Contains(UniqueID));
}

bool AIGameSession::IsDevID(const FString& UniqueID)
{
	return InstanceGameDevs.Contains(UniqueID);
}

bool AIGameSession::IsServer(APlayerController* Player)
{
	if (GetNetMode() == ENetMode::NM_ListenServer)
	{
		return (Player == UGameplayStatics::GetPlayerController(GetWorld(), 0));
	}

	return false;
}

bool AIGameSession::AllowAdminRequest(APlayerController* Player)
{
	if (!Player)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::AllowAdminRequest - !Player"));
		return false;
	}

#if WITH_EDITOR
	UE_LOG(TitansNetwork, Log, TEXT("AIGameSession::AllowAdminRequest"));
	return true;
#endif

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(Player->PlayerState);
	if (!IPlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::AllowAdminRequest - !IPlayerState"));
		return false;
	}

	// Don't allow admin requests from players who are not validated
	if (!IPlayerState->IsPlayerValidated())
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameSession::AllowAdminRequest - !IPlayerState->bPlayerValidated"));
		return false;
	}

	return IsDev(Player) || IsAdmin(Player);
}


/**
 * Delegate fired when a session start request has completed
 *
 * @param SessionName the name of the session this callback is for
 * @param bWasSuccessful true if the async action completed without error, false if there was an error
 */
void AIGameSession::OnStartOnlineGameComplete(FName InSessionName, bool bWasSuccessful)
{
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnStartSessionCompleteDelegate_Handle(OnStartSessionCompleteDelegateHandle);
		}
	}

	if (bWasSuccessful)
	{
		// tell non-local players to start online game
		//for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		//{
		//	AShooterPlayerController* PC = Cast<AShooterPlayerController>(*It);
		//	if (PC && !PC->IsLocalPlayerController())
		//	{
		//		PC->ClientStartOnlineGame();
		//	}
		//}
	}
}

/** Handle starting the match */
void AIGameSession::HandleMatchHasStarted()
{
	// start online game locally and wait for completion
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && (Sessions->GetNamedSession(NAME_GameSession) != nullptr))
		{
			UE_LOG(LogOnlineGame, Log, TEXT("Starting session %s on server"), *FName(NAME_GameSession).ToString());
			OnStartSessionCompleteDelegateHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(OnStartSessionCompleteDelegate);
			Sessions->StartSession(NAME_GameSession);
		}
	}
}

/**
 * Ends a game session
 *
 */
void AIGameSession::HandleMatchHasEnded()
{
	// end online game locally 
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && (Sessions->GetNamedSession(NAME_GameSession) != nullptr))
		{
			// tell the clients to end
			//for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			//{
			//	AIPlayerController* PC = Cast<AIPlayerController>(*It);
			//	if (PC && !PC->IsLocalPlayerController())
			//	{
			//		PC->ClientEndOnlineGame();
			//	}
			//}

			// server is handled here
			UE_LOG(LogOnlineGame, Log, TEXT("Ending session %s on server"), *FName(NAME_GameSession).ToString());
			Sessions->EndSession(NAME_GameSession);
		}
	}
}


EOnlineAsyncTaskState::Type AIGameSession::GetSearchResultStatus(int32& SearchResultIdx, int32& NumSearchResults)
{
	SearchResultIdx = 0;
	NumSearchResults = 0;

	if (SearchSettings.IsValid())
	{
		if (SearchSettings->SearchState == EOnlineAsyncTaskState::Done)
		{
			SearchResultIdx = CurrentSessionParams.BestSessionIdx;
			NumSearchResults = SearchSettings->SearchResults.Num();
		}
		return SearchSettings->SearchState;
	}

	return EOnlineAsyncTaskState::NotStarted;
}

/**
 * Get the search results.
 *
 * @return Search results
 */
const TArray<FOnlineSessionSearchResult> & AIGameSession::GetSearchResults() const
{
	return SearchSettings->SearchResults;
};

const TArray<FAttributeCapData>& AIGameSession::GetAttributeCapsConfig() const
{
	return AttributeCapsConfig;
}

/**
 * Delegate fired when a session create request has completed
 *
 * @param SessionName the name of the session this callback is for
 * @param bWasSuccessful true if the async action completed without error, false if there was an error
 */
void AIGameSession::OnCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogOnlineGame, Verbose, TEXT("OnCreateSessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);

	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandle);
	}

	OnCreatePresenceSessionComplete().Broadcast(InSessionName, bWasSuccessful);
}

/**
 * Delegate fired when a destroying an online session has completed
 *
 * @param SessionName the name of the session this callback is for
 * @param bWasSuccessful true if the async action completed without error, false if there was an error
 */
void AIGameSession::OnDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogOnlineGame, Verbose, TEXT("OnDestroySessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);

	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegateHandle);
		HostSettings = NULL;
	}
}

bool AIGameSession::HostSession(TSharedPtr<const FUniqueNetId> UserId, FName InSessionName, const FString& GameType, const FString& MapName, bool bIsLAN, bool bIsPresence, int32 MaxNumPlayers, FString ServerKey)
{
	IOnlineSubsystem* const OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		CurrentSessionParams.SessionName = InSessionName;
		CurrentSessionParams.bIsLAN = bIsLAN;
		CurrentSessionParams.bIsPresence = bIsPresence;
		CurrentSessionParams.UserId = UserId;
		MaxPlayers = MaxNumPlayers;

		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && CurrentSessionParams.UserId.IsValid())
		{
			HostSettings = MakeShareable(new FIOnlineSessionSettings(bIsLAN, bIsPresence, MaxPlayers));
			if (!ServerKey.IsEmpty())
			{
				HostSettings->Set(FName(TEXT("ServerKey")), ServerKey, EOnlineDataAdvertisementType::ViaOnlineService);
			}
			HostSettings->Set(SETTING_GAMEMODE, GameType, EOnlineDataAdvertisementType::ViaOnlineService);
			HostSettings->Set(SETTING_MAPNAME, MapName, EOnlineDataAdvertisementType::ViaOnlineService);
			HostSettings->Set(SETTING_MATCHING_HOPPER, FString(TEXT("TeamDeathmatch")), EOnlineDataAdvertisementType::DontAdvertise);
			HostSettings->Set(SETTING_MATCHING_TIMEOUT, 120.0f, EOnlineDataAdvertisementType::ViaOnlineService);
			HostSettings->Set(SETTING_SESSION_TEMPLATE_NAME, FString(TEXT("GameSession")), EOnlineDataAdvertisementType::DontAdvertise);

#if !PLATFORM_SWITCH
			// On Switch, we don't have room for this in the session data (and it's not used anyway when searching), so there's no need to add it.
			// Can be readded if the buffer size increases.
			HostSettings->Set(SEARCH_KEYWORDS, CustomMatchKeyword, EOnlineDataAdvertisementType::ViaOnlineService);
#endif

			OnCreateSessionCompleteDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);
			return Sessions->CreateSession(*CurrentSessionParams.UserId, CurrentSessionParams.SessionName, *HostSettings);
		}
		else
		{
			OnCreateSessionComplete(InSessionName, false);
		}
	}
#if !UE_BUILD_SHIPPING
	else
	{
		// Hack workflow in development
		OnCreatePresenceSessionComplete().Broadcast(NAME_GameSession, true);
		return true;
	}
#endif

	return false;
}

bool AIGameSession::HostSession(const TSharedPtr<const FUniqueNetId> UserId, const FName InSessionName, const FOnlineSessionSettings& SessionSettings)
{
	bool bResult = false;

	IOnlineSubsystem* const OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		CurrentSessionParams.SessionName = InSessionName;
		CurrentSessionParams.bIsLAN = SessionSettings.bIsLANMatch;
		CurrentSessionParams.bIsPresence = SessionSettings.bUsesPresence;
		CurrentSessionParams.UserId = UserId;
		MaxPlayers = SessionSettings.NumPrivateConnections + SessionSettings.NumPublicConnections;

		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && CurrentSessionParams.UserId.IsValid())
		{
			OnCreateSessionCompleteDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);
			bResult = Sessions->CreateSession(*UserId, InSessionName, SessionSettings);
		}
		else
		{
			OnCreateSessionComplete(InSessionName, false);
		}
	}

	return bResult;
}

void AIGameSession::OnFindSessionsComplete(bool bWasSuccessful)
{
	UE_LOG(LogOnlineGame, Verbose, TEXT("OnFindSessionsComplete bSuccess: %d"), bWasSuccessful);

	IOnlineSubsystem* const OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(OnFindSessionsCompleteDelegateHandle);

			UE_LOG(LogOnlineGame, Verbose, TEXT("Num Search Results: %d"), SearchSettings->SearchResults.Num());
			for (int32 SearchIdx = 0; SearchIdx < SearchSettings->SearchResults.Num(); SearchIdx++)
			{
				const FOnlineSessionSearchResult& SearchResult = SearchSettings->SearchResults[SearchIdx];
				DumpSession(&SearchResult.Session);
			}

			OnFindSessionsComplete().Broadcast(bWasSuccessful);
		}
	}
}

void AIGameSession::FindSessions(TSharedPtr<const FUniqueNetId> UserId, FName InSessionName, bool bIsLAN, bool bIsPresence, FString OverrideKeyword)
{
	const auto OnlineSub = IOnlineSubsystem::GetByPlatform();
	if (OnlineSub)
	{
		CurrentSessionParams.SessionName = InSessionName;
		CurrentSessionParams.bIsLAN = bIsLAN;
		CurrentSessionParams.bIsPresence = bIsPresence;
		CurrentSessionParams.UserId = UserId;

		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && CurrentSessionParams.UserId.IsValid())
		{
			SearchSettings = MakeShareable(new FIOnlineSearchSettings(bIsLAN, bIsPresence));
			SearchSettings->QuerySettings.Set(SEARCH_KEYWORDS, (OverrideKeyword.IsEmpty()) ? CustomMatchKeyword : OverrideKeyword, EOnlineComparisonOp::Equals);

			//SearchSettings->QuerySettings.Set(SETTING_GAMEMODE, "");
			//SearchSettings->QuerySettings.Set(SEARCH_USER, "");
			//SearchSettings->QuerySettings.Set(SETTING_MAX_RESULT, 100);
			//SearchSettings->QuerySettings.Get(SETTING_CONTRACT_VERSION_FILTER, ContractVersionFilter);
			//SearchSettings->QuerySettings.Get(SETTING_FIND_PRIVATE_SESSIONS, IncludePrivateSessions);
			//SearchSettings->QuerySettings.Get(SETTING_FIND_RESERVED_SESSIONS, IncludeReservations);
			//SearchSettings->QuerySettings.Get(SETTING_FIND_INACTIVE_SESSIONS, IncludeInactiveSessions);
			//SearchSettings->QuerySettings.Get(SETTING_MULTIPLAYER_VISIBILITY, MultiplayerVisibility);

			TSharedRef<FOnlineSessionSearch> SearchSettingsRef = SearchSettings.ToSharedRef();

			OnFindSessionsCompleteDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(OnFindSessionsCompleteDelegate);
			Sessions->FindSessions(*CurrentSessionParams.UserId, SearchSettingsRef);
		}
	}
	else
	{
		OnFindSessionsComplete(false);
	}
}

bool AIGameSession::JoinSession(TSharedPtr<const FUniqueNetId> UserId, FName InSessionName, int32 SessionIndexInSearchResults)
{
	bool bResult = false;

	if (SessionIndexInSearchResults >= 0 && SessionIndexInSearchResults < SearchSettings->SearchResults.Num())
	{
		bResult = JoinSession(UserId, InSessionName, SearchSettings->SearchResults[SessionIndexInSearchResults]);
	}

	return bResult;
}

bool AIGameSession::JoinSession(TSharedPtr<const FUniqueNetId> UserId, FName InSessionName, const FOnlineSessionSearchResult& SearchResult)
{
	bool bResult = false;

	const auto OnlineSub = IOnlineSubsystem::GetByPlatform();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && UserId.IsValid())
		{
			OnJoinSessionCompleteDelegateHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(OnJoinSessionCompleteDelegate);
			bResult = Sessions->JoinSession(*UserId, InSessionName, SearchResult);
		}
	}

	return bResult;
}

/**
 * Delegate fired when the joining process for an online session has completed
 *
 * @param SessionName the name of the session this callback is for
 * @param bWasSuccessful true if the async action completed without error, false if there was an error
 */
void AIGameSession::OnJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	bool bWillTravel = false;

	UE_LOG(LogOnlineGame, Verbose, TEXT("OnJoinSessionComplete %s bSuccess: %d"), *InSessionName.ToString(), static_cast<int32>(Result));

	const auto OnlineSub = IOnlineSubsystem::GetByPlatform();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnJoinSessionCompleteDelegate_Handle(OnJoinSessionCompleteDelegateHandle);
		}
	}

	OnJoinSessionComplete().Broadcast(Result);
}

bool AIGameSession::TravelToSession(int32 ControllerId, FName InSessionName)
{
	const auto OnlineSub = IOnlineSubsystem::GetByPlatform();
	if (OnlineSub)
	{
		FString URL;
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid() && Sessions->GetResolvedConnectString(InSessionName, URL))
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), ControllerId);
			if (PC)
			{
				PC->ClientTravel(URL, TRAVEL_Absolute);
				return true;
			}
		}
		else
		{
			UE_LOG(LogOnlineGame, Warning, TEXT("Failed to join session %s"), *SessionName.ToString());
		}
	}
#if !UE_BUILD_SHIPPING
	else
	{
		APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), ControllerId);
		if (PC)
		{
			FString LocalURL(TEXT("127.0.0.1"));
			PC->ClientTravel(LocalURL, TRAVEL_Absolute);
			return true;
		}
	}
#endif //!UE_BUILD_SHIPPING

	return false;
}

/*
void AIGameSession::RegisterServer()
{
	const auto OnlineSub = IOnlineSubsystem::GetByPlatform();
	if (OnlineSub)
	{
		IOnlineSessionPtr SessionInt = OnlineSub->GetSessionInterface();
		if (SessionInt.IsValid())
		{
			TSharedPtr<class FIOnlineSessionSettings> ShooterHostSettings = MakeShareable(new FIOnlineSessionSettings(false, false, 16));
			ShooterHostSettings->Set(SETTING_MATCHING_HOPPER, FString("TeamDeathmatch"), EOnlineDataAdvertisementType::DontAdvertise);
			ShooterHostSettings->Set(SETTING_MATCHING_TIMEOUT, 120.0f, EOnlineDataAdvertisementType::ViaOnlineService);
			ShooterHostSettings->Set(SETTING_SESSION_TEMPLATE_NAME, FString("GameSession"), EOnlineDataAdvertisementType::DontAdvertise);
			ShooterHostSettings->Set(SETTING_GAMEMODE, FString("TeamDeathmatch"), EOnlineDataAdvertisementType::ViaOnlineService);
			ShooterHostSettings->Set(SETTING_MAPNAME, GetWorld()->GetMapName(), EOnlineDataAdvertisementType::ViaOnlineService);
			ShooterHostSettings->bAllowInvites = true;
			ShooterHostSettings->bIsDedicated = true;
			if (FParse::Param(FCommandLine::Get(), TEXT("forcelan")))
			{
				UE_LOG(LogOnlineGame, Log, TEXT("Registering server as a LAN server"));
				ShooterHostSettings->bIsLANMatch = true;
			}
			HostSettings = ShooterHostSettings;
			OnCreateSessionCompleteDelegateHandle = SessionInt->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);
			SessionInt->CreateSession(0, NAME_GameSession, *HostSettings);
		}
	}
}
*/

/*

void AIGameSession::OnCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogOnlineGame, Verbose, TEXT("OnCreateSessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);

	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegateHandle);
	}

	OnCreatePresenceSessionComplete().Broadcast(InSessionName, bWasSuccessful);
}

void AIGameSession::OnDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogOnlineGame, Verbose, TEXT("OnDestroySessionComplete %s bSuccess: %d"), *InSessionName.ToString(), bWasSuccessful);

	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(OnDestroySessionCompleteDelegateHandle);
		//HostSettings = NULL;
	}
}

bool AIGameSession::HostSession(const TSharedPtr<const FUniqueNetId> UserId, const FName InSessionName, const FOnlineSessionSettings& SessionSettings)
{
	IOnlineSubsystem* const OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		//CurrentSessionParams.SessionName = InSessionName;
		//CurrentSessionParams.bIsLAN = bIsLAN;
		//CurrentSessionParams.bIsPresence = bIsPresence;
		//CurrentSessionParams.UserId = UserId;
		//MaxPlayers = MaxNumPlayers;

		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())// && CurrentSessionParams.UserId.IsValid())
		{
			//HostSettings = MakeShareable(new FIOnlineSessionSettings(bIsLAN, bIsPresence, MaxPlayers));
			//HostSettings->Set(SETTING_GAMEMODE, GameType, EOnlineDataAdvertisementType::ViaOnlineService);
			//HostSettings->Set(SETTING_MAPNAME, MapName, EOnlineDataAdvertisementType::ViaOnlineService);
			//HostSettings->Set(SETTING_MATCHING_HOPPER, FString("TeamDeathmatch"), EOnlineDataAdvertisementType::DontAdvertise);
			//HostSettings->Set(SETTING_MATCHING_TIMEOUT, 120.0f, EOnlineDataAdvertisementType::ViaOnlineService);
			//HostSettings->Set(SETTING_SESSION_TEMPLATE_NAME, FString("GameSession"), EOnlineDataAdvertisementType::DontAdvertise);

#if !PLATFORM_SWITCH
			// On Switch, we don't have room for this in the session data (and it's not used anyway when searching), so there's no need to add it.
			// Can be readded if the buffer size increases.
			//HostSettings->Set(SEARCH_KEYWORDS, CustomMatchKeyword, EOnlineDataAdvertisementType::ViaOnlineService);
#endif

			OnCreateSessionCompleteDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(OnCreateSessionCompleteDelegate);
			return Sessions->CreateSession(0, NAME_GameSession, SessionSettings);
		}
		else
		{
			OnCreateSessionComplete(InSessionName, false);
		}
	}
#if !UE_BUILD_SHIPPING
	else
	{
		// Hack workflow in development
		OnCreatePresenceSessionComplete().Broadcast(NAME_GameSession, true);
		return true;
	}
#endif

	return false;
}

void AIGameSession::OnStartOnlineGameComplete(FName InSessionName, bool bWasSuccessful)
{
	IOnlineSubsystem* OnlineSub = Online::GetSubsystem(GetWorld());
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->ClearOnStartSessionCompleteDelegate_Handle(OnStartSessionCompleteDelegateHandle);
		}
	}

	if (bWasSuccessful)
	{
		// tell non-local players to start online game
		//for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		//{
		//	AShooterPlayerController* PC = Cast<AShooterPlayerController>(*It);
		//	if (PC && !PC->IsLocalPlayerController())
		//	{
		//		PC->ClientStartOnlineGame();
		//	}
		//}
	}
}

*/

UWebhook* UWebhook::CreateWebhook(const FString& Key)
{
	UWebhook* Webhook = NewObject<UWebhook>();
	Webhook->WebhookKey = Key;
	return Webhook;
}

void UWebhook::AddPropertyString(const FString& Name, const FString& Property)
{
	Properties.Add(Name, MakeShareable(new FJsonValueString(Property)));
}

void UWebhook::AddPropertyInt(const FString& Name, int Property)
{
	Properties.Add(Name, MakeShareable(new FJsonValueNumber(Property)));
}

void UWebhook::AddPropertyFloat(const FString& Name, float Property)
{
	Properties.Add(Name, MakeShareable(new FJsonValueNumber(Property)));
}

void UWebhook::AddPropertyBool(const FString& Name, bool Property)
{
	Properties.Add(Name, MakeShareable(new FJsonValueBoolean(Property)));
}

void UWebhook::Send(const UObject* WorldContextObject)
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(WorldContextObject);
	check(IGameInstance);
	if (!IGameInstance) return;

	AIGameSession* IGameSession = Cast<AIGameSession>(IGameInstance->GetGameSession());
	check (IGameSession);
	if (!IGameSession) return;

	check(IGameSession->HasAuthority());
	if (!IGameSession->HasAuthority()) return;

	if (!IGameSession->UseWebHooks()) return;

	IGameSession->TriggerWebHook(this);
}
