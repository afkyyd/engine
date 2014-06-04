// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayAbilityTypes.generated.h"

class UGameplayEffect;
class UAnimInstance;
class UAbilitySystemComponent;


/** 
 *
 *
 */



UENUM(BlueprintType)
namespace EGameplayAbilityInstancingPolicy
{
	/**
	*	How the ability is instanced when executed. This limits what an ability can do in its implementation. For example, a NonInstanced
	*	Ability cannot have state. It is probably unsafe for an InstancedPerActor ability to have latent actions, etc.
	*/

	enum Type
	{
		NonInstanced,			// This ability is never instanced. Anything that executes the ability is operating on the CDO.
		InstancedPerActor,		// Each actor gets their own instance of this ability. State can be saved, replication is possible.
		InstancedPerExecution,	// We instance this ability each time it is executed. Replication possible but not recommended.
	};
}

UENUM(BlueprintType)
namespace EGameplayAbilityNetExecutionPolicy
{
	/**
	*	How does an ability execute on the network. Does a client "ask and predict", "ask and wait", "don't ask"
	*/

	enum Type
	{
		Predictive		UMETA(DisplayName = "Predictive"),	// Part of this ability runs predictively on the client.
		Server			UMETA(DisplayName = "Server"),		// This ability must be OK'd by the server before doing anything on a client.
		Client			UMETA(DisplayName = "Client"),		// This ability runs as long the client says it does.
	};
}

UENUM(BlueprintType)
namespace EGameplayAbilityReplicationPolicy
{
	/**
	*	How an ability replicates state/events to everyone on the network.
	*/

	enum Type
	{
		ReplicateNone		UMETA(DisplayName = "Replicate None"),	// We don't replicate the instance of the ability to anyone.
		ReplicateAll		UMETA(DisplayName = "Replicate All"),	// We replicate the instance of the ability to everyone (even simulating clients).
		ReplicateOwner		UMETA(DisplayName = "Replicate Owner"),	// Only replicate the instance of the ability to the owner.
	};
}

USTRUCT(BlueprintType)
struct FGameplayAbilityHandle
{
	GENERATED_USTRUCT_BODY()

	FGameplayAbilityHandle()
	: Handle(INDEX_NONE)
	{

	}

	FGameplayAbilityHandle(int32 InHandle)
		: Handle(InHandle)
	{

	}

	bool IsValid() const
	{
		return Handle != INDEX_NONE;
	}

	FGameplayAbilityHandle GetNextHandle()
	{
		return FGameplayAbilityHandle(Handle + 1);
	}

	bool operator==(const FGameplayAbilityHandle& Other) const
	{
		return Handle == Other.Handle;
	}

	bool operator!=(const FGameplayAbilityHandle& Other) const
	{
		return Handle != Other.Handle;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("%d"), Handle);
	}

private:

	UPROPERTY()
	int32 Handle;
};

/**
*	Not implemented yet, but we will need something like this to track ability + levels if we choose to support ability leveling in the base classes.
*/

USTRUCT()
struct FGameplayAbilitySpec
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	int32	Level;

	UPROPERTY()
	class UGameplayAbility * Ability;

	UPROPERTY()
	FGameplayAbilityHandle	Handle;
};


UENUM(BlueprintType)
namespace EGameplayAbilityActivationMode
{
	enum Type
	{
		Authority,			// We are the authority activating this ability
		NonAuthority,		// We are not the authority but aren't predicting yet. This is a mostly invalid state to be in.

		Predicting,			// We are predicting the activation of this ability
		Confirmed,			// We are not the authority, but the authority has confirmed this activation
	};
}

/**
*	FGameplayAbilityActorInfo
*
*	Cached data associated with an Actor using an Ability.
*		-Initialized from an AActor* in InitFromActor
*		-Abilities use this to know what to actor upon. E.g., instead of being coupled to a specific actor class.
*		-These are generally passed around as pointers to support polymorphism.
*		-Projects can override UAbilitySystemGlobals::AllocAbilityActorInfo to override the default struct type that is created.
*
*/
USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayAbilityActorInfo
{
	GENERATED_USTRUCT_BODY()

	virtual ~FGameplayAbilityActorInfo() {}

	UPROPERTY(BlueprintReadOnly, Category = "ActorInfo")
	TWeakObjectPtr<AActor>	Actor;

	UPROPERTY(BlueprintReadOnly, Category = "ActorInfo")
	TWeakObjectPtr<UAnimInstance>	AnimInstance;

	UPROPERTY(BlueprintReadOnly, Category = "ActorInfo")
	TWeakObjectPtr<UAbilitySystemComponent>	AbilitySystemComponent;

	UPROPERTY(BlueprintReadOnly, Category = "ActorInfo")
	TWeakObjectPtr<UMovementComponent>	MovementComponent;

	virtual void InitFromActor(AActor *Actor);
};

