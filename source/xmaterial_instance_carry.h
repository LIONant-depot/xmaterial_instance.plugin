#ifndef XMATERIAL_INSTANCE_CARRY_H
#define XMATERIAL_INSTANCE_CARRY_H
#pragma once

// Changing the material of an instance: the textures the person assigned come along. A texture slot is for something (SamplerAlbedo, SamplerNormal, ...): when the new
// material has a slot of the same name, the texture the person put in the old one goes there, so changing the material is not assigning everything again. What stays
// the material's own is not carried: a slot the person never touched takes the default of the new material.
//
// Standalone (no resources, no editor), so a test can run it; SetMaterial (Editor/xmaterial_instance_editor.h) uses it.
#include <string>
#include <vector>

namespace xmaterial_instance
{
    // For each slot of the new material (NewNames, in its order) the slot of the old instance whose texture it takes, or -1. OldAssigned[j]: the person assigned the texture
    // of old slot j (it is not the old material's default). Names are matched exactly; a slot is taken from once (two new slots of one name take two old slots of that name, in
    // order); the first new slot of a name is served first.
    inline std::vector<int> MatchCarriedTextures(const std::vector<std::string>& OldNames, const std::vector<bool>& OldAssigned, const std::vector<std::string>& NewNames)
    {
        std::vector<int>  Match(NewNames.size(), -1);
        std::vector<bool> Used(OldNames.size(), false);
        for (std::size_t i = 0; i < NewNames.size(); ++i)
            for (std::size_t j = 0; j < OldNames.size() && j < OldAssigned.size(); ++j)
            {
                if (Used[j] || !OldAssigned[j] || OldNames[j] != NewNames[i]) continue;
                Match[i] = static_cast<int>(j);
                Used[j]  = true;
                break;
            }
        return Match;
    }
}

#endif // XMATERIAL_INSTANCE_CARRY_H
