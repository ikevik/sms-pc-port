// Super Mario Sunburn's own code module, rebuilt from the released SuperMarioSunburn.kxe
// (v1.0, built 2025-02-07). Not the authors' source, which is not public: the behaviour
// below was read from the module's machine code (docs/SUNBURN.md). The Yoshi patches in it
// are Super Mario Eclipse's yoshi/green_mode.cpp (GPL-3.0), compiled beside this file
// (platform/mods/eclipse/lib/CMakeLists.txt, SMS_SUNBURN); the rest follows that module's
// settings, patches and callbacks.
#include <Dolphin/CARD.h>
#include <Dolphin/OS.h>

#include <SMS/Manager/FlagManager.hxx>
#include <SMS/MoveBG/NozzleBox.hxx>
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/GameSequence.hxx>
#include <SMS/System/MarDirector.hxx>

#include <BetterSMS/game.hxx>
#include <BetterSMS/memory.hxx>
#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/settings.hxx>
#include <BetterSMS/stage.hxx>

using namespace BetterSMS;

namespace {

enum WinCondition { WIN_SEVENTY_SHINES, WIN_ALL_SHADOW_MARIO };

// Shines (flag 0x40000) needed to unlock New Game Plus, and for the 70 Shines ending.
constexpr u32 ShineFlag         = 0x40000;
constexpr u32 NewGamePlusShines = 120;
constexpr u32 EndingShines      = 70;

// Flags the module reads (Super Mario Eclipse's meanings).
constexpr u32 FlagCoronaBeaten = 0x10077;  // Corona Mountain beaten
constexpr u32 FlagYoshiUnlock  = 0x1038F;
constexpr u32 FlagMareGate     = 0x50004;
constexpr u32 FlagEndingSeen   = 0x103AE;

constexpr u8 NozzleHover = 4;
constexpr u8 NozzleTurbo = 5;

bool sNewGamePlusUnlocked = false;  // saved with the setting; set once the shine count passes 119
bool sNewGamePlus         = false;
int sWinCondition         = WIN_SEVENTY_SHINES;

// "New Game Plus": a back nozzle (Turbo in Hover's place), half the health.
class NewGamePlusSetting final : public Settings::BoolSetting {
public:
    NewGamePlusSetting() : BoolSetting("New Game Plus", &sNewGamePlus) {
        mValueChangedCB = valueChanged;
    }
    ~NewGamePlusSetting() override {}

    bool isUnlocked() const override { return sNewGamePlusUnlocked; }

    void load(JSUMemoryInputStream &in) override {
        u8 unlocked;
        in.read(&unlocked, 1);
        sNewGamePlusUnlocked = unlocked != 0;
        BoolSetting::load(in);
    }
    void save(JSUMemoryOutputStream &out) override {
        out.write(&sNewGamePlusUnlocked, 1);
        BoolSetting::save(out);
    }

    // TWaterGun::init's `li r8, 4` (the starting second nozzle).
    static void valueChanged(void *old, void *cur, ValueKind) {
        const bool on = *reinterpret_cast<bool *>(cur);
        PowerPC::writeU32(reinterpret_cast<u32 *>(SMS_PORT_REGION(0x8026A3C8, 0, 0, 0)),
                          on ? 0x39000005 : 0x39000004);
    }
};

// "Win Condition": how the game ends.
class WinConditionSetting final : public Settings::IntSetting {
public:
    WinConditionSetting() : IntSetting("Win Condition", &sWinCondition) {
        mValueRange = {WIN_SEVENTY_SHINES, WIN_ALL_SHADOW_MARIO, 1};
    }
    ~WinConditionSetting() override {}

