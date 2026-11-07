#include "PCH.h"
#include "Targeting.h"

#include "Classification.h"

#include "RE/P/ProcessLists.h"

#include <algorithm>
#include <cmath>

namespace
{
    template <class EvaluateCandidate>
    TULLBUNG::Targeting::Selection
        FindBestTarget(
            RE::Actor* a_player,
            float a_radiusUnits,
            float a_tieEpsilon,
            EvaluateCandidate&& a_evaluate) noexcept
    {
        TULLBUNG::Targeting::Selection
            result{};

        if (!a_player ||
            !std::isfinite(a_radiusUnits) ||
            a_radiusUnits <= 0.0f) {

            return result;
        }

        auto* processLists =
            RE::ProcessLists::
                GetSingleton();

        if (!processLists) {
            return result;
        }

        result.processListsAvailable =
            true;

        processLists->ForEachHighActor(
            [&](RE::Actor* a_candidate) {
                if (!a_candidate ||
                    a_candidate == a_player ||
                    a_candidate->IsDead() ||
                    !a_candidate->
                        IsHostileToActor(
                            a_player)) {

                    return
                        RE::BSContainer::
                            ForEachResult::
                                kContinue;
                }

                const float distance =
                    a_player->GetDistance(
                        a_candidate);

                if (!std::isfinite(distance) ||
                    distance > a_radiusUnits) {

                    return
                        RE::BSContainer::
                            ForEachResult::
                                kContinue;
                }

                float score{};

                if (!a_evaluate(
                        a_candidate,
                        score) ||
                    !std::isfinite(score)) {

                    return
                        RE::BSContainer::
                            ForEachResult::
                                kContinue;
                }

                const bool higherScore =
                    !result.actor ||
                    score >
                        result.score +
                            a_tieEpsilon;

                const bool sameScore =
                    result.actor &&
                    std::fabs(
                        score -
                        result.score) <=
                        a_tieEpsilon;

                const bool nearerTie =
                    sameScore &&
                    distance <
                        result.distance;

                if (higherScore ||
                    nearerTie) {

                    result.actor =
                        a_candidate;

                    result.score =
                        score;

                    result.distance =
                        distance;
                }

                return
                    RE::BSContainer::
                        ForEachResult::
                            kContinue;
            });

        return result;
    }
}

namespace TULLBUNG::Targeting
{
    Selection FindHighestMaxHealthMonsterWithLOS(
        RE::Actor* a_player,
        float a_radiusUnits) noexcept
    {
        return FindBestTarget(
            a_player,
            a_radiusUnits,
            0.001f,
            [a_player](
                RE::Actor* a_candidate,
                float& a_score) noexcept {
                auto* candidateAVOwner =
                    a_candidate->
                        AsActorValueOwner();

                if (!candidateAVOwner) {
                    return false;
                }

                const float currentHealth =
                    candidateAVOwner->
                        GetActorValue(
                            RE::ActorValue::
                                kHealth);

                if (!std::isfinite(
                        currentHealth) ||
                    currentHealth <= 0.0f) {

                    return false;
                }

                if (!TULLBUNG::
                        Classification::
                            IsMonster(
                                a_candidate)) {

                    return false;
                }

                bool losContext{};

                if (!a_player->
                        HasLineOfSight(
                            a_candidate,
                            losContext)) {

                    return false;
                }

                const float maxHealth =
                    a_candidate->
                        GetActorValueMax(
                            RE::ActorValue::
                                kHealth);

                if (!std::isfinite(
                        maxHealth) ||
                    maxHealth <= 0.0f) {

                    return false;
                }

                a_score =
                    maxHealth;

                return true;
            });
    }


    Selection FindHighestCurrentMagickaHostile(
        RE::Actor* a_player,
        float a_radiusUnits) noexcept
    {
        return FindBestTarget(
            a_player,
            a_radiusUnits,
            0.0f,
            [](RE::Actor* a_candidate,
               float& a_score) noexcept {
                auto* targetOwner =
                    a_candidate->
                        AsActorValueOwner();

                if (!targetOwner) {
                    return false;
                }

                const float rawMagicka =
                    targetOwner->
                        GetActorValue(
                            RE::ActorValue::
                                kMagicka);

                if (!std::isfinite(
                        rawMagicka)) {

                    return false;
                }

                a_score =
                    (std::max)(
                        rawMagicka,
                        0.0f);

                return true;
            });
    }
}