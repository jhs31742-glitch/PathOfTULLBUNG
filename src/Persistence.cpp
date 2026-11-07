#include "Profiles/RuntimeBoundary.h"
#include "Profiles/ProfileService.h"
#include "Profiles/UIProfileTransaction.h"
#include <cstring>
#include "PCH.h"
#include "Persistence.h"

#include "BuildState.h"
#include "VersionedBuildRecord.h"
#include "ClassRuntime.h"
#include "Hunter.h"
#include "Paladin.h"
#include "Prisma/PrismaBridge.h"
#include "TitlePersistenceCodec.h"
#include "TitleRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace
{
    // "TULL"
    inline constexpr std::uint32_t
        kSerializationID =
            0x4C4C5554;

    // "INQ1"
    inline constexpr std::uint32_t
        kInquisitorRecord =
            0x31514E49;

    inline constexpr std::uint32_t
        kInquisitorVersion = 1;

    // "VOD1"
    inline constexpr std::uint32_t
        kVoidRecord =
            0x31444F56;

    inline constexpr std::uint32_t
        kVoidVersion = 1;

    // "BLD1"
    inline constexpr std::uint32_t
        kBuildRecord =
            0x31444C42;

    inline constexpr std::uint32_t
        kBuildVersion = 1;

    // "CLS1"
    inline constexpr std::uint32_t
        kClassRecord =
            0x31534C43;

    inline constexpr std::uint32_t
        kClassVersion = 1;

    // "TIT1"
    inline constexpr std::uint32_t
        kTitleRecord =
            0x31544954;

    inline constexpr std::uint32_t
        kTitleVersion = 1;

    // "CTI1" - custom title slots.
    inline constexpr std::uint32_t
        kCustomTitleRecord = 0x31495443;

    inline constexpr std::uint32_t
        kCustomTitleVersion = 1;

    inline constexpr std::uint32_t
        kMaxCustomTitlePayload = static_cast<std::uint32_t>(
            8 + TULLBUNG::TitleSystem::kCustomTitleCount *
                (10 + 2 * TULLBUNG::TitleSystem::kCustomTextMaxBytes));

    inline constexpr std::uint32_t
        kMaxCountedMonsterKills = 500;

    inline constexpr std::uint8_t
        kMaxVoidCount = 4;

    inline constexpr std::uint8_t
        kMaxHolyStrikeOvercastIndex = 5;

    // Published BLD1/v1 disk record. Keep independent of future runtime storage.
    struct LegacyBuildV1 final
    {
        std::uint16_t highestPlayerLevelSeen{};
        std::array<std::uint8_t, 12> acquiredClasses{};
        std::array<std::uint8_t, 0xC0> acquiredNodes{};
        std::array<std::uint8_t, 0xC0> enabledNodes{};
        std::array<std::uint8_t, 0xC0> paidCost{};
    };

    static_assert(sizeof(LegacyBuildV1) == 590);
    static_assert(sizeof(TULLBUNG::BuildState::PersistentState) == (POT_QA15_EXTENSION ? 640 : 590));
    static_assert(offsetof(LegacyBuildV1, highestPlayerLevelSeen) == 0);
    static_assert(offsetof(LegacyBuildV1, acquiredClasses) == 2);
    static_assert(offsetof(LegacyBuildV1, acquiredNodes) == 14);
    static_assert(offsetof(LegacyBuildV1, enabledNodes) == 206);
    static_assert(offsetof(LegacyBuildV1, paidCost) == 398);
    static_assert(offsetof(TULLBUNG::BuildState::PersistentState, highestPlayerLevelSeen) == 0);
    static_assert(offsetof(TULLBUNG::BuildState::PersistentState, acquiredClasses) == 2);
    static_assert(offsetof(TULLBUNG::BuildState::PersistentState, acquiredNodes) == (POT_QA15_EXTENSION ? 15 : 14));
    static_assert(offsetof(TULLBUNG::BuildState::PersistentState, enabledNodes) == (POT_QA15_EXTENSION ? 223 : 206));
    static_assert(offsetof(TULLBUNG::BuildState::PersistentState, paidCost) == (POT_QA15_EXTENSION ? 431 : 398));

    struct ClassProgressRecord final
    {
        std::uint8_t holyStrikeOvercastIndex{};
        std::uint8_t quietProcessingReady{};
    };

    struct PendingLoadState final
    {
        bool hasInquisitor{};
        bool hasVoid{};
        bool hasBuild{};
        bool hasClass{};
        bool hasTitle{};
        bool hasCustomTitle{};
        TULLBUNG::BuildState::RetainedQAState retainedQA{};

        TULLBUNG::Persistence::
            InquisitorProgress inquisitor{};

        TULLBUNG::Persistence::
            VoidProgress voidProgress{};

        TULLBUNG::BuildState::
            PersistentState build{};

        ClassProgressRecord classProgress{};

        TULLBUNG::TitleSystem::
            PersistedState title{};

        TULLBUNG::TitleSystem::
            CustomTitleRecords customTitles{};    };

    TULLBUNG::Persistence::
        InquisitorProgress
            g_inquisitor{};

    TULLBUNG::Persistence::
        VoidProgress
            g_void{};

    PendingLoadState
        g_pending{};



    // Exact images of this POT block, bounded to 32 records / 1 MiB in RAM.
    // No conversion or new game-save schema. Incomplete capture is an explicit HOLD.
    struct OriginalRecord {std::uint32_t type{},version{};std::vector<std::byte> bytes;bool accepted=false;};
    std::vector<OriginalRecord> g_originalRecords;
    constexpr std::size_t kMaxOriginalRecords=32,kMaxOriginalBytes=1024*1024;
    std::size_t g_originalBytes=0;
    bool g_captureIncomplete=false,g_preserveOriginal=false;
    void ClearOriginalRecords(){g_originalRecords.clear();g_originalBytes=0;g_captureIncomplete=false;g_preserveOriginal=false;}

    void SkipRecord(
        SKSE::SerializationInterface*
            a_intfc,
        std::uint32_t a_length)
    {
        if (!a_intfc) {
            return;
        }

        std::array<std::byte, 256>
            scratch{};

        std::uint32_t remaining =
            a_length;

        while (remaining > 0) {

            const auto chunk =
                (std::min)(
                    remaining,
                    static_cast<std::uint32_t>(
                        scratch.size()));

            const auto read =
                a_intfc->ReadRecordData(
                    scratch.data(),
                    chunk);

            if (read == 0) {
                break;
            }

            remaining -= read;
        }
    }


    void SaveCallback(
        SKSE::SerializationInterface*
            a_intfc)
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        if (!a_intfc) {
            return;
        }


        if(g_captureIncomplete){logger::error("[Persistence][SAVE_SAFETY_HOLD] Original capture incomplete; no synthesized v1 records. Callback return DOES NOT cancel ESS/co-save save.");return;}
        if(g_preserveOriginal){
            for(const auto& r:g_originalRecords)if(!a_intfc->WriteRecord(r.type,r.version,r.bytes.data(),static_cast<std::uint32_t>(r.bytes.size()))){logger::error("[Persistence][SAVE_SAFETY_HOLD] Original replay write failed; originals retained in RAM; ESS cancellation NOT guaranteed");return;}
            logger::warn("[Persistence][SAVE_SAFETY_HOLD] Original record types/versions/payloads replayed; unvalidated gameplay changes are not serialized");return;
        }
        // Capture TIT1 and CTI1 together before any WriteRecord or external callback.
        const auto saveTitles=TULLBUNG::UIProfiles::CaptureSaveTitles(TULLBUNG::TitleRuntime::GetRuntime());

        {
            const auto length =
                static_cast<std::uint32_t>(
                    sizeof(g_inquisitor));

            if (!a_intfc->WriteRecord(
                    kInquisitorRecord,
                    kInquisitorVersion,
                    &g_inquisitor,
                    length)) {

                logger::error(
                    "[Persistence] INQ1 save FAILED");

                return;
            }

            logger::info(
                "[Persistence] INQ1 save | MonsterKills={}",
                g_inquisitor.monsterKills);
        }

        {
            const auto length =
                static_cast<std::uint32_t>(
                    sizeof(g_void));

            if (!a_intfc->WriteRecord(
                    kVoidRecord,
                    kVoidVersion,
                    &g_void,
                    length)) {

                logger::error(
                    "[Persistence] VOD1 save FAILED");

                return;
            }

            logger::info(
                "[Persistence] VOD1 save | Void={}",
                static_cast<std::uint32_t>(
                    g_void.voidCount));
        }

        {
            TULLBUNG::BuildState::PersistentState snapshot{};
            TULLBUNG::BuildState::ExportPersistentState(snapshot);
            const auto record=TULLBUNG::VersionedBuildRecord::Encode(snapshot,TULLBUNG::BuildState::GetRetainedQAState());
            if(!a_intfc->WriteRecord(kBuildRecord,record.version,record.bytes.data(),record.length)){logger::error("[Persistence] BLD1 save FAILED");return;}
            logger::info("[Persistence] BLD1 save | Version={} | HighestLevel={} | Bytes={} | RetainedQAJP={}",record.version,snapshot.highestPlayerLevelSeen,record.length,TULLBUNG::BuildState::GetRetainedQAState().spent);
        }

        {
            ClassProgressRecord
                classProgress{};

            classProgress.
                holyStrikeOvercastIndex =
                    TULLBUNG::Classes::
                        Paladin::
                            GetHolyStrikeOvercastIndex();

            classProgress.
                quietProcessingReady =
                    TULLBUNG::Classes::
                        Hunter::
                            IsQuietProcessingReady() ?
                                1 :
                                0;

            const auto length =
                static_cast<std::uint32_t>(
                    sizeof(classProgress));

            if (!a_intfc->WriteRecord(
                    kClassRecord,
                    kClassVersion,
                    &classProgress,
                    length)) {

                logger::error(
                    "[Persistence] CLS1 save FAILED");

                return;
            }

            logger::info(
                "[Persistence] CLS1 save | HolyStrikeStage={} | QuietReady={}",
                static_cast<std::uint32_t>(
                    classProgress.
                        holyStrikeOvercastIndex),
                classProgress.
                    quietProcessingReady != 0);
        }
        {
            const auto payload =
                TULLBUNG::TitleSystem::
                    PersistenceCodec::Encode(
                        saveTitles.title);

            if (payload.size() >
                static_cast<std::size_t>(
                    (std::numeric_limits<
                        std::uint32_t>::max)())) {

                logger::error(
                    "[Persistence] TIT1 save FAILED | Payload too large");

                return;
            }

            const auto length =
                static_cast<std::uint32_t>(
                    payload.size());

            if (!a_intfc->WriteRecord(
                    kTitleRecord,
                    kTitleVersion,
                    payload.data(),
                    length)) {

                logger::error(
                    "[Persistence] TIT1 save FAILED");

                return;
            }

            logger::info(
                "[Persistence] TIT1 save | Bytes={}",
                length);
        }

        {
            const auto payload =
                TULLBUNG::TitleSystem::PersistenceCodec::EncodeCustom(
                    saveTitles.custom);

            if (payload.empty() ||
                payload.size() > kMaxCustomTitlePayload) {
                logger::error(
                    "[Persistence] CTI1 save FAILED | Invalid payload");
                return;
            }

            const auto length =
                static_cast<std::uint32_t>(payload.size());

            if (!a_intfc->WriteRecord(
                    kCustomTitleRecord,
                    kCustomTitleVersion,
                    payload.data(),
                    length)) {
                logger::error(
                    "[Persistence] CTI1 save FAILED");
                return;
            }

            logger::info(
                "[Persistence] CTI1 save | Bytes={}",
                length);
        }
    }


    void LoadCallback(
        SKSE::SerializationInterface*
            a_intfc)
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        TULLBUNG::UIProfiles::InvalidateSession();
        TULLBUNG::UIProfiles::ResetSaveTitleSnapshotForLoad();
        ClearOriginalRecords();
        // Remain fail-closed if GetNextRecordInfo or an unexpected decoder throws.
        g_captureIncomplete=true;bool captureFailed=false;
        if (!a_intfc) {
            g_captureIncomplete=true;
            return;
        }

        g_pending = {};

        std::uint32_t type{};
        std::uint32_t version{};
        std::uint32_t length{};

        while (
            a_intfc->GetNextRecordInfo(
                type,
                version,
                length)
        ) {
            if(g_originalRecords.size()>=kMaxOriginalRecords||length>kMaxOriginalBytes-g_originalBytes){captureFailed=true;SkipRecord(a_intfc,length);continue;}
            try{
                OriginalRecord r;r.type=type;r.version=version;r.bytes.resize(length);
                const auto count=a_intfc->ReadRecordData(r.bytes.data(),length);
                if(count!=length){captureFailed=true;continue;}
                if(std::any_of(g_originalRecords.begin(),g_originalRecords.end(),[&](const auto& old){return old.type==type;}))g_preserveOriginal=true;
                g_originalRecords.push_back(std::move(r));g_originalBytes+=length;
            }catch(...){captureFailed=true;SkipRecord(a_intfc,length);continue;}
            auto& image=g_originalRecords.back();std::size_t cursor=0;
            const auto recordRead=[&](void* out,std::uint32_t count){auto n=(std::min)(static_cast<std::size_t>(count),image.bytes.size()-cursor);if(n)std::memcpy(out,image.bytes.data()+cursor,n);cursor+=n;return static_cast<std::uint32_t>(n);};

            if (
                type == kInquisitorRecord &&
                version ==
                    kInquisitorVersion &&
                length ==
                    sizeof(
                        g_pending.inquisitor)
            ) {
                TULLBUNG::Persistence::
                    InquisitorProgress loaded{};

                const auto read =
                    recordRead(
                        &loaded,
                        static_cast<std::uint32_t>(
                            sizeof(loaded)));

                if (read != sizeof(loaded)) {
                    logger::error(
                        "[Persistence] INQ1 load FAILED | Read={} | Expected={}",
                        read,
                        sizeof(loaded));

                    continue;
                }

                if(loaded.monsterKills>kMaxCountedMonsterKills)continue;
                loaded.monsterKills =
                    (std::min)(
                        loaded.monsterKills,
                        kMaxCountedMonsterKills);

                g_pending.inquisitor =
                    loaded;

                g_pending.hasInquisitor =
                    true;
                image.accepted=true;

                logger::info(
                    "[Persistence] INQ1 load PENDING | MonsterKills={}",
                    loaded.monsterKills);

                continue;
            }

            if (
                type == kVoidRecord &&
                version ==
                    kVoidVersion &&
                length ==
                    sizeof(
                        g_pending.voidProgress)
            ) {
                TULLBUNG::Persistence::
                    VoidProgress loaded{};

                const auto read =
                    recordRead(
                        &loaded,
                        static_cast<std::uint32_t>(
                            sizeof(loaded)));

                if (read != sizeof(loaded)) {
                    logger::error(
                        "[Persistence] VOD1 load FAILED | Read={} | Expected={}",
                        read,
                        sizeof(loaded));

                    continue;
                }

                if(loaded.voidCount>kMaxVoidCount)continue;
                loaded.voidCount =
                    (std::min)(
                        loaded.voidCount,
                        kMaxVoidCount);

                g_pending.voidProgress =
                    loaded;

                g_pending.hasVoid =
                    true;
                image.accepted=true;

                logger::info(
                    "[Persistence] VOD1 load PENDING | Void={}",
                    static_cast<std::uint32_t>(
                        loaded.voidCount));

                continue;
            }

            if(type==kBuildRecord){
                TULLBUNG::BuildState::PersistentState converted{};
                TULLBUNG::BuildState::RetainedQAState retained{};
                if(!TULLBUNG::VersionedBuildRecord::Decode(version,std::span<const std::byte>(image.bytes),converted,retained))continue;
                g_pending.build=converted;g_pending.retainedQA=retained;g_pending.hasBuild=true;image.accepted=true;
                logger::info("[Persistence] BLD1 load PENDING | Version={} | HighestLevel={} | Bytes={} | RetainedQAJP={}",version,converted.highestPlayerLevelSeen,length,retained.spent);
                continue;
            }

            if (
                type == kClassRecord &&
                version ==
                    kClassVersion &&
                length ==
                    sizeof(
                        g_pending.classProgress)
            ) {
                ClassProgressRecord loaded{};

                const auto read =
                    recordRead(
                        &loaded,
                        static_cast<std::uint32_t>(
                            sizeof(loaded)));

                if (read != sizeof(loaded)) {
                    logger::error(
                        "[Persistence] CLS1 load FAILED | Read={} | Expected={}",
                        read,
                        sizeof(loaded));

                    continue;
                }

                if (
                    loaded.
                        holyStrikeOvercastIndex >
                        kMaxHolyStrikeOvercastIndex ||
                    loaded.
                        quietProcessingReady > 1
                ) {
                    logger::error(
                        "[Persistence] CLS1 invalid | HolyStrikeStage={} | QuietReady={}",
                        static_cast<std::uint32_t>(
                            loaded.
                                holyStrikeOvercastIndex),
                        static_cast<std::uint32_t>(
                            loaded.
                                quietProcessingReady));

                    continue;
                }

                g_pending.classProgress =
                    loaded;

                g_pending.hasClass =
                    true;
                image.accepted=true;

                logger::info(
                    "[Persistence] CLS1 load PENDING | HolyStrikeStage={} | QuietReady={}",
                    static_cast<std::uint32_t>(
                        loaded.
                            holyStrikeOvercastIndex),
                    loaded.
                        quietProcessingReady != 0);

                continue;
            }

            if (
                type == kCustomTitleRecord &&
                version == kCustomTitleVersion
            ) {
                const auto minimum =
                    8 + TULLBUNG::TitleSystem::kCustomTitleCount * 10;

                if (length < minimum ||
                    length > kMaxCustomTitlePayload) {
                    logger::error(
                        "[Persistence] CTI1 rejected | Length={}",
                        length);

                    /* Already captured the entire record. */
                    continue;
                }

                std::vector<std::byte> payload(length);

                const auto read =
                    recordRead(
                        payload.data(),
                        length);

                if (read != length) {
                    logger::error(
                        "[Persistence] CTI1 load FAILED | Read={} | Expected={}",
                        read,
                        length);
                    continue;
                }

                TULLBUNG::TitleSystem::
                    CustomTitleRecords loaded{};

                const auto status =
                    TULLBUNG::TitleSystem::PersistenceCodec::
                        DecodeCustom(
                            std::span<const std::byte>{
                                payload.data(),
                                payload.size()
                            },
                            loaded);

                if (status !=
                    TULLBUNG::TitleSystem::PersistenceCodec::
                        DecodeStatus::kOK) {
                    logger::error(
                        "[Persistence] CTI1 decode FAILED | Status={} | Bytes={}",
                        static_cast<std::uint32_t>(status),
                        length);
                    continue;
                }

                if(TULLBUNG::TitleSystem::PersistenceCodec::EncodeCustom(loaded)!=image.bytes)continue;
                g_pending.customTitles = loaded;
                g_pending.hasCustomTitle = true;
                image.accepted=true;

                logger::info(
                    "[Persistence] CTI1 load PENDING | Bytes={}",
                    length);
                continue;
            }

            if (
                type == kTitleRecord &&
                version == kTitleVersion
            ) {
                std::vector<std::byte>
                    payload(length);

                const auto read =
                    recordRead(
                        payload.data(),
                        length);

                if (read != length) {
                    logger::error(
                        "[Persistence] TIT1 load FAILED | Read={} | Expected={}",
                        read,
                        length);

                    continue;
                }

                TULLBUNG::TitleSystem::
                    PersistedState loaded{};

                const auto status =
                    TULLBUNG::TitleSystem::
                        PersistenceCodec::Decode(
                            std::span<const std::byte>{
                                payload.data(),
                                payload.size()
                            },
                            loaded);

                if (status !=
                    TULLBUNG::TitleSystem::
                        PersistenceCodec::
                            DecodeStatus::kOK) {

                    logger::error(
                        "[Persistence] TIT1 decode FAILED | Status={} | Bytes={}",
                        static_cast<std::uint32_t>(
                            status),
                        length);

                    continue;
                }

                if(TULLBUNG::TitleSystem::PersistenceCodec::Encode(loaded)!=image.bytes)continue;
                g_pending.title =
                    loaded;

                g_pending.hasTitle =
                    true;
                image.accepted=true;

                logger::info(
                    "[Persistence] TIT1 load PENDING | Bytes={}",
                    length);

                continue;
            }
            logger::warn(
                "[Persistence] Record skipped | Type={:08X} | Version={} | Length={}",
                type,
                version,
                length);

            /* Already captured the entire record. */
        }
        g_captureIncomplete=captureFailed;
        if(g_captureIncomplete||!g_pending.hasBuild||std::any_of(g_originalRecords.begin(),g_originalRecords.end(),[](const auto& r){return !r.accepted;}))g_preserveOriginal=true;
        if(g_preserveOriginal)logger::error("[Persistence][SAVE_SAFETY_HOLD] Unsupported/corrupt/duplicate/missing BLD1 record; apply blocked and original replay selected");
    }

    void RevertCallback(
        SKSE::SerializationInterface*)
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        TULLBUNG::UIProfiles::InvalidateSession();
        TULLBUNG::UIProfiles::ResetSaveTitleSnapshotForLoad();
        ClearOriginalRecords();
        g_inquisitor = {};
        g_void = {};
        g_pending = {};

        // Prevent state from a previously loaded save leaking into
        // the save that is about to be loaded.
        TULLBUNG::BuildState::
            ResetForNewGame();

        TULLBUNG::TitleRuntime::
            ResetDefaults();

        logger::info(
            "[Persistence] Revert | INQ1 reset | VOD1 reset | BLD1 reset | CLS1 pending reset");
    }}


