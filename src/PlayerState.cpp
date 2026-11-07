#include "PCH.h"
#include "PlayerState.h"
#include "DataCache.h"
#include "ClassRuntime.h"

namespace
{
    TULLBUNG::PlayerState::EquipmentState g_equipment{};
    TULLBUNG::PlayerState::ArtifactState g_artifacts{};

    bool g_registered{};

    std::string_view WeaponClassName(
        TULLBUNG::Classification::WeaponClass a_class)
    {
        using WC = TULLBUNG::Classification::WeaponClass;

        switch (a_class) {
        case WC::kNone:
            return "NONE";
        case WC::kOneHandSword:
            return "1H_SWORD";
        case WC::kOneHandDagger:
            return "DAGGER";
        case WC::kOneHandAxe:
            return "1H_AXE";
        case WC::kOneHandMace:
            return "1H_MACE";
        case WC::kTwoHandSword:
            return "2H_SWORD";
        case WC::kTwoHandAxe:
            return "2H_AXE";
        case WC::kBow:
            return "BOW";
        case WC::kCrossbow:
            return "CROSSBOW";
        case WC::kStaff:
            return "STAFF";
        case WC::kOther:
        default:
            return "OTHER";
        }
    }

    void FillHandState(
        TULLBUNG::PlayerState::HandState& a_state,
        RE::TESForm* a_form)
    {
        a_state = {};
        a_state.form = a_form;

        if (!a_form) {
            return;
        }

        a_state.weapon =
            a_form->As<RE::TESObjectWEAP>();

        a_state.spell =
            a_form->As<RE::SpellItem>();

        a_state.armor =
            a_form->As<RE::TESObjectARMO>();

        if (a_state.weapon) {
            a_state.weaponClass =
                TULLBUNG::Classification::ClassifyWeapon(
                    a_state.weapon);

            a_state.boundWeapon =
                TULLBUNG::Classification::IsBound(
                    a_state.weapon);
        }
    }

    std::uint8_t CountLightArmorPieces(
        RE::PlayerCharacter* a_player)
    {
        if (!a_player) {
            return 0;
        }

        std::uint8_t count = 0;

        const auto inventory =
            a_player->GetInventory(
                [](RE::TESBoundObject& a_object) {
                    return a_object.IsArmor();
                });

        for (const auto& [item, data] : inventory) {
            const auto& [itemCount, entry] = data;

            if (itemCount <= 0 ||
                !entry ||
                !entry->IsWorn()) {
                continue;
            }

            auto* armor =
                item->As<RE::TESObjectARMO>();

            if (!armor) {
                continue;
            }

            // 방패는 "경갑 방어구 부위 수"에서 제외.
            if (armor->IsShield()) {
                continue;
            }

            if (armor->IsLightArmor()) {
                ++count;
            }
        }

        return count;
    }

    bool IsRecognizedArtifactFormID(
        RE::FormID a_formID)
    {
        if (!a_formID ||
            !TULLBUNG::DataCache::IsReady()) {
            return false;
        }

        const auto* list =
            TULLBUNG::DataCache::Get().daedricArtifacts;

        if (!list) {
            return false;
        }

        bool found = false;

        list->ForEachForm(
            [&](RE::TESForm* a_form) {
                if (a_form &&
                    a_form->GetFormID() == a_formID) {
                    found = true;

                    return RE::BSContainer::ForEachResult::kStop;
                }

                return RE::BSContainer::ForEachResult::kContinue;
            });

        return found;
    }

    std::uint8_t CountUniqueArtifacts(
        RE::PlayerCharacter* a_player)
    {
        if (!a_player ||
            !TULLBUNG::DataCache::IsReady()) {
            return 0;
        }

        const auto inventory =
            a_player->GetInventory();

        std::uint8_t count = 0;

        for (const auto& [item, data] : inventory) {
            if (!item) {
                continue;
            }

            const auto& [itemCount, entry] = data;

            // 같은 base item이 여러 개 있어도
            // inventory map의 한 entry이므로 1개로 계산된다.
            if (itemCount <= 0) {
                continue;
            }

            if (IsRecognizedArtifactFormID(
                    item->GetFormID())) {
                ++count;
            }
        }

        return count;
    }

    std::uint16_t CalculateArtifactBonus(
        std::uint8_t a_counted)
    {
        std::uint16_t bonus =
            static_cast<std::uint16_t>(a_counted) * 10;

        if (a_counted >= 5) {
            bonus += 50;
        }

        if (a_counted >= 8) {
            bonus += 80;
        }

        return bonus;
    }

