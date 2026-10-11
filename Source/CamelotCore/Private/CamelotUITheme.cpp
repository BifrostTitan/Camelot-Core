#include "CamelotUITheme.h"
#include "Engine/Texture2D.h"

UTexture2D *UCamelotUITheme::LoadAbilityIcon(const FDaocCombatAbility &Ability) const
{
    const auto &TypedIcons = Ability.Kind == EDaocAbilityKind::Style ? StyleIcons : (Ability.Kind == EDaocAbilityKind::Ability || Ability.Kind == EDaocAbilityKind::RealmAbility || Ability.Kind == EDaocAbilityKind::Specialization ? SkillIcons : AbilityIcons);
    if (const auto *Asset = TypedIcons.Find(Ability.Icon))
        if (auto *Texture = Asset->LoadSynchronous())
            return Texture;
    // Preserve themes that already provide every namespace through AbilityIcons.
    if (&TypedIcons != &AbilityIcons)
        if (const auto *Asset = AbilityIcons.Find(Ability.Icon))
            if (auto *Texture = Asset->LoadSynchronous())
                return Texture;
    return MissingAbilityIcon.LoadSynchronous();
}
