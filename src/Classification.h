#pragma once

namespace TULLBUNG::Classification
{
    enum class WeaponClass : std::uint8_t
    {
        kNone = 0,
        kOneHandSword,
        kOneHandDagger,
        kOneHandAxe,
        kOneHandMace,
        kTwoHandSword,
        kTwoHandAxe,
        kBow,
        kCrossbow,
        kStaff,
        kOther
    };

    [[nodiscard]] WeaponClass ClassifyWeapon(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsDagger(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsOneHandedBlunt(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsMelee(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsRanged(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsBowOrCrossbow(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsBound(
        const RE::TESObjectWEAP* a_weapon) noexcept;

    [[nodiscard]] bool IsMonster(
        const RE::Actor* a_actor) noexcept;

    // 개별 Effect가 실제 체력 회복 효과인지 판정.
    [[nodiscard]] bool IsHealthRestorationEffect(
        const RE::Effect* a_effect) noexcept;

    // 왼손 사제 자세 등에 사용하는 체력 회복 주문 판정.
    [[nodiscard]] bool IsHealthRestorationSpell(
        const RE::SpellItem* a_spell) noexcept;

    // 직접적인 공격 주문 판정.
    [[nodiscard]] bool IsAttackSpell(
        const RE::SpellItem* a_spell) noexcept;

    // 공성마도사용 투사체형 파괴마법 판정.
    [[nodiscard]] bool IsProjectileDestructionSpell(
        const RE::SpellItem* a_spell) noexcept;

    // 개별 Spell Effect가 TULLBUNG의 직접 Health 피해인지 판정.
    // Spell 전체가 아니라 ActiveEffect의 원본 Effect 단위 판정에 사용한다.
    [[nodiscard]] bool IsDirectHealthDamageEffect(
        const RE::Effect* a_effect) noexcept;

    // Spell Classification 런타임 검증 전용.
    // PASS 후 제거한다.
    void DebugLogSpellClassification(
        const RE::SpellItem* a_spell) noexcept;
}
