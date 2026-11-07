#pragma once

#include "Classification.h"

namespace TULLBUNG::PlayerState
{
    struct HandState final
    {
        RE::TESForm* form{};
        RE::TESObjectWEAP* weapon{};
        RE::SpellItem* spell{};
        RE::TESObjectARMO* armor{};

        Classification::WeaponClass weaponClass{
            Classification::WeaponClass::kNone
        };

        bool boundWeapon{};
    };

    struct EquipmentState final
    {
        HandState right{};
        HandState left{};

        bool hasShield{};
        std::uint8_t lightArmorPieces{};
    };

    struct ArtifactState final
    {
        // 실제 인벤토리에 있는 서로 다른 인정 유물 수.
        // 범위: 0~17
        std::uint8_t uniqueOwned{};

        // 효과 계산에 사용되는 수.
        // 최대 8.
        std::uint8_t counted{};

        // "데이드릭 유물 연구"를 보유한다고 가정했을 때의
        // 체력/매지카/스태미나 각각의 잠재 보너스.
        // 실제 적용은 BuildState 단계에서 처리한다.
        std::uint16_t potentialBonusEach{};
    };

    bool Initialize();

    void RefreshEquipment();
    void RefreshArtifacts();
    void RefreshAll();

    [[nodiscard]] const EquipmentState& GetEquipment() noexcept;
    [[nodiscard]] const ArtifactState& GetArtifacts() noexcept;
}
