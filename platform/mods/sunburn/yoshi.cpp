// Super Mario Sunburn's Yoshi: the green Yoshi that does not turn back, rides over water, and
// whose tongue reaches further. Written from the 27 patches and 14 functions in the released
// SuperMarioSunburn.kxe (v1.0), decoded with tools/mods/kxe (docs/SUNBURN.md); each patch below
// names its retail address and the module function it replaces.
#include <SMS/Player/Mario.hxx>
#include <SMS/Player/Watergun.hxx>
#include <SMS/Player/Yoshi.hxx>

#include <BetterSMS/module.hxx>
#include <BetterSMS/player.hxx>
#include <BetterSMS/stage.hxx>

using namespace BetterSMS;

namespace {

// --- what a Yoshi is (module +0xc7c, +0xcb8, +0xcc8, +0xcdc, +0xd40) --------------------------

bool isMounted(TYoshi *yoshi) { return yoshi->mState == TYoshi::MOUNTED; }
bool isGreen(TYoshi *yoshi) { return yoshi->mType == TYoshi::GREEN; }
bool isGreen(TMario *player) { return isGreen(player->mYoshi); }
bool isGreenMounted(TYoshi *yoshi) { return isGreen(yoshi) && isMounted(yoshi); }

// In the water, on a green Yoshi, with A held.
bool isGreenAscendingWater(TMario *player) {
    return player->mAttributes.mIsWater &&
           (player->mController->mButtons.mInput & TMarioGamePad::EButtons::A) &&
           isGreenMounted(player->mYoshi);
}

// --- Yoshi stays green and does not starve ------------------------------------------------------

// TYoshi::entry +0xcc, +0x104 and TYoshi::movement +0x69c: Yoshi's colour and juice tests.
SMS_WRITE_32(SMS_PORT_REGION(0x8026E068, 0, 0, 0), 0x2C000001);
SMS_WRITE_32(SMS_PORT_REGION(0x8026E0A0, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x8026EE14, 0, 0, 0), 0x4800020C);

// TYoshi::movement +0x388, +0x484, +0xaa0 (module +0xd90): the three tests of whether Yoshi
// dies of lack of juice now read "is it not a green Yoshi".
bool isYoshiDie(TMario *player) { return !isGreen(player); }
SMS_PATCH_BL(SMS_PORT_REGION(0x8026EB00, 0, 0, 0), isYoshiDie);
SMS_WRITE_32(SMS_PORT_REGION(0x8026EB04, 0, 0, 0), 0x2C030000);
SMS_WRITE_32(SMS_PORT_REGION(0x8026EB08, 0, 0, 0), 0x41820518);
SMS_PATCH_BL(SMS_PORT_REGION(0x8026EBFC, 0, 0, 0), isYoshiDie);
SMS_WRITE_32(SMS_PORT_REGION(0x8026EC00, 0, 0, 0), 0x2C030000);
SMS_WRITE_32(SMS_PORT_REGION(0x8026EC04, 0, 0, 0), 0x4182041C);
SMS_PATCH_BL(SMS_PORT_REGION(0x8026F218, 0, 0, 0), isYoshiDie);
SMS_WRITE_32(SMS_PORT_REGION(0x8026F21C, 0, 0, 0), 0x2C030000);
SMS_WRITE_32(SMS_PORT_REGION(0x8026F220, 0, 0, 0), 0x41820164);

// TMario::thinkWaterSurface +0x25c (module +0xdb4): Yoshi leaves the water only if not green.
void maybeYoshiDrown(TYoshi *yoshi) {
    if (!isGreen(yoshi))
        disappear__6TYoshiFv(yoshi);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8024F240, 0, 0, 0), maybeYoshiDrown);

// TYoshi::movement +0x98 (module +0x1074; reads r30 = the Yoshi): spraying with a mounted Yoshi
// costs it juice, by the amount sprayed, and nothing else does.
void fixYoshiJuiceDecrement() {
    TYoshi *yoshi;
    SMS_FROM_GPR(30, yoshi);
    TWaterGun *fludd = yoshi->mMario->mFludd;
    if (!fludd->mIsEmitWater)
        return;
    if (yoshi->mState != TYoshi::MOUNTED)
        return;
    yoshi->mCurJuice -= fludd->mEmitInfo->mNum.get();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8026E810, 0, 0, 0), fixYoshiJuiceDecrement);

// --- riding -------------------------------------------------------------------------------------

