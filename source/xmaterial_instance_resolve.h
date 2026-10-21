#ifndef XMATERIAL_INSTANCE_RESOLVE_H
#define XMATERIAL_INSTANCE_RESOLVE_H
#pragma once

// Resolving an instance (documentation/material_contract.md section 5): walk the parent chain, apply the overrides on top of the manifest defaults, clamp, report. Run by the
// instance compiler at asset build, never at runtime. Standalone (no resources, no editor), so a test can run it: the descriptor (xmaterial_intance_descriptor.h) fills a
// resolve_node per instance, keyed by whatever it likes (the resource guid), and Resolve() does the rest. Textures follow the same rules but are not stored here: a resolved
// parameter says which instance of the chain set it (m_Source) and the compiler reads the texture from that one.
#include "plugins/xmaterial.plugin/source/xmaterial_params.h"

namespace xmaterial_instance
{
    struct override_value                               // what the resolver reads of an override_entry
    {
        xmaterial::param_id     m_ID    = 0;
        xmaterial::param_type   m_Type  = xmaterial::param_type::FLOAT;
        xmaterial::float4v      m_Value;
    };

    struct resolve_node                                 // what the resolver reads of an instance
    {
        std::uint64_t               m_Key       = 0;    // identity of this instance, never 0
        std::uint64_t               m_Parent    = 0;    // 0 = the parent is the material
        std::uint64_t               m_Root      = 0;    // the root material it declares
        std::vector<override_value> m_Overrides;        // sorted by m_ID
    };

    enum class diag_code : std::uint8_t { W_ORPHAN_ID, W_ORPHAN_TYPE, W_CLAMPED, E_CHAIN_CYCLE, E_CHAIN_DEPTH, E_PARENT_ROOT };
    struct diag { diag_code m_Code; xmaterial::param_id m_ID = 0; };     // m_ID: the parameter, 0 for the chain errors

    struct resolved_param
    {
        xmaterial::param_id     m_ID;
        xmaterial::param_type   m_Type;
        xmaterial::float4v      m_Value;
        int                     m_Source = -1;          // index in the chain of the instance that set it; -1 = the manifest default
    };

    struct resolved
    {
        std::vector<std::uint64_t>  m_Chain;            // keys, this instance first
        std::vector<resolved_param> m_Params;           // one per manifest declaration, in declaration order
        std::vector<diag>           m_Diags;
        bool Failed() const { for (auto& D : m_Diags) if (D.m_Code >= diag_code::E_CHAIN_CYCLE) return true; return false; }
    };

    inline constexpr int max_chain_depth = 8;

    // Lookup(key) -> const resolve_node* (nullptr = unknown, ends the chain like a material would). A chain longer than 8 instances is E_CHAIN_DEPTH, a repeated key
    // E_CHAIN_CYCLE, an ancestor declaring another root than Start's E_PARENT_ROOT. On any of them the walk stops and Resolve() gives up with no params.
    // Every declaration of the manifest resolves (used or not); an override whose ID is in no declaration is an orphan.
    template<typename LOOKUP>
    resolved Resolve(const xmaterial::manifest& M, std::uint64_t Start, LOOKUP&& Lookup)
    {
        resolved R;
        std::vector<const resolve_node*> Nodes;
        for (std::uint64_t Key = Start; Key; )
        {
            const resolve_node* p = Lookup(Key);
            if (!p) break;
            if (std::find(R.m_Chain.begin(), R.m_Chain.end(), Key) != R.m_Chain.end()) { R.m_Diags.push_back({ diag_code::E_CHAIN_CYCLE }); return R; }
            if (int(Nodes.size()) == max_chain_depth)                                   { R.m_Diags.push_back({ diag_code::E_CHAIN_DEPTH }); return R; }
            if (!Nodes.empty() && p->m_Root != Nodes[0]->m_Root)                        { R.m_Diags.push_back({ diag_code::E_PARENT_ROOT }); return R; }
            R.m_Chain.push_back(Key);
            Nodes.push_back(p);
            Key = p->m_Parent;
        }

        using namespace xmaterial;
        for (const auto& D : M.m_Decls) R.m_Params.push_back({ D.m_ID, D.m_Type, D.m_Default });

        for (int i = int(Nodes.size()) - 1; i >= 0; --i)                                // the root-most ancestor first, so the nearer one wins
            for (const auto& O : Nodes[i]->m_Overrides)
            {
                auto It = std::find_if(R.m_Params.begin(), R.m_Params.end(), [&](auto& P) { return P.m_ID == O.m_ID; });
                if (It == R.m_Params.end())  { R.m_Diags.push_back({ diag_code::W_ORPHAN_ID,   O.m_ID }); continue; }      // kept in the file, not applied
                if (It->m_Type != O.m_Type)  { R.m_Diags.push_back({ diag_code::W_ORPHAN_TYPE, O.m_ID }); continue; }
                It->m_Value  = O.m_Value;
                It->m_Source = i;
            }

        // validRange, plus the COLOR rules of section 3: no alpha -> w = 1, not HDR -> rgb in [0,1]. A component past the type's size is left alone.
        for (std::size_t k = 0; k < R.m_Params.size(); ++k)
        {
            const auto& D = M.m_Decls[k];
            auto&       P = R.m_Params[k];
            if (D.m_Type == param_type::TEXTURE) continue;
            const int    n  = D.m_Type == param_type::FLOAT ? 1 : D.m_Type == param_type::FLOAT2 ? 2 : D.m_Type == param_type::FLOAT3 ? 3 : 4;
            float*       V  = &P.m_Value.m_X;
            const float* Lo = &D.m_ValidMin.m_X, *Hi = &D.m_ValidMax.m_X;
            bool bClamped = false;
            for (int c = 0; c < n; ++c)
            {
                float lo = Lo[c], hi = Hi[c];
                if (D.m_Type == param_type::COLOR && c < 3 && !D.m_bHDR) { lo = std::max(lo, 0.f); hi = std::min(hi, 1.f); }
                const float v = std::clamp(V[c], lo, hi);
                bClamped |= v != V[c];
                V[c] = v;
            }
            if (D.m_Type == param_type::COLOR && !D.m_bAlpha) P.m_Value.m_W = 1.f;       // forced, not reported
            if (bClamped) R.m_Diags.push_back({ diag_code::W_CLAMPED, D.m_ID });
        }
        return R;
    }

