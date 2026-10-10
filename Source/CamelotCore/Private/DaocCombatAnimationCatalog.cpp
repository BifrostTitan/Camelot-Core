#include "DaocCombatAnimationCatalog.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundBase.h"
void UDaocCombatAnimationCatalog::SetClip(int32 Model, const FString &Action, const FString &Path)
{
    Models.FindOrAdd(Model).Actions.Add(Action, TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Path)));
}
void UDaocCombatAnimationCatalog::SetSound(int32 Id, const FString &Path, float Min, float Max)
{
    FDaocCombatSound S;
    S.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path));
    S.MinDistance = Min;
    S.MaxDistance = Max;
    Sounds.Add(Id, S);
}
void UDaocCombatAnimationCatalog::SetSpell(int32 Id, int32 Start, int32 Loop, int32 Finish, int32 Gather, int32 Cast, int32 Hit, int32 Miss)
{
    FDaocSpellPresentation S;
    S.Start = Start;
    S.Loop = Loop;
    S.Finish = Finish;
    S.GatherSound = Gather;
    S.CastSound = Cast;
    S.HitSound = Hit;
    S.MissSound = Miss;
    Spells.Add(Id, S);
}
