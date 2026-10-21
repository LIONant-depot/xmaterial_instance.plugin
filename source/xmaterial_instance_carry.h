#ifndef XMATERIAL_INSTANCE_CARRY_H
#define XMATERIAL_INSTANCE_CARRY_H
#pragma once

// Changing the material (or the parent, when it has another material) of an instance: what the person set comes along (documentation/material_contract.md section 6). An
// override is for something (a parameter id): when the new material declares that id the value stays where it is. An id the new material does not have may still be the same
// thing under another id (a material rebuilt from scratch): it goes to the parameter of the same name and type that nothing else took. What matches nothing is kept as an
// orphan, never dropped. What the person never touched is not here at all (overrides are sparse): the new material's own default applies.
//
// Standalone (no resources, no editor), so a test can run it; SetMaterial and SetParent (Editor/xmaterial_instance_editor.h) use it.
#include <cstdint>
#include <string>
#include <vector>

namespace xmaterial_instance
{
    struct carry_param
    {
        std::uint64_t   m_ID    = 0;
        std::string     m_Name;                 // an override: its NameHint; a declaration: its name
        int             m_Type  = 0;
    };

    // For each override of the old instance (Old, in order) the id it has under the new material: its own when New has it, else the id of the parameter of New with the same
    // (non empty) name and type that no other override took, first come first served, else 0 (an orphan, unchanged). An id is taken once.
    inline std::vector<std::uint64_t> MatchCarriedOverrides(const std::vector<carry_param>& Old, const std::vector<carry_param>& New)
    {
        std::vector<std::uint64_t> Match(Old.size(), 0);
        std::vector<bool>          Taken(New.size(), false);
        for (std::size_t i = 0; i < Old.size(); ++i)
            for (std::size_t j = 0; j < New.size(); ++j)
                if (!Taken[j] && New[j].m_ID == Old[i].m_ID) { Match[i] = Old[i].m_ID; Taken[j] = true; break; }

        for (std::size_t i = 0; i < Old.size(); ++i)
        {
            if (Match[i] || Old[i].m_Name.empty()) continue;
            for (std::size_t j = 0; j < New.size(); ++j)
                if (!Taken[j] && New[j].m_Name == Old[i].m_Name && New[j].m_Type == Old[i].m_Type) { Match[i] = New[j].m_ID; Taken[j] = true; break; }
        }
        return Match;
    }
}

#endif // XMATERIAL_INSTANCE_CARRY_H
