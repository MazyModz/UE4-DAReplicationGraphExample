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

#include "DAProjectile.h"

// Sets default values
ADAProjectile::ADAProjectile()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	ProjMovement = CreateDefaultSubobject <UProjectileMovementComponent>(TEXT("ProjMovement"));
	ProjMovement->bRotationFollowsVelocity = true;
	ProjMovement->bInterpMovement = true;
	ProjMovement->bInterpRotation = true;
	ProjMovement->bShouldBounce = true;
	ProjMovement->MaxSpeed = 8000.0f;
	ProjMovement->InitialSpeed = 15000.0f;
	ProjMovement->SetIsReplicated(true);

	bReplicates = true;
	bReplicateMovement = true;

	SetMobility(EComponentMobility::Movable);
}

// Called when the game starts or when spawned
void ADAProjectile::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void ADAProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ADAProjectile::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	ProjMovement->Velocity = NewVelocity;
}

