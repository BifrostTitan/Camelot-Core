#include "DaocFrontendTheme.h"
#include "Engine/Texture2D.h"
UTexture2D *UDaocFrontendTheme::Image(FName Name) const
{
    if (const auto *Value = Images.Find(Name))
        if (auto *Texture = Value->LoadSynchronous())
            return Texture;
    if (auto *Parent = Fallback.LoadSynchronous(); Parent && Parent != this)
    {
        if (const auto *Value = Parent->Images.Find(Name))
            return Value->LoadSynchronous();
    }
    return nullptr;
}