    // SetParamDefault -Policy PreserveLook (section 6): every instance whose parent is the material (m_Parent == 0, same root) and has no override for the parameter gets the
    // OLD default written, so it looks as before; deeper instances inherit it through them. Returns the keys it wrote to. Overrides stay sorted by m_ID.
    inline std::vector<std::uint64_t> PreserveLook(std::vector<resolve_node>& All, std::uint64_t Root, const xmaterial::param_decl& Decl, const xmaterial::float4v& OldDefault)
    {
        std::vector<std::uint64_t> Written;
        for (auto& N : All)
        {
            if (N.m_Parent != 0 || N.m_Root != Root) continue;
            auto It = std::lower_bound(N.m_Overrides.begin(), N.m_Overrides.end(), Decl.m_ID, [](auto& O, auto ID) { return O.m_ID < ID; });
            if (It != N.m_Overrides.end() && It->m_ID == Decl.m_ID) continue;           // already set here (a wrong-typed one is an orphan, still the person's: left alone)
            N.m_Overrides.insert(It, { Decl.m_ID, Decl.m_Type, OldDefault });
            Written.push_back(N.m_Key);
        }
        return Written;
    }

    // The self-check of the resolver (acceptance tests 3 and 4). Run by plugins/xmaterial_instance.plugin/source/xmaterial_instance_resolve_test.cpp
    // The save/load round trip of an unknown ID is the descriptor's (task 8); here: Resolve never touches the overrides, so the orphan is still there to be saved.
    inline void demo()
    {
        using namespace xmaterial;
        param_decl Tint; Tint.m_ID = 1; Tint.m_Name = "Tint"; Tint.m_Type = param_type::COLOR; Tint.m_bAlpha = false; Tint.m_EntityField = entity_field::TINT;
        Tint.m_Default = { 1, 1, 1, 1 };
        param_decl Rough; Rough.m_ID = 2; Rough.m_Name = "Roughness"; Rough.m_Type = param_type::FLOAT; Rough.m_Default = { 0.5f };
        Rough.m_ValidMin = { 0 }; Rough.m_ValidMax = { 1 };
        const auto M = Pack({ Tint, Rough }, { 1, 2 });

        enum { MAT = 100, RED = 1, REDROUGH = 2 };
        std::vector<resolve_node> All
        { { RED,      0,   MAT, { { 1, param_type::COLOR, { 1, 0, 0, 1 } } } }
        , { REDROUGH, RED, MAT, { { 2, param_type::FLOAT, { 0.9f } } } }
        };
        auto Lookup = [&](std::uint64_t K) -> const resolve_node* { for (auto& N : All) if (N.m_Key == K) return &N; return nullptr; };
        auto Get    = [&](const resolved& R, param_id ID) { for (auto& P : R.m_Params) if (P.m_ID == ID) return P; assert(false); return R.m_Params[0]; };

        auto R = Resolve(M, REDROUGH, Lookup);                                          // Tint from the parent, Roughness from here
        assert(!R.Failed() && R.m_Diags.empty() && R.m_Chain.size() == 2);
        assert(Get(R, 1).m_Value.m_X == 1 && Get(R, 1).m_Value.m_Y == 0 && Get(R, 1).m_Source == 1);
        assert(Get(R, 2).m_Value.m_X == 0.9f && Get(R, 2).m_Source == 0);

        All[0].m_Overrides.clear();                                                     // ClearOverride on MI_Red's Tint: back to the default
        R = Resolve(M, REDROUGH, Lookup);
        assert(Get(R, 1).m_Value.m_Y == 1 && Get(R, 1).m_Source == -1);
        All[0].m_Overrides = { { 1, param_type::COLOR, { 1, 0, 0, 1 } } };

        // Cycle, depth, root mismatch
        All.push_back({ 10, 11, MAT, {} }); All.push_back({ 11, 10, MAT, {} });
        R = Resolve(M, 10, Lookup);
        assert(R.Failed() && R.m_Diags[0].m_Code == diag_code::E_CHAIN_CYCLE);
        for (std::uint64_t k = 20; k < 29; ++k) All.push_back({ k, k + 1, MAT, {} });   // 20 -> ... -> 28 -> 29: 10 instances from 20, 8 from 22
        All.push_back({ 29, 0, MAT, {} });
        assert(Resolve(M, 20, Lookup).m_Diags[0].m_Code == diag_code::E_CHAIN_DEPTH);
        assert(!Resolve(M, 22, Lookup).Failed());                                       // exactly 8 is fine
        All.push_back({ 30, RED, 999, {} });
        assert(Resolve(M, 30, Lookup).m_Diags[0].m_Code == diag_code::E_PARENT_ROOT);

        // Orphans are reported, kept, not applied: unknown ID, retyped; out of range is clamped
        All[1].m_Overrides = { { 2, param_type::FLOAT, { 7 } }, { 77, param_type::FLOAT, { 3 } } };
        All[0].m_Overrides = { { 1, param_type::FLOAT, { 3 } } };                       // Tint set as a FLOAT: type orphan
        R = Resolve(M, REDROUGH, Lookup);
        assert(Get(R, 1).m_Source == -1 && Get(R, 2).m_Value.m_X == 1.f);               // Tint stays its default; Roughness clamped to validMax
        int nId = 0, nType = 0, nClamp = 0;
        for (auto& D : R.m_Diags) { nId += D.m_Code == diag_code::W_ORPHAN_ID && D.m_ID == 77; nType += D.m_Code == diag_code::W_ORPHAN_TYPE; nClamp += D.m_Code == diag_code::W_CLAMPED; }
        assert(nId == 1 && nType == 1 && nClamp == 1 && All[1].m_Overrides.size() == 2 && All[1].m_Overrides[1].m_ID == 77);
        All[0].m_Overrides = { { 1, param_type::COLOR, { 4, 0.5f, -1, 0.2f } } };
        R = Resolve(M, RED, Lookup);                                                    // not HDR: rgb to [0,1]; no alpha: w = 1
        assert(Get(R, 1).m_Value.m_X == 1 && Get(R, 1).m_Value.m_Z == 0 && Get(R, 1).m_Value.m_W == 1);

        // PreserveLook (test 4): the default goes 0.5 -> 0.2; MI_Red (parent is the material) gets 0.5, MI_RedRough already sets it and is unchanged
        All = { { RED, 0, MAT, { { 1, param_type::COLOR, { 1, 0, 0, 1 } } } }, { REDROUGH, RED, MAT, { { 2, param_type::FLOAT, { 0.9f } } } } };
        auto Written = PreserveLook(All, MAT, Rough, Rough.m_Default);
        Rough.m_Default = { 0.2f };
        const auto M2 = Pack({ Tint, Rough }, { 1, 2 });
        assert(Written == std::vector<std::uint64_t>{ RED } && All[0].m_Overrides.size() == 2 && All[0].m_Overrides[1].m_ID == 2 && All[1].m_Overrides.size() == 1);
        assert(Get(Resolve(M2, RED, Lookup), 2).m_Value.m_X == 0.5f && Get(Resolve(M2, REDROUGH, Lookup), 2).m_Value.m_X == 0.9f);
    }
}

#endif // XMATERIAL_INSTANCE_RESOLVE_H
