#include "bg_misctables.h"
#include <universal/q_shared.h>
#include <qcommon/common.h>
#include <clientscript/cscr_stringlist.h>
#include <clientscript/cscr_vm.h>
#include <client/cl_keys.h>
#include <qcommon/graph.h>

const char *s_vehicleCameraModes[6] = { "first", "chase", "view", "strafe", "horse", "oldtank" };
const char *s_vehicleBoostModes[2] = { "burst", "continuous" };
const char *s_tractionTypeNames[3] = { "TRACTION_TYPE_FRONT", "TRACTION_TYPE_BACK", "TRACTION_TYPE_ALL_WD" };
const char *s_vehicleClassNames[7] = { "4 wheel", "motorcycle", "tank", "plane", "boat", "artillery", "helicopter" };

cspField_t s_vehicleFields[345] =
{
  { "type", offsetof(vehicle_info_t, type), 15 },
  { "remoteControl", offsetof(vehicle_info_t, remoteControl), 6 },
  { "steerWheels", offsetof(vehicle_info_t, steerWheels), 6 },
  { "texureScroll", offsetof(vehicle_info_t, texScroll), 6 },
  { "quadBarrel", offsetof(vehicle_info_t, quadBarrel), 6 },
  { "bulletDamage", offsetof(vehicle_info_t, bulletDamage), 6 },
  { "armorPiercingDamage", offsetof(vehicle_info_t, armorPiercingDamage), 6 },
  { "grenadeDamage", offsetof(vehicle_info_t, grenadeDamage), 6 },
  { "projectileDamage", offsetof(vehicle_info_t, projectileDamage), 6 },
  { "projectileSplashDamage", offsetof(vehicle_info_t, projectileSplashDamage), 6 },
  { "heavyExplosiveDamage", offsetof(vehicle_info_t, heavyExplosiveDamage), 6 },
  { "cameraMode", offsetof(vehicle_info_t, cameraMode), 16 },
  { "autoRecenterOnAccel", offsetof(vehicle_info_t, autoRecenterOnAccel), 6 },
  { "thirdPersonDriver", offsetof(vehicle_info_t, thirdPersonDriver), 6 },
  { "thirdPersonCameraHeightMin", offsetof(vehicle_info_t, thirdPersonCameraHeight), 7 },
  { "thirdPersonCameraPitchMin", offsetof(vehicle_info_t, thirdPersonCameraPitch), 7 },
  { "thirdPersonCameraRange", offsetof(vehicle_info_t, thirdPersonCameraRange), 7 },
  { "thirdPersonCameraHeight", offsetof(vehicle_info_t, thirdPersonCameraHeight[1]), 7 },
  { "thirdPersonCameraPitch", offsetof(vehicle_info_t, thirdPersonCameraPitch[1]), 7 },
  { "thirdPersonCameraMinPitchClamp", offsetof(vehicle_info_t, thirdPersonCameraMinPitchClamp), 7 },
  { "thirdPersonCameraMaxPitchClamp", offsetof(vehicle_info_t, thirdPersonCameraMaxPitchClamp), 7 },
  { "cameraAlwaysAutoCenter", offsetof(vehicle_info_t, cameraAlwaysAutoCenter), 6 },
  { "cameraAutoCenterLerpRate", offsetof(vehicle_info_t, cameraAutoCenterLerpRate), 7 },
  { "cameraAutoCenterMaxLerpRate", offsetof(vehicle_info_t, cameraAutoCenterMaxLerpRate), 7 },
  { "thirdPersonCameraSpringDistance", offsetof(vehicle_info_t, thirdPersonCameraSpringDistance), 7 },
  { "thirdPersonCameraSpringTime", offsetof(vehicle_info_t, thirdPersonCameraSpringTime), 7 },
  { "thirdPersonCameraHandbrakeTurnRateInc", offsetof(vehicle_info_t, thirdPersonCameraHandbrakeTurnRateInc), 7 },
  { "cameraFOV", offsetof(vehicle_info_t, cameraFOV), 7 },
  { "viewInfluence", offsetof(vehicle_info_t, viewInfluence), 7 },
  { "killcamCollision", offsetof(vehicle_info_t, killcamCollision), 6 },
  { "killcamDist", offsetof(vehicle_info_t, killcamDist), 7 },
  { "killcamZDist", offsetof(vehicle_info_t, killcamZDist), 7 },
  { "killcamMinDist", offsetof(vehicle_info_t, killcamMinDist), 7 },
  { "killcamZTargetOffset", offsetof(vehicle_info_t, killcamZTargetOffset), 7 },
  { "killcamFOV", offsetof(vehicle_info_t, killcamFOV), 7 },
  { "killcamNearBlur", offsetof(vehicle_info_t, killcamNearBlur), 7 },
  { "killcamNearBlurStart", offsetof(vehicle_info_t, killcamNearBlurStart), 7 },
  { "killcamNearBlurEnd", offsetof(vehicle_info_t, killcamNearBlurEnd), 7 },
  { "killcamFarBlur", offsetof(vehicle_info_t, killcamFarBlur), 7 },
  { "killcamFarBlurStart", offsetof(vehicle_info_t, killcamFarBlurStart), 7 },
  { "killcamFarBlurEnd", offsetof(vehicle_info_t, killcamFarBlurEnd), 7 },
  { "isDrivable", offsetof(vehicle_info_t, isDrivable), 6 },
  { "numberOfSeats", offsetof(vehicle_info_t, numberOfSeats), 4 },
  { "numberOfGunners", offsetof(vehicle_info_t, numberOfGunners), 4 },
  { "driverControlledGunPos", offsetof(vehicle_info_t, driverControlledGunPos), 4 },
  { "seatSwitchOrder1", offsetof(vehicle_info_t, seatSwitchOrder), 4 },
  { "seatSwitchOrder2", offsetof(vehicle_info_t, seatSwitchOrder[1]), 4 },
  { "seatSwitchOrder3", offsetof(vehicle_info_t, seatSwitchOrder[2]), 4 },
  { "seatSwitchOrder4", offsetof(vehicle_info_t, seatSwitchOrder[3]), 4 },
  { "seatSwitchOrder5", offsetof(vehicle_info_t, seatSwitchOrder[4]), 4 },
  { "seatSwitchOrder6", offsetof(vehicle_info_t, seatSwitchOrder[5]), 4 },
  { "seatSwitchOrder7", offsetof(vehicle_info_t, seatSwitchOrder[6]), 4 },
  { "seatSwitchOrder8", offsetof(vehicle_info_t, seatSwitchOrder[7]), 4 },
  { "seatSwitchOrder9", offsetof(vehicle_info_t, seatSwitchOrder[8]), 4 },
  { "seatSwitchOrder10", offsetof(vehicle_info_t, seatSwitchOrder[9]), 4 },
  { "seatSwitchOrder11", offsetof(vehicle_info_t, seatSwitchOrder[10]), 4 },
  { "texureScrollScale", offsetof(vehicle_info_t, texScrollScale), 7 },
  { "wheelRotRate", offsetof(vehicle_info_t, wheelRotRate), 7 },
  { "extraWheelRotScale", offsetof(vehicle_info_t, extraWheelRotScale), 7 },
  { "enterRadiusDriver", offsetof(vehicle_info_t, entryPointRadius), 7 },
  { "enterRadiusGunner1", offsetof(vehicle_info_t, entryPointRadius[1]), 7 },
  { "enterRadiusGunner2", offsetof(vehicle_info_t, entryPointRadius[2]), 7 },
  { "enterRadiusGunner3", offsetof(vehicle_info_t, entryPointRadius[3]), 7 },
  { "enterRadiusGunner4", offsetof(vehicle_info_t, entryPointRadius[4]), 7 },
  { "maxSpeed", offsetof(vehicle_info_t, maxSpeed), 19 },
  { "accel", offsetof(vehicle_info_t, accel), 19 },
  { "rotRate", offsetof(vehicle_info_t, rotRate), 7 },
  { "rotAccel", offsetof(vehicle_info_t, rotAccel), 7 },
  { "collisionDamage", offsetof(vehicle_info_t, collisionDamage), 7 },
  { "collisionSpeed", offsetof(vehicle_info_t, collisionSpeed), 19 },
  { "suspensionTravel", offsetof(vehicle_info_t, suspensionTravel), 7 },
  { "maxBodyPitch", offsetof(vehicle_info_t, maxBodyPitch), 7 },
  { "maxBodyRoll", offsetof(vehicle_info_t, maxBodyRoll), 7 },
  { "boostMode", offsetof(vehicle_info_t, cameraMode), 17 },
  { "boostSpeedIncrease", offsetof(vehicle_info_t, boostSpeedIncrease), 7 },
  { "boostAccelMultiplier", offsetof(vehicle_info_t, boostAccelMultiplier), 7 },
  { "boostInterval", offsetof(vehicle_info_t, boostInterval), 7 },
  { "boostDuration", offsetof(vehicle_info_t, boostDuration), 7 },
  { "boostFrictionMultiplier", offsetof(vehicle_info_t, boostFrictionMultiplier), 7 },
  { "boostContinuousRegenCooldown", offsetof(vehicle_info_t, boostContinuousRegenCooldown), 7 },
  { "boostContinuousRegenAmount", offsetof(vehicle_info_t, boostContinuousRegenAmount), 7 },
  { "heliCollisionScalar", offsetof(vehicle_info_t, heliCollisionScalar), 7 },
  { "viewPitchOffset", offsetof(vehicle_info_t, viewPitchOffset), 7 },
  { "tiltFromAccelerationPitch", offsetof(vehicle_info_t, tiltFromAcceleration), 7 },
  { "tiltFromAccelerationRoll", offsetof(vehicle_info_t, tiltFromAcceleration[1]), 7 },
  { "tiltFromDecelerationPitch", offsetof(vehicle_info_t, tiltFromDeceleration), 7 },
  { "tiltFromDecelerationRoll", offsetof(vehicle_info_t, tiltFromDeceleration[1]), 7 },
  { "tiltFromVelocityPitch", offsetof(vehicle_info_t, tiltFromVelocity), 7 },
  { "tiltFromVelocityRoll", offsetof(vehicle_info_t, tiltFromVelocity[1]), 7 },
  { "tiltSpeedPitch", offsetof(vehicle_info_t, tiltSpeed), 7 },
  { "tiltSpeedRoll", offsetof(vehicle_info_t, tiltSpeed[1]), 7 },
  { "tracerOffsetForward", offsetof(vehicle_info_t, tracerOffset), 7 },
  { "tracerOffsetUp", offsetof(vehicle_info_t, tracerOffset[1]), 7 },
  { "turretWeapon", offsetof(vehicle_info_t, turretWeapon), 0 },
  { "turretHorizSpanLeft", offsetof(vehicle_info_t, turretViewLimits.horizSpanLeft), 7 },
  { "turretHorizSpanRight", offsetof(vehicle_info_t, turretViewLimits.horizSpanRight), 7 },
  { "turretVertSpanUp", offsetof(vehicle_info_t, turretViewLimits.vertSpanUp), 7 },
  { "turretVertSpanDown", offsetof(vehicle_info_t, turretViewLimits.vertSpanDown), 7 },
  { "turretRotRate", offsetof(vehicle_info_t, turretRotRate), 7 },
  { "turretClampPlayerView", offsetof(vehicle_info_t, turretClampPlayerView), 6 },
  { "turretTag1", offsetof(vehicle_info_t, turretTag1), 0 },
  { "turretTag2", offsetof(vehicle_info_t, turretTag2), 0 },
  { "turretTag3", offsetof(vehicle_info_t, turretTag3), 0 },
  { "turretTag4", offsetof(vehicle_info_t, turretTag4), 0 },
  { "gunnerWeapon", offsetof(vehicle_info_t, gunnerWeapon), 0 },
  { "gunnerWeapon1", offsetof(vehicle_info_t, gunnerWeapon[1]), 0 },
  { "gunnerWeapon2", offsetof(vehicle_info_t, gunnerWeapon[2]), 0 },
  { "gunnerWeapon3", offsetof(vehicle_info_t, gunnerWeapon[3]), 0 },
  { "gunnerRotRate", offsetof(vehicle_info_t, gunnerRotRate), 7 },
  { "passenger1HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[0].horizSpanLeft), 7 },
  { "passenger1HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[0].horizSpanRight), 7 },
  { "passenger1VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[0].vertSpanUp), 7 },
  { "passenger1VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[0].vertSpanDown), 7 },
  { "passenger2HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[1].horizSpanLeft), 7 },
  { "passenger2HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[1].horizSpanRight), 7 },
  { "passenger2VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[1].vertSpanUp), 7 },
  { "passenger2VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[1].vertSpanDown), 7 },
  { "passenger3HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[2].horizSpanLeft), 7 },
  { "passenger3HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[2].horizSpanRight), 7 },
  { "passenger3VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[2].vertSpanUp), 7 },
  { "passenger3VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[2].vertSpanDown), 7 },
  { "passenger4HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[3].horizSpanLeft), 7 },
  { "passenger4HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[3].horizSpanRight), 7 },
  { "passenger4VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[3].vertSpanUp), 7 },
  { "passenger4VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[3].vertSpanDown), 7 },
  { "passenger5HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[4].horizSpanLeft), 7 },
  { "passenger5HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[4].horizSpanRight), 7 },
  { "passenger5VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[4].vertSpanUp), 7 },
  { "passenger5VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[4].vertSpanDown), 7 },
  { "passenger6HorizSpanLeft", offsetof(vehicle_info_t, passengerViewLimits[5].horizSpanLeft), 7 },
  { "passenger6HorizSpanRight", offsetof(vehicle_info_t, passengerViewLimits[5].horizSpanRight), 7 },
  { "passenger6VertSpanUp", offsetof(vehicle_info_t, passengerViewLimits[5].vertSpanUp), 7 },
  { "passenger6VertSpanDown", offsetof(vehicle_info_t, passengerViewLimits[5].vertSpanDown), 7 },
  { "lowIdleSnd", offsetof(vehicle_info_t, sndNames), 0 },
  { "lowIdleSndPlyr", offsetof(vehicle_info_t, sndNames[1]), 0 },
  { "highIdleSnd", offsetof(vehicle_info_t, sndNames[2]), 0 },
  { "lowEngineSnd", offsetof(vehicle_info_t, sndNames[3]), 0 },
  { "highEngineSnd", offsetof(vehicle_info_t, sndNames[4]), 0 },
  { "turretSpinSnd", offsetof(vehicle_info_t, sndNames[5]), 0 },
  { "turretStopSnd", offsetof(vehicle_info_t, sndNames[6]), 0 },
  { "engineModLoopSnd", offsetof(vehicle_info_t, sndNames[7]), 0 },
  { "engineModLoopSndPlyr", offsetof(vehicle_info_t, sndNames[8]), 0 },
  { "engineModLoopLoadSnd", offsetof(vehicle_info_t, sndNames[9]), 0 },
  { "engineModLoopLoadSndPlyr", offsetof(vehicle_info_t, sndNames[10]), 0 },
  { "tankBodyDmgLeftSnd", offsetof(vehicle_info_t, sndNames[11]), 0 },
  { "tankBodyDmgRightSnd", offsetof(vehicle_info_t, sndNames[12]), 0 },
  { "tankGrindLeftSnd", offsetof(vehicle_info_t, sndNames[13]), 0 },
  { "tankGrindRightSnd", offsetof(vehicle_info_t, sndNames[14]), 0 },
  { "tankGrindLeftDmgSnd", offsetof(vehicle_info_t, sndNames[15]), 0 },
  { "tankGrindRightDmgSnd", offsetof(vehicle_info_t, sndNames[16]), 0 },
  { "tankBoostPerkSnd", offsetof(vehicle_info_t, sndNames[17]), 0 },
  { "tankBoostPerkSndPlyr", offsetof(vehicle_info_t, sndNames[18]), 0 },
  { "wheelRoadNoiseSnd", offsetof(vehicle_info_t, sndMaterialNames), 0 },
  { "wheelSlidingSnd", offsetof(vehicle_info_t, sndMaterialNames[1]), 0 },
  { "wheelPeelingOutSnd", offsetof(vehicle_info_t, sndMaterialNames[2]), 0 },
  { "engineSndSpeed", offsetof(vehicle_info_t, engineSndSpeed), 19 },
  { "engineModLoopNaturalRPMs", offsetof(vehicle_info_t, engineModLoopNaturalRPMs), 7 },
  { "idleRPMs", offsetof(vehicle_info_t, idleRPMs), 7 },
  { "idleRPMsMax", offsetof(vehicle_info_t, idleRPMsMax), 7 },
  { "gear1MinRPMs", offsetof(vehicle_info_t, gear1MinRPMs), 7 },
  { "gear1MaxRPMs", offsetof(vehicle_info_t, gear1MaxRPMs), 7 },
  { "gearChangeMPH", offsetof(vehicle_info_t, gearChangeMPH), 7 },
  { "gear2MinRPMs", offsetof(vehicle_info_t, gear2MinRPMs), 7 },
  { "gear2MaxRPMs", offsetof(vehicle_info_t, gear2MaxRPMs), 7 },
  { "trackLengthInches", offsetof(vehicle_info_t, trackLengthInches), 4 },
  { "clacksCurveDamageStart", offsetof(vehicle_info_t, clacksCurveDamageStart), 7 },
  { "clacksCurveDamageEnd", offsetof(vehicle_info_t, clacksCurveDamageEnd), 7 },
  { "clacksCurveMaxOmitted", offsetof(vehicle_info_t, clacksCurveMaxOmitted), 7 },
  { "treadGrindAttenuation", offsetof(vehicle_info_t, treadGrindAttenuation), 7 },
  { "treadDamageLoopHealthRatio", offsetof(vehicle_info_t, treadDamageLoopHealthRatio), 7 },
  { "skidSpeedMin", offsetof(vehicle_info_t, skidSpeedMin), 7 },
  { "skidSpeedMax", offsetof(vehicle_info_t, skidSpeedMax), 7 },
  { "animType", offsetof(vehicle_info_t, animType), 4 },
  { "animSet", offsetof(vehicle_info_t, animSet), 0 },
  { "mantleAngleFront", offsetof(vehicle_info_t, mantleAngles), 7 },
  { "mantleAngleBack", offsetof(vehicle_info_t, mantleAngles[1]), 7 },
  { "mantleAngleLeft", offsetof(vehicle_info_t, mantleAngles[2]), 7 },
  { "mantleAngleRight", offsetof(vehicle_info_t, mantleAngles[3]), 7 },
  { "extraWheelLeft1", offsetof(vehicle_info_t, extraWheelTags), 21 },
  { "extraWheelRight1", offsetof(vehicle_info_t, extraWheelTags[1]), 21 },
  { "extraWheelLeft2", offsetof(vehicle_info_t, extraWheelTags[2]), 21 },
  { "extraWheelRight2", offsetof(vehicle_info_t, extraWheelTags[3]), 21 },
  { "mod0", offsetof(vehicle_info_t, attachmentModels), 0 },
  { "tag0", offsetof(vehicle_info_t, attachmentTags), 0 },
  { "mod1", offsetof(vehicle_info_t, attachmentModels[1]), 0 },
  { "tag1", offsetof(vehicle_info_t, attachmentTags[1]), 0 },
  { "mod2", offsetof(vehicle_info_t, attachmentModels[2]), 0 },
  { "tag2", offsetof(vehicle_info_t, attachmentTags[2]), 0 },
  { "mod3", offsetof(vehicle_info_t, attachmentModels[3]), 0 },
  { "tag3", offsetof(vehicle_info_t, attachmentTags[3]), 0 },
  { "dmod0", offsetof(vehicle_info_t, deathAttachmentModels), 0 },
  { "dtag0", offsetof(vehicle_info_t, deathAttachmentTags), 0 },
  { "dmod1", offsetof(vehicle_info_t, deathAttachmentModels[1]), 0 },
  { "dtag1", offsetof(vehicle_info_t, deathAttachmentTags[1]), 0 },
  { "dmod2", offsetof(vehicle_info_t, deathAttachmentModels[2]), 0 },
  { "dtag2", offsetof(vehicle_info_t, deathAttachmentTags[2]), 0 },
  { "dmod3", offsetof(vehicle_info_t, deathAttachmentModels[3]), 0 },
  { "dtag3", offsetof(vehicle_info_t, deathAttachmentTags[3]), 0 },
  { "worldModel", offsetof(vehicle_info_t, model), 0 },
  { "deathModel", offsetof(vehicle_info_t, deathModel), 0 },
  { "modelSwapDelay", offsetof(vehicle_info_t, modelSwapDelay), 7 },
  { "exhaustFx", offsetof(vehicle_info_t, exhaustFx), 0 },
  { "oneExhaust", offsetof(vehicle_info_t, oneExhaust), 6 },
  { "rotorMainIdleFx", offsetof(vehicle_info_t, rotorMainIdleFx), 0 },
  { "rotorMainStartFx", offsetof(vehicle_info_t, rotorMainStartFx), 0 },
  { "rotorMainRunningFx", offsetof(vehicle_info_t, rotorMainRunningFx), 0 },
  { "rotorMainStopFx", offsetof(vehicle_info_t, rotorMainStopFx), 0 },
  { "rotorTailIdleFx", offsetof(vehicle_info_t, rotorTailIdleFx), 0 },
  { "rotorTailStartFx", offsetof(vehicle_info_t, rotorTailStartFx), 0 },
  { "rotorTailRunningFx", offsetof(vehicle_info_t, rotorTailRunningFx), 0 },
  { "rotorTailStopFx", offsetof(vehicle_info_t, rotorTailStopFx), 0 },
  { "treadFxAsphalt", offsetof(vehicle_info_t, treadFx[22]), 0 },
  { "treadFxBark", offsetof(vehicle_info_t, treadFx[1]), 0 },
  { "treadFxBrick", offsetof(vehicle_info_t, treadFx[2]), 0 },
  { "treadFxCarpet", offsetof(vehicle_info_t, treadFx[3]), 0 },
  { "treadFxCeramic", offsetof(vehicle_info_t, treadFx[23]), 0 },
  { "treadFxCloth", offsetof(vehicle_info_t, treadFx[4]), 0 },
  { "treadFxConcrete", offsetof(vehicle_info_t, treadFx[5]), 0 },
  { "treadFxCushion", offsetof(vehicle_info_t, treadFx[26]), 0 },
  { "treadFxDefault", offsetof(vehicle_info_t, treadFx), 0 },
  { "treadFxDirt", offsetof(vehicle_info_t, treadFx[6]), 0 },
  { "treadFxFlesh", offsetof(vehicle_info_t, treadFx[7]), 0 },
  { "treadFxFoliage", offsetof(vehicle_info_t, treadFx[8]), 0 },
  { "treadFxFruit", offsetof(vehicle_info_t, treadFx[27]), 0 },
  { "treadFxGlass", offsetof(vehicle_info_t, treadFx[9]), 0 },
  { "treadFxGrass", offsetof(vehicle_info_t, treadFx[10]), 0 },
  { "treadFxGravel", offsetof(vehicle_info_t, treadFx[11]), 0 },
  { "treadFxIce", offsetof(vehicle_info_t, treadFx[12]), 0 },
  { "treadFxMetal", offsetof(vehicle_info_t, treadFx[13]), 0 },
  { "treadFxMud", offsetof(vehicle_info_t, treadFx[14]), 0 },
  { "treadFxPaintedMetal", offsetof(vehicle_info_t, treadFx[28]), 0 },
  { "treadFxPaper", offsetof(vehicle_info_t, treadFx[15]), 0 },
  { "treadFxPlaster", offsetof(vehicle_info_t, treadFx[16]), 0 },
  { "treadFxPlastic", offsetof(vehicle_info_t, treadFx[24]), 0 },
  { "treadFxRock", offsetof(vehicle_info_t, treadFx[17]), 0 },
  { "treadFxRubber", offsetof(vehicle_info_t, treadFx[25]), 0 },
  { "treadFxSand", offsetof(vehicle_info_t, treadFx[18]), 0 },
  { "treadFxSnow", offsetof(vehicle_info_t, treadFx[19]), 0 },
  { "treadFxWater", offsetof(vehicle_info_t, treadFx[20]), 0 },
  { "treadFxWood", offsetof(vehicle_info_t, treadFx[21]), 0 },
  { "deathFxName", offsetof(vehicle_info_t, deathFxName), 0 },
  { "deathFxTag", offsetof(vehicle_info_t, deathFxTag), 0 },
  { "deathFxSound", offsetof(vehicle_info_t, deathFxSound), 0 },
  { "radiusDamageMin", offsetof(vehicle_info_t, radiusDamageMin), 7 },
  { "radiusDamageMax", offsetof(vehicle_info_t, radiusDamageMax), 7 },
  { "radiusDamageRadius", offsetof(vehicle_info_t, radiusDamageRadius), 7 },
  { "shootShock", offsetof(vehicle_info_t, shootShock), 0 },
  { "shootRumble", offsetof(vehicle_info_t, shootRumble), 0 },
  { "deathQuakeScale", offsetof(vehicle_info_t, deathQuakeScale), 7 },
  { "deathQuakeDuration", offsetof(vehicle_info_t, deathQuakeDuration), 7 },
  { "deathQuakeRadius", offsetof(vehicle_info_t, deathQuakeRadius), 7 },
  { "secTurretType", offsetof(vehicle_info_t, secTurretType), 0 },
  { "secTurretTag", offsetof(vehicle_info_t, secTurretTag), 0 },
  { "secTurretModel", offsetof(vehicle_info_t, secTurretModel), 0 },
  { "secTurretAiControlled", offsetof(vehicle_info_t, secTurretAiControlled), 6 },
  { "frontArmor", offsetof(vehicle_info_t, frontArmorRegen), 7 },
  { "rumbleType", offsetof(vehicle_info_t, rumbleType), 0 },
  { "rumbleScale", offsetof(vehicle_info_t, rumbleScale), 7 },
  { "rumbleDuration", offsetof(vehicle_info_t, rumbleDuration), 7 },
  { "rumbleRadius", offsetof(vehicle_info_t, rumbleRadius), 7 },
  { "rumbleBaseTime", offsetof(vehicle_info_t, rumbleBaseTime), 7 },
  { "rumbleAdditionalTime", offsetof(vehicle_info_t, rumbleAdditionalTime), 7 },
  { "healthDefault", offsetof(vehicle_info_t, healthDefault), 4 },
  { "healthMin", offsetof(vehicle_info_t, healthMin), 4 },
  { "healthMax", offsetof(vehicle_info_t, healthMax), 4 },
  { "team", offsetof(vehicle_info_t, eTeam), 22 },
  { "addToCompass", offsetof(vehicle_info_t, addToCompass), 6 },
  { "addToCompassEnemy", offsetof(vehicle_info_t, addToCompassEnemy), 6 },
  { "compassIcon", offsetof(vehicle_info_t, compassIcon), 0 },
  { "gasButton", offsetof(vehicle_info_t, gasButton), 23 },
  { "boostButton", offsetof(vehicle_info_t, boostButton), 23 },
  { "reverseBrakeButton", offsetof(vehicle_info_t, reverseBrakeButton), 23 },
  { "handBrakeButton", offsetof(vehicle_info_t, handBrakeButton), 23 },
  { "attackButton", offsetof(vehicle_info_t, attackButton), 23 },
  { "attackSecondaryButton", offsetof(vehicle_info_t, attackSecondaryButton), 23 },
  { "moveUpButton", offsetof(vehicle_info_t, moveUpButton), 23 },
  { "moveDownButton", offsetof(vehicle_info_t, moveDownButton), 23 },
  { "switchSeatButton", offsetof(vehicle_info_t, switchSeatButton), 23 },
  { "specialAbilityButton", offsetof(vehicle_info_t, specialAbilityButton), 23 },
  { "firePickupButton", offsetof(vehicle_info_t, firePickupButton), 23 },
  { "swapPickupButton", offsetof(vehicle_info_t, swapPickupButton), 23 },
  { "dropDeployableButton", offsetof(vehicle_info_t, dropDeployableButton), 23 },
  { "steerGraph", offsetof(vehicle_info_t, steerGraph), 24 },
  { "accelGraph", offsetof(vehicle_info_t, accelGraph), 24 },
  { "isNitrous", offsetof(vehicle_info_t, isNitrous), 6 },
  { "isFourWheelSteering", offsetof(vehicle_info_t, isFourWheelSteering), 6 },
  { "noDirectionalDamage", offsetof(vehicle_info_t, noDirectionalDamage), 6 },
  { "max_fric_tilt_angle", offsetof(vehicle_info_t, max_fric_tilt_angle), 7 },
  { "max_fric_tilt", offsetof(vehicle_info_t, max_fric_tilt), 7 },
  { "maxSpeed", offsetof(vehicle_info_t, nitrousVehParams.m_speed_max), 19 },
  { "accel", offsetof(vehicle_info_t, nitrousVehParams.m_accel_max), 19 },
  { "autoHandbrakeMinSpeed", offsetof(vehicle_info_t, nitrousVehParams.m_auto_hand_brake_min_speed), 19 },
  { "nitrous_steer_angle_max", offsetof(vehicle_info_t, nitrousVehParams.m_steer_angle_max), 7 },
  { "nitrous_steer_speed", offsetof(vehicle_info_t, nitrousVehParams.m_steer_speed), 7 },
  { "nitrous_body_mass", offsetof(vehicle_info_t, nitrousVehParams.m_body_mass), 20 },
  { "nitrous_wheel_radius", offsetof(vehicle_info_t, nitrousVehParams.m_wheel_radius), 7 },
  { "nitrous_susp_adj", offsetof(vehicle_info_t, nitrousVehParams.m_susp_adj), 7 },
  { "nitrous_susp_spring_k", offsetof(vehicle_info_t, nitrousVehParams.m_susp_spring_k), 7 },
  { "nitrous_susp_damp_k", offsetof(vehicle_info_t, nitrousVehParams.m_susp_damp_k), 7 },
  { "nitrous_susp_hard_limit", offsetof(vehicle_info_t, nitrousVehParams.m_susp_hard_limit), 7 },
  { "nitrous_susp_min_height", offsetof(vehicle_info_t, nitrousVehParams.m_susp_min_height), 7 },
  { "nitrous_tire_damp_hand", offsetof(vehicle_info_t, nitrousVehParams.m_tire_damp_hand), 7 },
  { "nitrous_tire_damp_brake", offsetof(vehicle_info_t, nitrousVehParams.m_tire_damp_brake), 7 },
  { "nitrous_tire_damp_coast", offsetof(vehicle_info_t, nitrousVehParams.m_tire_damp_coast), 7 },
  { "nitrous_tire_fric_brake", offsetof(vehicle_info_t, nitrousVehParams.m_tire_fric_brake), 7 },
  { "nitrous_tire_fric_hand_brake", offsetof(vehicle_info_t, nitrousVehParams.m_tire_fric_hand_brake), 7 },
  { "nitrous_tire_fric_fwd", offsetof(vehicle_info_t, nitrousVehParams.m_tire_fric_fwd), 7 },
  { "nitrous_tire_fric_side", offsetof(vehicle_info_t, nitrousVehParams.m_tire_fric_side), 7 },
  { "nitrous_roll_stability", offsetof(vehicle_info_t, nitrousVehParams.m_roll_stability), 7 },
  { "nitrous_roll_resistance", offsetof(vehicle_info_t, nitrousVehParams.m_pitch_roll_resistance), 7 },
  { "nitrous_yaw_resistance", offsetof(vehicle_info_t, nitrousVehParams.m_yaw_resistance), 7 },
  { "nitrous_upright_strength", offsetof(vehicle_info_t, nitrousVehParams.m_upright_strength), 7 },
  { "nitrous_tilt_fakey", offsetof(vehicle_info_t, nitrousVehParams.m_tilt_fakey), 7 },
  { "nitrous_traction_type", offsetof(vehicle_info_t, nitrousVehParams.m_traction_type), 18 },
  { "nitrous_peel_out_max_speed", offsetof(vehicle_info_t, nitrousVehParams.m_peel_out_max_speed), 7 },
  { "nitrous_tire_fric_side_max", offsetof(vehicle_info_t, nitrousVehParams.m_tire_fric_side_max), 7 },
  { "nitrous_reverse_scale", offsetof(vehicle_info_t, nitrousVehParams.m_reverse_scale), 7 },
  { "nitrous_water_speed_max", offsetof(vehicle_info_t, nitrousVehParams.m_water_speed_max), 19 },
  { "nitrous_water_accel_max", offsetof(vehicle_info_t, nitrousVehParams.m_water_accel_max), 7 },
  { "nitrous_water_turn_accel", offsetof(vehicle_info_t, nitrousVehParams.m_water_turn_accel), 7 },
  { "nitrous_water_turn_speed_max", offsetof(vehicle_info_t, nitrousVehParams.m_water_turn_speed_max), 7 },
  { "nitrous_boat_ebrake_power", offsetof(vehicle_info_t, nitrousVehParams.m_water_ebrake_power), 7 },
  { "nitrous_bbox_min_x", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_min), 7 },
  { "nitrous_bbox_min_y", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_min[1]), 7 },
  { "nitrous_bbox_min_z", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_min[2]), 7 },
  { "nitrous_bbox_max_x", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_max), 7 },
  { "nitrous_bbox_max_y", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_max[1]), 7 },
  { "nitrous_bbox_max_z", offsetof(vehicle_info_t, nitrousVehParams.m_bbox_max[2]), 7 },
  { "nitrous_mass_center_offset_x", offsetof(vehicle_info_t, nitrousVehParams.m_mass_center_offset), 7 },
  { "nitrous_mass_center_offset_y", offsetof(vehicle_info_t, nitrousVehParams.m_mass_center_offset[1]), 7 },
  { "nitrous_mass_center_offset_z", offsetof(vehicle_info_t, nitrousVehParams.m_mass_center_offset[2]), 7 },
  { "nitrous_buoyancybox_min_x", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_min), 7 },
  { "nitrous_buoyancybox_min_y", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_min[1]), 7 },
  { "nitrous_buoyancybox_min_z", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_min[2]), 7 },
  { "nitrous_buoyancybox_max_x", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_max), 7 },
  { "nitrous_buoyancybox_max_y", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_max[1]), 7 },
  { "nitrous_buoyancybox_max_z", offsetof(vehicle_info_t, nitrousVehParams.m_buoyancybox_max[2]), 7 },
  { "nitrous_boat_speed_rise", offsetof(vehicle_info_t, nitrousVehParams.m_boat_speed_rise), 7 },
  { "nitrous_boat_speed_tilt", offsetof(vehicle_info_t, nitrousVehParams.m_boat_speed_tilt), 7 },
  { "nitrous_boat_motor_offset_x", offsetof(vehicle_info_t, nitrousVehParams.m_boat_motor_offset), 7 },
  { "nitrous_boat_motor_offset_y", offsetof(vehicle_info_t, nitrousVehParams.m_boat_motor_offset[1]), 7 },
  { "nitrous_boat_motor_offset_z", offsetof(vehicle_info_t, nitrousVehParams.m_boat_motor_offset[2]), 7 },
  { "nitrous_boat_side_fric", offsetof(vehicle_info_t, nitrousVehParams.m_boat_side_fric_scale), 7 },
  { "nitrous_boat_forward_fric", offsetof(vehicle_info_t, nitrousVehParams.m_boat_forward_fric_scale), 7 },
  { "nitrous_boat_vertical_fric", offsetof(vehicle_info_t, nitrousVehParams.m_boat_vertical_fric_scale), 7 }
};



int __cdecl VEH_ParseSpecificField(unsigned __int8 *pStruct, const char *pValue, const int fieldType, const int fieldOffset)
{
    const char *v4; // eax
    const char *v5; // eax
    float v7; // [esp+0h] [ebp-10h]
    float v8; // [esp+4h] [ebp-Ch]
    int i; // [esp+Ch] [ebp-4h]
    int ia; // [esp+Ch] [ebp-4h]
    int ib; // [esp+Ch] [ebp-4h]
    int ic; // [esp+Ch] [ebp-4h]

    switch ( fieldType )
    {
        case 15:
            i = 0;
            break;
        case 16:
            ia = 0;
            while ( 2 )
            {
                if ( ia < 6 )
                {
                    if ( I_stricmp(pValue, s_vehicleCameraModes[ia]) )
                    {
                        ++ia;
                        continue;
                    }
                    *((_WORD *)pStruct + 54) = ia;
                }
                break;
            }
            if ( ia == 6 )
                Com_Error(ERR_DROP, "Unknown vehicle camera mode [%s]", pValue);
            return 1;
        case 17:
            ib = 0;
            while ( 2 )
            {
                if ( ib < 2 )
                {
                    if ( I_stricmp(pValue, s_vehicleBoostModes[ib]) )
                    {
                        ++ib;
                        continue;
                    }
                    *((_WORD *)pStruct + 176) = ib;
                }
                break;
            }
            if ( ib == 2 )
                Com_Error(ERR_DROP, "Unknown vehicle boost mode [%s]", pValue);
            return 1;
        case 18:
            ic = 0;
            while ( 2 )
            {
                if ( ic < 3 )
                {
                    if ( I_stricmp(pValue, s_tractionTypeNames[ic]) )
                    {
                        ++ic;
                        continue;
                    }
                    *((unsigned int *)pStruct + 1863) = ic;
                }
                break;
            }
            if ( ic == 3 )
                Com_Error(ERR_DROP, "Unknown traction type [%s]", pValue);
            return 1;
        case 19:
            v8 = atof(pValue);
            *(float *)&pStruct[fieldOffset] = v8 * 17.6;
            return 1;
        case 20:
            v7 = atof(pValue);
            *(float *)&pStruct[fieldOffset] = v7 * 0.001;
            return 1;
        case 21:
            *(unsigned int *)&pStruct[fieldOffset] = SL_FindLowercaseString(pValue, SCRIPTINSTANCE_SERVER);
            return 1;
        case 22:
            if ( I_stricmp(pValue, "axis") )
            {
                if ( I_stricmp(pValue, "allies") )
                {
                    v4 = va("unknown team '%s', should be axis or allies\n", pValue);
                    Scr_Error(v4, 0);
                }
                else
                {
                    *((unsigned int *)pStruct + 1797) = 2;
                }
            }
            else
            {
                *((unsigned int *)pStruct + 1797) = 1;
            }
            return 1;
        case 23:
            *(unsigned int *)&pStruct[fieldOffset] = Key_StringToKeynum(pValue);
            return 1;
        case 24:
            *(GraphFloat **)&pStruct[fieldOffset] = GraphFloat_Load((char*)pValue);
            return 1;
        default:
            v5 = va("Bad vehicle field type %i\n", fieldType);
            if ( !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\bgame\\bg_misctables.cpp", 528, 0, v5) )
                __debugbreak();
            Com_Error(ERR_DROP, "Bad vehicle field type %i", fieldType);
            return 0;
    }
    while ( 1 )
    {
        if ( i >= 7 )
            goto LABEL_7;
        if ( !I_stricmp(pValue, s_vehicleClassNames[i]) )
            break;
        ++i;
    }
    *((_WORD *)pStruct + 32) = i;
LABEL_7:
    if ( i == 7 )
        Com_Error(ERR_DROP, "Unknown vehicle type [%s]", pValue);
    return 1;
}