/**
*	FGameplayAbilityActivationInfo
*
*	Data tied to a specific activation of an ability.
*		-Tell us whether we are the authority, if we are predicting, confirmed, etc.
*		-Holds PredictionKey
*		-Generally not meant to be subclassed in projects.
*		-Passed around by value since the struct is small.
*/

USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayAbilityActivationInfo
{
	GENERATED_USTRUCT_BODY()

	FGameplayAbilityActivationInfo()
	: ActivationMode(EGameplayAbilityActivationMode::Authority),
	PredictionKey(0)
	{

	}

	FGameplayAbilityActivationInfo(AActor * InActor, int32 InPredictionKey)
		: PredictionKey(InPredictionKey)
	{
		// On Init, we are either Authority or NonAuthority. We haven't been given a PredictionKey and we haven't been confirmed.
		// NonAuthority essentially means 'I'm not sure what how I'm going to do this yet'.
		ActivationMode = (InActor->Role == ROLE_Authority ? EGameplayAbilityActivationMode::Authority : EGameplayAbilityActivationMode::NonAuthority);
	}

	FGameplayAbilityActivationInfo(EGameplayAbilityActivationMode::Type InType, int32 InPredictionKey)
		: ActivationMode(InType)
		, PredictionKey(InPredictionKey)
	{
	}


	void GeneratePredictionKey(UAbilitySystemComponent * Component) const;

	void SetPredictionKey(int32 InPredictionKey);

	void SetActivationConfirmed();

	UPROPERTY(BlueprintReadOnly, Category = "ActorInfo")
	mutable TEnumAsByte<EGameplayAbilityActivationMode::Type>	ActivationMode;

	UPROPERTY()
	mutable int32 PredictionKey;
};


// ----------------------------------------------------

/**
*	A generic structure for targeting data. We want generic functions to produce this data and other generic
*	functions to consume this data.
*
*	We expect this to be able to hold specific actors/object reference and also generic location/direction/origin
*	information.
*
*	Some example producers:
*		-Overlap/Hit collision event generates TargetData about who was hit in a melee attack
*		-A mouse input causes a hit trace and the actor infront of the crosshair is turned into TargetData
*		-A mouse input causes TargetData to be generated from the owner's crosshair view origin/direction
*		-An AOE/aura pulses and all actors in a radius around the instigator are added to TargetData
*		-Panzer Dragoon style 'painting' targeting mode
*		-MMORPG style ground AOE targeting style (potentially both a location on the ground and actors that were targeted)
*
*	Some example consumers:
*		-Apply a GameplayEffect to all actors in TargetData
*		-Find closest actor from all in TargetData
*		-Call some function on all actors in TargetData
*		-Filter or merge TargetDatas
*		-Spawn a new actor at a TargetData location
*
*
*
*	Maybe it is better to distinguish between actor list targeting vs positional targeting data?
*		-AOE/aura type of targeting data blurs the line
*
*
*/

struct GAMEPLAYABILITIES_API FGameplayAbilityTargetData
{
	virtual ~FGameplayAbilityTargetData() { }

	void ApplyGameplayEffect(UGameplayEffect *GameplayEffect, const FGameplayAbilityActorInfo InstigatorInfo);

	virtual TArray<AActor*>	GetActors() const = 0;

	virtual bool HasHitResult() const
	{
		return false;
	}

	virtual const FHitResult * GetHitResult() const
	{
		return NULL;
	}

	virtual FString ToString() const;
};

struct GAMEPLAYABILITIES_API FGameplayAbilityTargetData_SingleTargetHit : public FGameplayAbilityTargetData
{
	FGameplayAbilityTargetData_SingleTargetHit(const FHitResult InHitResult)
	: HitResult(InHitResult)
	{ }

	virtual TArray<AActor*>	GetActors() const
	{
		TArray<AActor*>	Actors;
		if (HitResult.Actor.IsValid())
		{
			Actors.Push(HitResult.Actor.Get());
		}
		return Actors;
	}

	virtual bool HasHitResult() const
	{
		return true;
	}

	virtual const FHitResult * GetHitResult() const
	{
		return &HitResult;
	}

	FHitResult	HitResult;
};

/**
*	Handle for Targeting Data. This servers two main purposes:
*		-Avoid us having to copy around the full targeting data structure in Blueprints
*		-Allows us to leverage polymorphism in the target data structure
*		-Allows us to implement NetSerialize and replicate by value between clients/server
*
*		-Avoid using UObjects could be used to give us polymorphism and by reference passing in blueprints.
*		-However we would still be screwed when it came to replication
*
*		-Replication by value
*		-Pass by reference in blueprints
*		-Polymophism in TargetData structure
*
*/

USTRUCT(BlueprintType)
struct GAMEPLAYABILITIES_API FGameplayAbilityTargetDataHandle
{
	GENERATED_USTRUCT_BODY()

	TSharedPtr<FGameplayAbilityTargetData>	Data;
};

template<>
struct TStructOpsTypeTraits<FGameplayAbilityTargetDataHandle> : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithCopy = true		// Necessary so that TSharedPtr<FGameplayAbilityTargetData> Data is copied around
	};
};