    void getValueName(char *dst) const override {
        if (getInt() == WIN_SEVENTY_SHINES)
            strncpy(dst, "70 Shines", 17);
        else
            strncpy(dst, "All Shadow Mario", 17);
    }
};

NewGamePlusSetting gNewGamePlusSetting;
WinConditionSetting gWinConditionSetting;

Settings::SettingsGroup gSettingsGroup(1, 0, Settings::Priority::GAME);

// The memory card banner and icons (blank: the port has no card menu to show them).
const u8 gSaveBnr[0x20 + 0xE00]      = {};
const u8 gSaveIcon[0x20 + 0x500 * 2] = {};

BetterSMS::ModuleInfo &moduleInfo() {
    static BetterSMS::ModuleInfo info("Super Mario Sunburn", 1, 0, &gSettingsGroup);
    return info;
}

// --- callbacks ---------------------------------------------------------------------------

// Corona Mountain beaten: Yoshi is yours.
void forceYoshiUnlock(TMarDirector *director) {
    if (TFlagManager::smInstance->getBool(FlagCoronaBeaten))
        TFlagManager::smInstance->setBool(true, FlagYoshiUnlock);
}

// Entering the area (scene 1 = Delfino Plaza's first episode state): the Mare gate opens.
void openMareGate(TMarDirector *director) {
    if (director->mAreaID == 1)
        TFlagManager::smInstance->setBool(true, FlagMareGate);
}

void unlockNewGamePlus(TMarDirector *director) {
    if (!sNewGamePlusUnlocked && TFlagManager::smInstance->getFlag(ShineFlag) >= NewGamePlusShines)
        sNewGamePlusUnlocked = true;
}

// New Game Plus: half the stage's maximum health (at most 4 now), a back nozzle as the second one.
void applyNewGamePlus(TMario *player, bool isMario) {
    if (!isMario || !sNewGamePlus)
        return;
    auto *config = Stage::getStageConfiguration();
    if (!config)
        return;

    player->mDeParams.mHPMax.set(config->mPlayerMaxHealth.get() >> 1);
    if (player->mHealth > 4)
        player->mHealth = 4;

    u8 second = player->mFludd->mSecondNozzle;
    if ((second | NozzleHover) == NozzleHover)
        player->mFludd->mSecondNozzle = NozzleTurbo;
}

void initModule() {
    moduleInfo();
    gSettingsGroup.addSetting(&gNewGamePlusSetting);
    gSettingsGroup.addSetting(&gWinConditionSetting);
    {
        auto &saveInfo        = gSettingsGroup.getSaveInfo();
        saveInfo.mSaveName    = Settings::getGroupName(gSettingsGroup);
        saveInfo.mBlocks      = 1;
        saveInfo.mGameCode    = 'GMSE';
        saveInfo.mCompany     = 0x3033;  // '03'
        saveInfo.mBannerFmt   = CARD_BANNER_CI;
        saveInfo.mBannerImage = reinterpret_cast<const ResTIMG *>(gSaveBnr);
        saveInfo.mIconFmt     = CARD_ICON_CI;
        saveInfo.mIconSpeed   = CARD_SPEED_FAST;
        saveInfo.mIconCount   = 2;
        saveInfo.mIconTable   = reinterpret_cast<const ResTIMG *>(gSaveIcon);
        saveInfo.mSaveGlobal  = true;
    }
    BetterSMS::registerModule(moduleInfo());

    // The module turns BetterSunshineEngine's bug fixes off by default.
    BetterSMS::getBugFixesSetting()->setBool(false);

    Stage::addInitCallback(forceYoshiUnlock);
    Stage::addInitCallback(openMareGate);
    Stage::addUpdateCallback(unlockNewGamePlus);
    Player::addUpdateCallback(applyNewGamePlus);
}

// --- patches to the retail game ----------------------------------------------------------

// A red coin also counts as two coins in the stage's gold coin total.
// TCoinRed::taken +0x2c: the call of TFlagManager::incFlag.
void incRedCoinFlags(TFlagManager *manager, u32 flag, s32 num) {
    manager->incFlag(flag, num);
    manager->incGoldCoinFlag(SMS_getShineStage__FUc(gpApplication.mCurrentScene.mAreaID), 2);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x801BE454, 0, 0, 0), incRedCoinFlags);

