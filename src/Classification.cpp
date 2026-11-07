#include "PCH.h"
#include "Classification.h"
#include "DataCache.h"

namespace
{
    bool ListContainsForm(
        const RE::BGSListForm* a_list,
        const RE::TESForm* a_target)
    {
        if (!a_list || !a_target) {
            return false;
        }

        bool found = false;

        a_list->ForEachForm([&](RE::TESForm* a_form) {
            if (a_form == a_target) {
                found = true;
                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return found;
    }

    bool ActorHasApplicableKeyword(
        const RE::Actor* a_actor,
        const RE::BGSKeyword* a_keyword)
    {
        if (!a_actor || !a_keyword) {
            return false;
        }

        const char* editorID =
            a_keyword->GetFormEditorID();

        if (!editorID || editorID[0] == '\0') {
            return false;
        }

        return const_cast<RE::Actor*>(a_actor)->
            HasKeywordString(editorID);
    }

    bool HasOfficialMonsterKeyword(
        const RE::Actor* a_actor,
        const RE::BGSListForm* a_keywordList,
        const RE::BGSKeyword* a_customKeyword)
    {
        if (!a_actor || !a_keywordList) {
            return false;
        }

        bool found = false;

        a_keywordList->ForEachForm([&](RE::TESForm* a_form) {
            auto* keyword =
                a_form ?
                    a_form->As<RE::BGSKeyword>() :
                    nullptr;

            // TULLBUNG_Monster는 두 번째 우선순위에서 처리한다.
            if (!keyword ||
                keyword == a_customKeyword) {

                return RE::BSContainer::ForEachResult::kContinue;
            }

            if (ActorHasApplicableKeyword(
                    a_actor,
                    keyword)) {
                found = true;

                return RE::BSContainer::ForEachResult::kStop;
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        return found;
    }

    bool IsNormalSpell(
        const RE::SpellItem* a_spell) noexcept
    {
        return
            a_spell &&
            a_spell->GetSpellType() ==
                RE::MagicSystem::SpellType::kSpell;
    }

    bool HasRecoverFlag(
        const RE::EffectSetting* a_effect) noexcept
    {
        if (!a_effect) {
            return false;
        }

        using Flag =
            RE::EffectSetting::EffectSettingData::Flag;

        return a_effect->data.flags.all(
            Flag::kRecover);
    }

    bool IsHealthRestorationEffectImpl(
        const RE::Effect* a_effect) noexcept
    {
        if (!a_effect ||
            !a_effect->baseEffect) {

            return false;
        }

        const auto* base =
            a_effect->baseEffect;

        if (base->data.primaryAV !=
            RE::ActorValue::kHealth) {

            return false;
        }

        // 공격/피해 효과는 회복 주문으로 인정하지 않는다.
        if (base->IsHostile() ||
            base->IsDetrimental()) {

            return false;
        }

        // Recover는 지속시간 종료 시 원상복구하는
        // Fortify형 효과에 사용되므로 실제 체력 회복에서 제외.
        if (HasRecoverFlag(base)) {
            return false;
        }

        if (a_effect->GetMagnitude() <= 0.0f) {
            return false;
        }

        const auto archetype =
            base->GetArchetype();

        using AT =
            RE::EffectArchetypes::ArchetypeID;

        return
            archetype == AT::kValueModifier ||
            archetype == AT::kDualValueModifier ||
            archetype == AT::kPeakValueModifier;
    }

    bool IsDirectHealthDamageEffectImpl(
        const RE::Effect* a_effect) noexcept
    {
        if (!a_effect ||
            !a_effect->baseEffect) {

            return false;
        }

        const auto* base =
            a_effect->baseEffect;

        if (base->data.primaryAV !=
            RE::ActorValue::kHealth) {

            return false;
        }

        if (!base->IsHostile() ||
            !base->IsDetrimental()) {

            return false;
        }

        // 일시적인 최대 체력 감소 등 Recover형은
        // 직접 피해로 보지 않는다.
        if (HasRecoverFlag(base)) {
            return false;
        }

        if (a_effect->GetMagnitude() <= 0.0f) {
            return false;
        }

        // 순수 지속피해는 직접 피해에서 제외.
        // 바닐라 직접타격/집중형 효과의 0~1초 단위 효과는 허용.
        if (a_effect->GetDuration() > 1) {
            return false;
        }

        const auto archetype =
            base->GetArchetype();

        using AT =
            RE::EffectArchetypes::ArchetypeID;

        const bool damageArchetype =
            archetype == AT::kValueModifier ||
            archetype == AT::kDualValueModifier ||
            archetype == AT::kPeakValueModifier ||
            archetype == AT::kAbsorb;

        if (!damageArchetype) {
            return false;
        }

        // Barrier형 projectile은 Wall 계열로 간주하여
        // 직접 공격 주문에서 제외.
        if (const auto* projectile =
                base->data.projectileBase;
            projectile &&
            projectile->IsBarrier()) {

            return false;
        }

        return true;
    }

    bool IsValidSiegeProjectile(
        const RE::BGSProjectile* a_projectile) noexcept
    {
        if (!a_projectile) {
            return false;
        }

        // 실제 전방 투사체 계열.
        // Beam은 바닐라 Lightning Bolt 계열 때문에 포함한다.
        return
            a_projectile->IsMissile() ||
            a_projectile->IsGrenade() ||
            a_projectile->IsBeam();
    }

    std::string_view CastingTypeName(
        RE::MagicSystem::CastingType a_type) noexcept
    {
        using CT =
            RE::MagicSystem::CastingType;

        switch (a_type) {
        case CT::kConstantEffect:
            return "CONSTANT";
        case CT::kFireAndForget:
            return "FIRE_AND_FORGET";
        case CT::kConcentration:
            return "CONCENTRATION";
        case CT::kScroll:
            return "SCROLL";
        default:
            return "UNKNOWN";
        }
    }

    std::string_view DeliveryName(
        RE::MagicSystem::Delivery a_delivery) noexcept
    {
        using D =
            RE::MagicSystem::Delivery;

        switch (a_delivery) {
        case D::kSelf:
            return "SELF";
        case D::kTouch:
            return "TOUCH";
        case D::kAimed:
            return "AIMED";
        case D::kTargetActor:
            return "TARGET_ACTOR";
        case D::kTargetLocation:
            return "TARGET_LOCATION";
        default:
            return "UNKNOWN";
        }
    }
}

namespace TULLBUNG::Classification
{
    bool IsDagger(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        if (!a_weapon) {
            return false;
        }

        const auto& forms =
            DataCache::Get();

        if (forms.weapTypeDagger &&
            a_weapon->HasKeyword(
                forms.weapTypeDagger)) {

            return true;
        }

        return a_weapon->IsOneHandedDagger();
    }

    WeaponClass ClassifyWeapon(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        if (!a_weapon) {
            return WeaponClass::kNone;
        }

        if (IsDagger(a_weapon)) {
            return WeaponClass::kOneHandDagger;
        }

        if (a_weapon->IsOneHandedSword()) {
            return WeaponClass::kOneHandSword;
        }

        if (a_weapon->IsOneHandedAxe()) {
            return WeaponClass::kOneHandAxe;
        }

        if (a_weapon->IsOneHandedMace()) {
            return WeaponClass::kOneHandMace;
        }

        if (a_weapon->IsTwoHandedSword()) {
            return WeaponClass::kTwoHandSword;
        }

        if (a_weapon->IsTwoHandedAxe()) {
            return WeaponClass::kTwoHandAxe;
        }

        if (a_weapon->IsBow()) {
            return WeaponClass::kBow;
        }

        if (a_weapon->IsCrossbow()) {
            return WeaponClass::kCrossbow;
        }

        if (a_weapon->IsStaff()) {
            return WeaponClass::kStaff;
        }

        return WeaponClass::kOther;
    }

    bool IsOneHandedBlunt(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        return
            a_weapon &&
            a_weapon->IsOneHandedMace();
    }

    bool IsMelee(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        return
            a_weapon &&
            a_weapon->IsMelee();
    }

    bool IsRanged(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        return
            a_weapon &&
            (a_weapon->IsBow() ||
             a_weapon->IsCrossbow());
    }

    bool IsBowOrCrossbow(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        return
            a_weapon &&
            (a_weapon->IsBow() ||
             a_weapon->IsCrossbow());
    }

    bool IsBound(
        const RE::TESObjectWEAP* a_weapon) noexcept
    {
        return
            a_weapon &&
            a_weapon->IsBound();
    }

    bool IsMonster(
        const RE::Actor* a_actor) noexcept
    {
        if (!a_actor ||
            !DataCache::IsReady()) {

            return false;
        }

        const auto& forms =
            DataCache::Get();

        // 1. 공식 분류 Keyword
        if (HasOfficialMonsterKeyword(
                a_actor,
                forms.monsterKeywords,
                forms.monsterKeyword)) {

            return true;
        }

        // 2. TULLBUNG 명시 override Keyword
        if (forms.monsterKeyword &&
            ActorHasApplicableKeyword(
                a_actor,
                forms.monsterKeyword)) {

            return true;
        }

        // 3. Race whitelist
        const auto* race =
            a_actor->GetRace();

        return ListContainsForm(
            forms.monsterRaceWhitelist,
            race);
    }

    bool IsHealthRestorationEffect(
        const RE::Effect* a_effect) noexcept
    {
        return IsHealthRestorationEffectImpl(
            a_effect);
    }

    bool IsHealthRestorationSpell(
        const RE::SpellItem* a_spell) noexcept
    {
        if (!IsNormalSpell(a_spell)) {
            return false;
        }

        for (const auto* effect :
             a_spell->effects) {

            if (IsHealthRestorationEffectImpl(
                    effect)) {

                return true;
            }
        }

        return false;
    }

    bool IsDirectHealthDamageEffect(
        const RE::Effect* a_effect) noexcept
    {
        return
            IsDirectHealthDamageEffectImpl(
                a_effect);
    }

    bool IsAttackSpell(
        const RE::SpellItem* a_spell) noexcept
    {
        if (!IsNormalSpell(a_spell)) {
            return false;
        }

        // Rune처럼 지점 설치형 주문은
        // 직접 공격 주문에서 제외.
        if (a_spell->GetDelivery() ==
            RE::MagicSystem::Delivery::
                kTargetLocation) {

            return false;
        }

        for (const auto* effect :
             a_spell->effects) {

            if (IsDirectHealthDamageEffectImpl(
                    effect)) {

                return true;
            }
        }

        return false;
    }

    bool IsProjectileDestructionSpell(
        const RE::SpellItem* a_spell) noexcept
    {
        if (!IsNormalSpell(a_spell)) {
            return false;
        }

        // 공성마도사: 집중형 제외.
        if (a_spell->GetCastingType() !=
            RE::MagicSystem::CastingType::
                kFireAndForget) {

            return false;
        }

        const auto delivery =
            a_spell->GetDelivery();

        // Rune(TargetLocation) 및 Self AoE 제외.
        if (delivery !=
                RE::MagicSystem::Delivery::kAimed &&
            delivery !=
                RE::MagicSystem::Delivery::kTargetActor) {

            return false;
        }

        for (const auto* effect :
             a_spell->effects) {

            if (!IsDirectHealthDamageEffectImpl(
                    effect) ||
                !effect ||
                !effect->baseEffect) {

                continue;
            }

            const auto* base =
                effect->baseEffect;

            if (base->data.associatedSkill !=
                RE::ActorValue::kDestruction) {

                continue;
            }

            if (IsValidSiegeProjectile(
                    base->data.projectileBase)) {

                return true;
            }
        }

        return false;
    }

    void DebugLogSpellClassification(
        const RE::SpellItem* a_spell) noexcept
    {
        if (!a_spell) {
            logger::info(
                "[SpellTest] Spell=NULL");

            return;
        }

        const auto* name =
            a_spell->GetName();

        logger::info(
            "[SpellTest] Spell=\"{}\" | Form={:08X} | HealthRestore={} | Attack={} | ProjectileDestruction={} | Casting={} | Delivery={}",
            name && name[0] ?
                name :
                "<unnamed>",
            a_spell->GetFormID(),
            IsHealthRestorationSpell(a_spell),
            IsAttackSpell(a_spell),
            IsProjectileDestructionSpell(a_spell),
            CastingTypeName(
                a_spell->GetCastingType()),
            DeliveryName(
                a_spell->GetDelivery()));

        std::size_t index = 0;

        for (const auto* effect :
             a_spell->effects) {

            if (!effect ||
                !effect->baseEffect) {

                logger::info(
                    "[SpellTestEffect] Index={} | MGEF=NULL",
                    index++);

                continue;
            }

            const auto* base =
                effect->baseEffect;

            const auto* projectile =
                base->data.projectileBase;

            logger::info(
                "[SpellTestEffect] Index={} | MGEF={:08X} | AV={} | Skill={} | Archetype={} | Hostile={} | Detrimental={} | Recover={} | Magnitude={:.2f} | Duration={} | Projectile={:08X} | Missile={} | Grenade={} | Beam={} | Flame={} | Cone={} | Barrier={} | Arrow={}",
                index++,
                base->GetFormID(),
                static_cast<std::int32_t>(
                    base->data.primaryAV),
                static_cast<std::int32_t>(
                    base->data.associatedSkill),
                static_cast<std::int32_t>(
                    base->GetArchetype()),
                base->IsHostile(),
                base->IsDetrimental(),
                HasRecoverFlag(base),
                effect->GetMagnitude(),
                effect->GetDuration(),
                projectile ?
                    projectile->GetFormID() :
                    0,
                projectile &&
                    projectile->IsMissile(),
                projectile &&
                    projectile->IsGrenade(),
                projectile &&
                    projectile->IsBeam(),
                projectile &&
                    projectile->IsFlamethrower(),
                projectile &&
                    projectile->IsCone(),
                projectile &&
                    projectile->IsBarrier(),
                projectile &&
                    projectile->IsArrow());
        }
    }
}
