// Copyright Epic Games, Inc. All Rights Reserved.

#include "SphereCaptuerGameMode.h"
#include "SphereCaptuerCharacter.h"
#include "UObject/ConstructorHelpers.h"

ASphereCaptuerGameMode::ASphereCaptuerGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