// TMarDirector::changeState +0x3b0: the call of TGameSequence::set that picks the next area.
// With Z held the next area is the one just left (episode 0xFF: the area's own).
void setNextArea(TGameSequence *next, u8 area, u8 episode, u16 flag) {
    if (gpApplication.mGamePads[0]->mButtons.mInput & TMarioGamePad::Z) {
        episode = 0xFF;
        area    = gpApplication.mPrevScene.mAreaID;
        if (area == TGameSequence::AREA_DOLPIC && !SMS_isExMap__Fv())
            area = gpApplication.mCurrentScene.mAreaID;
    }
    next->set(area, episode, JDrama::TFlagT<u16>(flag));
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80299230, 0, 0, 0), setNextArea);

// TNozzleBox::load +0x84: the box's nozzle, colour and name (a back nozzle with New Game Plus).
void initNozzleBox() {
    TNozzleBox *box;
    SMS_FROM_GPR(30, box);

    if (!sNewGamePlus) {
        box->mNozzleType = NozzleHover;
        box->mMatColor.r = 0;
        box->mMatColor.g = 0;
        box->mMatColor.b = 0xFF;
    } else {
        box->mNozzleType = NozzleTurbo;
        box->mBoxName    = "back_nozzle_item";
        box->mMatColor.r = 0x5A;
        box->mMatColor.g = 0x5A;
        box->mMatColor.b = 0x78;
    }
}
SMS_PATCH_BL(SMS_PORT_REGION(0x801BB308, 0, 0, 0), initNozzleBox);
SMS_WRITE_32(SMS_PORT_REGION(0x801BB30C, 0, 0, 0), 0x480000CC);  // skip the rest of the setup

// TWaterGun::changeNozzle +0x8c and +0xa0: the stores of the second and the current nozzle.
void storeSecondNozzle() {
    TWaterGun *fludd;
    u8 nozzle;
    SMS_FROM_GPR(31, fludd);
    SMS_FROM_GPR(30, nozzle);
    if (nozzle == NozzleHover && sNewGamePlus)
        nozzle = NozzleTurbo;
    fludd->mSecondNozzle = nozzle;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026A1F0, 0, 0, 0), storeSecondNozzle);

void storeCurrentNozzle() {
    TWaterGun *fludd;
    u8 nozzle;
    SMS_FROM_GPR(31, fludd);
    SMS_FROM_GPR(30, nozzle);
    if (nozzle == NozzleHover && sNewGamePlus)
        nozzle = NozzleTurbo;
    fludd->mCurrentNozzle = nozzle;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026A204, 0, 0, 0), storeCurrentNozzle);

// TModelGate::loadAfter +0x810: a gate is open once Corona Mountain is beaten.
u32 isGateOpen(u32 opened) {
    return (TFlagManager::smInstance->getBool(FlagCoronaBeaten) | opened) & 1;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x801EC858, 0, 0, 0), isGateOpen);
SMS_WRITE_32(SMS_PORT_REGION(0x801EC85C, 0, 0, 0), 0x28030001);  // cmplwi r3, 1

// TMarDirector::moveStage +0x128: the call of decideNextScenario. The 70 Shines ending: scenario 9.
// (The module calls the game's function; the port's hook passes its answer in as `chosen`,
// since the decomp keeps decideNextScenario private to that file.)
u8 decideScenario(u8 area, u8 chosen) {
    if (!TFlagManager::smInstance->getBool(FlagEndingSeen) && sWinCondition == WIN_SEVENTY_SHINES &&
        TFlagManager::smInstance->getFlag(ShineFlag) >= EndingShines)
        return 9;
    return chosen;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802976AC, 0, 0, 0), decideScenario);

}  // namespace

KURIBO_MODULE_BEGIN("Super Mario Sunburn", "EpicWade", "v1.0") {
    KURIBO_EXECUTE_ON_LOAD { initModule(); }
}
KURIBO_MODULE_END()
