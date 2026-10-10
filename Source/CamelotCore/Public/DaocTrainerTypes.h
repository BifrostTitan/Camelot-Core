#pragma once
#include "CoreMinimal.h"
#include "DaocTrainerTypes.generated.h"

/// One server training choice with its rank, next cost and conservative allocation limit.
USTRUCT(BlueprintType)
struct FDaocTrainerLine
{
    GENERATED_BODY()
    /// Server line index within its training category; protocol encoding is handled by the session.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 Index = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 Rank = 0;
    /// Server-reported cost of the next rank; later ranks may have different costs.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 NextCost = 0;
    /// Conservative client allocation limit derived from available training data.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 MaximumRank = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    bool bAvailable = false;
};

/// Revisioned trainer choices, point balances and pending allocation state.
USTRUCT(BlueprintType)
struct FDaocTrainerState
{
    GENERATED_BODY()
    /// Snapshot identity used to reject requests from stale trainer views.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int64 Revision = 0;
    /// Server object ID of the trainer associated with this interaction.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 ObjectId = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    FString Name;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    bool bOpen = false;
    /// An allocation is awaiting server acknowledgement; further requests are blocked.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    bool bPending = false;
    /// A specialization list has been received, including a confirmed empty list.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    bool bSpecsKnown = false;
    /// A realm-ability list has been received, including a confirmed empty list.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    bool bRealmKnown = false;
    /// Unspent specialization points reported by the server.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 SpecializationPoints = 0;
    /// Unspent realm-ability training points, rather than lifetime realm points earned.
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    int32 RealmPoints = 0;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    TArray<FDaocTrainerLine> Specializations;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    TArray<FDaocTrainerLine> RealmAbilities;
    UPROPERTY(BlueprintReadOnly, Category = "DAoC|Training")
    FString Feedback;
};
