// Copyright (C) 2018 - Dennis "MazyModz" Andersson.

/*

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 (the "Software"), to deal in the Software without restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 persons to whom the Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "DAWeapon.h"
#include "UnrealNetwork.h"
#include "DACharacter.h"
#include "DAProjectile.h"

// Sets default values
ADAWeapon::ADAWeapon()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	bReplicateMovement = true;

	SetActorEnableCollision(false);
}

void ADAWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADAWeapon, OwnerPawn);
}

// Called when the game starts or when spawned
void ADAWeapon::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void ADAWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FVector ADAWeapon::GetMuzzleLocation() const
{
	USkeletalMeshComponent* MeshComp = GetSkeletalMeshComponent();
	if (MeshComp != NULL)
	{
		return MeshComp->GetSocketLocation(TEXT("Muzzle"));
	}
	else
	{
		return GetActorLocation();
	}
}

FVector ADAWeapon::GetAimLocation() const
{
	if (OwnerPawn != NULL)
	{
		FVector Location;
		FRotator Rotation;
		OwnerPawn->GetActorEyesViewPoint(Location, Rotation);

		FHitResult OutHit(ForceInit);
		FVector End = Rotation.Vector() * 10000000;
		GetWorld()->LineTraceSingleByChannel(OutHit, Location, End, ECC_Visibility);

		return OutHit.Location;
	}
	else
	{
		return FVector::ZeroVector;
	}
}

void ADAWeapon::FireWeapon()
{
	if (OwnerPawn != NULL && OwnerPawn->IsLocallyControlled() == true)
	{
		ServerFireWeapon(GetMuzzleLocation());
	}
}

void ADAWeapon::ServerFireWeapon(const FVector& MuzzleLocation)
{
	if ((ProjectileClass != NULL) && (OwnerPawn != NULL))
	{
		if (HasAuthority() == true)
		{
			FRotator Direction = (GetAimLocation() - MuzzleLocation).Rotation();
			GetWorld()->SpawnActor<ADAProjectile>(ProjectileClass, FTransform(Direction, MuzzleLocation, FVector(0.25f, 0.25f, 0.25f)));
		}
		else
		{
			OwnerPawn->ServerFireWeapon(MuzzleLocation);
		}
	}
}