    class EquipEventSink final :
        public RE::BSTEventSink<RE::TESEquipEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESEquipEvent* a_event,
            RE::BSTEventSource<RE::TESEquipEvent>*) override
        {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player =
                RE::PlayerCharacter::GetSingleton();

            if (!player ||
                a_event->actor.get() != player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            TULLBUNG::PlayerState::RefreshEquipment();

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class ContainerChangedEventSink final :
        public RE::BSTEventSink<
            RE::TESContainerChangedEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent* a_event,
            RE::BSTEventSource<
                RE::TESContainerChangedEvent>*) override
        {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player =
                RE::PlayerCharacter::GetSingleton();

            if (!player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto playerID =
                player->GetFormID();

            const bool touchesPlayer =
                a_event->oldContainer == playerID ||
                a_event->newContainer == playerID;

            if (!touchesPlayer) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // 일반 전리품/포션/화살 등의 이동에는
            // ArtifactState를 다시 계산하지 않는다.
            if (!IsRecognizedArtifactFormID(
                    a_event->baseObj)) {
                return RE::BSEventNotifyControl::kContinue;
            }

            TULLBUNG::PlayerState::RefreshArtifacts();

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    EquipEventSink g_equipEventSink{};
    ContainerChangedEventSink g_containerChangedEventSink{};
}

namespace TULLBUNG::PlayerState
{
    bool Initialize()
    {
        if (g_registered) {
            return true;
        }

        auto* eventHolder =
            RE::ScriptEventSourceHolder::GetSingleton();

        if (!eventHolder) {
            logger::error(
                "[PlayerState] ScriptEventSourceHolder = MISSING");
            return false;
        }

        eventHolder->AddEventSink(
            &g_equipEventSink);

        eventHolder->AddEventSink(
            &g_containerChangedEventSink);

        g_registered = true;

        logger::info(
            "[PlayerState] EquipEventSink = OK");

        logger::info(
            "[PlayerState] ContainerChangedEventSink = OK");

        RefreshAll();

        return true;
    }

    void RefreshEquipment()
    {
        auto* player =
            RE::PlayerCharacter::GetSingleton();

        if (!player) {
            g_equipment = {};
            return;
        }

        FillHandState(
            g_equipment.right,
            player->GetEquippedObject(false));

        FillHandState(
            g_equipment.left,
            player->GetEquippedObject(true));

        g_equipment.hasShield =
            g_equipment.left.armor &&
            g_equipment.left.armor->IsShield();

        g_equipment.lightArmorPieces =
            CountLightArmorPieces(player);

        logger::info(
            "[PlayerState] Right={} Bound={} | Left={} Bound={} | Shield={} | LightArmor={}",
            WeaponClassName(
                g_equipment.right.weaponClass),
            g_equipment.right.boundWeapon,
            WeaponClassName(
                g_equipment.left.weaponClass),
            g_equipment.left.boundWeapon,
            g_equipment.hasShield,
            g_equipment.lightArmorPieces);

        // Event-driven class passive refresh.
        TULLBUNG::ClassRuntime::
            OnEquipmentChanged();
    }

    void RefreshArtifacts()
    {
        auto* player =
            RE::PlayerCharacter::GetSingleton();

        if (!player) {
            g_artifacts = {};
            return;
        }

        const std::uint8_t uniqueOwned =
            CountUniqueArtifacts(player);

        const std::uint8_t counted =
            uniqueOwned > 8 ?
                8 :
                uniqueOwned;

        const std::uint16_t potentialBonus =
            CalculateArtifactBonus(counted);

        g_artifacts.uniqueOwned =
            uniqueOwned;

        g_artifacts.counted =
            counted;

        g_artifacts.potentialBonusEach =
            potentialBonus;

        logger::info(
            "[ArtifactState] UniqueOwned={} | Counted={} | PotentialBonusEach={}",
            g_artifacts.uniqueOwned,
            g_artifacts.counted,
            g_artifacts.potentialBonusEach);

        // Event-driven only:
        // recognized artifact inventory changes refresh this state.
        TULLBUNG::ClassRuntime::
            OnArtifactStateChanged();
    }

    void RefreshAll()
    {
        RefreshEquipment();
        RefreshArtifacts();
    }

    const EquipmentState& GetEquipment() noexcept
    {
        return g_equipment;
    }

    const ArtifactState& GetArtifacts() noexcept
    {
        return g_artifacts;
    }
}