// TMario::checkCollision +0x110 (module +0xe00; reads r31 = Mario): Mario can mount Yoshi in the
// air or in the water, if his parameters allow and his size (the stage's multiplier times the
// player parameter at module offset 0x90) is at most 1.5.
bool canMountYoshi() {
    TMario *player;
    SMS_FROM_GPR(31, player);

    auto *config = Stage::getStageConfiguration();
    auto *params = Player::getData(player)->getParams();

    if (!(config->mPlayerSizeMultiplier.get() * params->mThrowPowerMultiplier.get() <= 1.5f))
        return false;
    if (!(player->mState & (static_cast<u32>(TMario::STATE_WATERBORN) |
                            static_cast<u32>(TMario::STATE_AIRBORN))))
        return false;
    return params->mCanRideYoshi.get();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x802810F8, 0, 0, 0), canMountYoshi);
SMS_WRITE_32(SMS_PORT_REGION(0x802810FC, 0, 0, 0), 0x2C030000);
SMS_WRITE_32(SMS_PORT_REGION(0x80281100, 0, 0, 0), 0x807F03F0);
SMS_WRITE_32(SMS_PORT_REGION(0x80281104, 0, 0, 0), 0x38830020);
SMS_WRITE_32(SMS_PORT_REGION(0x80281110, 0, 0, 0), 0x60000000);

// TMario::checkCollision +0x160 (module +0xe98; reads r31 = Mario): the height of Yoshi.
f32 getYoshiYPos() {
    TMario *player;
    SMS_FROM_GPR(31, player);
    return player->mYoshi->mTranslation.y;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80281148, 0, 0, 0), getYoshiYPos);

// TMario::gunExec +0x28c (module +0xea8): Fludd sprays unless Mario is on a green Yoshi.
void canYoshiSpray(TWaterGun *fludd) {
    TYoshi *yoshi = fludd->mMario->mYoshi;
    if (!yoshi)
        return;
    if (!isGreenMounted(yoshi))
        fludd->emit();
}
SMS_PATCH_BL(SMS_PORT_REGION(0x8024E58C, 0, 0, 0), canYoshiSpray);

// TMario::doSwimming +0x228 (module +0xef4): on a green Yoshi in the water, A makes it rise and
// letting go makes it sink, by 0.34375 a frame between -12 and 12.
u32 calcYoshiSwimVelocity(TMario *player, u32 arg) {
    auto *data = Player::getData(player);
    if (data && player->mYoshi && isGreenMounted(player->mYoshi)) {
        f32 speed = data->mYoshiWaterSpeed.y;
        if (player->mController->mButtons.mInput & TMarioGamePad::EButtons::A)
            speed = speed <= 12.0f ? speed + 0.34375f : 12.0f;
        else
            speed = speed >= -12.0f ? speed - 0.34375f : -12.0f;
        data->mYoshiWaterSpeed.y = speed;
        player->mSpeed.y         = speed;
    }
    return player->jumpProcess(arg);
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80273198, 0, 0, 0), calcYoshiSwimVelocity);

// TYoshi::thinkAnimation +0x240 (module +0xfb8; reads r29 = the Yoshi and r30 = the animation):
// the rising animation in the water, and animation 24 becomes 15.
u32 isYoshiWaterFlutter() {
    TYoshi *yoshi;
    u32 animID;
    SMS_FROM_GPR(29, yoshi);
    SMS_FROM_GPR(30, animID);

    if (isGreenAscendingWater(yoshi->mMario))
        animID = 9;
    if ((animID & 0xFFFF) == 24)
        animID = 15;
    return animID;
}
SMS_PATCH_BL(SMS_PORT_REGION(0x80270078, 0, 0, 0), isYoshiWaterFlutter);
SMS_WRITE_32(SMS_PORT_REGION(0x8027007C, 0, 0, 0), 0x7C7E1B78);
SMS_WRITE_32(SMS_PORT_REGION(0x80270080, 0, 0, 0), 0x60000000);
SMS_WRITE_32(SMS_PORT_REGION(0x80270084, 0, 0, 0), 0x60000000);

// TMario::checkWet +0x40 (module +0x101c): the drips of a Yoshi that is mounted and not green.
bool isYoshiValidDrip(TYoshi *yoshi) { return isMounted(yoshi) && !isGreen(yoshi); }
SMS_PATCH_BL(SMS_PORT_REGION(0x8024E788, 0, 0, 0), isYoshiValidDrip);

}  // namespace

// Player update callback (module +0x10b0): a green Yoshi's tongue reaches further. The two
// values are retail's constants at 0x80415F4C and 0x80415F68, which the module rewrites.
void adjustYoshiTongue(TMario *player, bool isMario) {
    if (!player->mYoshi)
        return;
    if (isGreen(player->mYoshi)) {
        *(f32 *)sms_mod_rawdata(0x80415F4C) = 480.0f;
        *(f32 *)sms_mod_rawdata(0x80415F68) = 16384.0f;
    } else {
        *(f32 *)sms_mod_rawdata(0x80415F4C) = 300.0f;
        *(f32 *)sms_mod_rawdata(0x80415F68) = 10000.0f;
    }
}