namespace TULLBUNG::Persistence
{
    bool IsSaveSafetyHeld() noexcept {std::lock_guard lock(TULLBUNG::UIProfiles::RuntimeMutex());return g_preserveOriginal||g_captureIncomplete;}

    bool Initialize() noexcept
    {
        auto* serialization =
            SKSE::GetSerializationInterface();

        if (!serialization) {

            logger::error(
                "[Persistence] SerializationInterface = MISSING");

            return false;
        }

        serialization->SetUniqueID(
            kSerializationID);

        serialization->SetSaveCallback(
            SaveCallback);

        serialization->SetLoadCallback(
            LoadCallback);

        serialization->SetRevertCallback(
            RevertCallback);

        logger::info(
            "[Persistence] Serialization = READY | ID=TULL");

        return true;
    }


    void ApplyLoadedState() noexcept
    {
        std::lock_guard profileOwnerLock(TULLBUNG::UIProfiles::RuntimeMutex());
        TULLBUNG::UIProfiles::InvalidateSession();
        if(g_preserveOriginal||g_captureIncomplete){logger::error("[Persistence][SAVE_SAFETY_HOLD] ApplyLoadedState blocked; no partial record application");g_pending={};return;}
        const bool hadBuildRecord =
            g_pending.hasBuild;

        bool buildRestored{};

        if (g_pending.hasBuild) {
            buildRestored =
                TULLBUNG::BuildState::
                    RestorePersistentState(
                        g_pending.build);

            if (!buildRestored) {
                logger::error(
                    "[Persistence] BLD1 apply FAILED");
                g_preserveOriginal=true;g_pending={};
                logger::error("[Persistence][SAVE_SAFETY_HOLD] Semantic BLD1 rejection; other records not applied; originals retained");return;
            }
        }

        // Adopt extension and JP reservation only after the complete core restore succeeded.
        if(buildRestored)TULLBUNG::BuildState::SetRetainedQAState(g_pending.retainedQA);

        if (g_pending.hasInquisitor) {
            g_inquisitor =
                g_pending.inquisitor;
        }

        if (g_pending.hasVoid) {
            SetVoidCount(
                g_pending.
                    voidProgress.
                        voidCount);
        }

        if (g_pending.hasClass) {
            TULLBUNG::Classes::Paladin::
                RestoreHolyStrikeOvercastIndex(
                    g_pending.
                        classProgress.
                            holyStrikeOvercastIndex);

            TULLBUNG::Classes::Hunter::
                RestoreQuietProcessingReady(
                    g_pending.
                        classProgress.
                            quietProcessingReady != 0);
        }

        TULLBUNG::TitleRuntime::
            EstablishBaselineFromBuildState();

        // BLD1 present means the final runtime passive authority must
        // follow the loaded BuildState. A rejected/corrupt BLD1 fails
        // closed against the reverted empty BuildState.
        if (hadBuildRecord) {
            TULLBUNG::ClassRuntime::
                OnBuildStateChanged();
        }

        // Custom IDs must exist before TIT1 restores selectedTitleID.
        if (g_pending.hasCustomTitle) {
            TULLBUNG::TitleRuntime::GetRuntime().
                ApplyCustomTitleRecords(
                    g_pending.customTitles);
        }

        if (g_pending.hasTitle) {
            TULLBUNG::TitleRuntime::
                GetRuntime().
                    ApplyPersistedState(
                        g_pending.title);
        }

        logger::info(
            "[Persistence] CTI1 apply | Present={}",
            g_pending.hasCustomTitle);

        const auto qaRestoredRecords =
            TULLBUNG::TitleRuntime::GetRuntime().
                MakeCustomTitleRecords();

        std::uint32_t qaRestoredCount = 0;
        std::uint32_t qaRestoredFavorites = 0;

        for (const auto& qaRecord : qaRestoredRecords) {
            if (qaRecord.present) {
                ++qaRestoredCount;
                if (qaRecord.favorite) {
                    ++qaRestoredFavorites;
                }
            }
        }

        const auto& qaRestoredSlots =
            TULLBUNG::TitleRuntime::GetRuntime().Slots();

        logger::info(
            "[S18QA][CTI1_RESTORE] CTI1={} TIT1={} Defined={} Favorites={} IDs={},{},{},{},{},{}",
            g_pending.hasCustomTitle,
            g_pending.hasTitle,
            qaRestoredCount,
            qaRestoredFavorites,
            qaRestoredSlots[0].selectedTitleID,
            qaRestoredSlots[1].selectedTitleID,
            qaRestoredSlots[2].selectedTitleID,
            qaRestoredSlots[3].selectedTitleID,
            qaRestoredSlots[4].selectedTitleID,
            qaRestoredSlots[5].selectedTitleID);

        logger::info(
            "[Persistence] TIT1 apply | Present={} | RecentBatch={}",
            g_pending.hasTitle,
            TULLBUNG::TitleRuntime::
                GetRuntime().
                    LastAcquisitionBatch().
                        size());
        logger::info(
            "[Persistence] ApplyLoadedState = COMPLETE | INQ1={} | VOD1={} | BLD1={} | BLD1Restored={} | CLS1={}",
            g_pending.hasInquisitor,
            g_pending.hasVoid,
            g_pending.hasBuild,
            buildRestored,
            g_pending.hasClass);

        g_pending = {};
    }

    const InquisitorProgress&
        GetInquisitorProgress() noexcept
    {
        return g_inquisitor;
    }


    void SetInquisitorMonsterKills(
        std::uint32_t a_kills) noexcept
    {
        g_inquisitor.monsterKills =
            (std::min)(
                a_kills,
                kMaxCountedMonsterKills);
    }


    void ResetInquisitorProgress() noexcept
    {
        g_inquisitor = {};
    }


    const VoidProgress&
        GetVoidProgress() noexcept
    {
        return g_void;
    }


    void SetVoidCount(
        std::uint8_t a_count) noexcept
    {
        const auto previous =
            g_void.voidCount;

        g_void.voidCount =
            (std::min)(
                a_count,
                kMaxVoidCount);

        if (g_void.voidCount != previous) {
            TULLBUNG::PrismaBridge::
                NotifyVoidCountChanged();
        }
    }

    void ResetVoidProgress() noexcept
    {
        g_void = {};
    }
}