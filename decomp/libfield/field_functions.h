/* libfield: the functions of the module that other functions call, by offset from the module base. Those listed as not decompiled are still the generated
 * code; calling them through `callLocal` runs it (and the swaps it contains). */
#pragma once
#include "field_data.h"

namespace field {
namespace fn {

/* heap of the field module (FUN_091cc4f0 / FUN_091cc500) */
constexpr uint32_t HeapAlloc = offsetOf(0x091CC4F0), HeapFree = offsetOf(0x091CC500);
constexpr uint32_t HeapHandle = offsetOf(0x091CC510), SystemSprite = offsetOf(0x091CC750);
constexpr uint32_t PlaySoundWhenReady = offsetOf(0x091CF828);

/* indicators */
constexpr uint32_t ClearIndicators = offsetOf(0x091C5564), ClearIndicator = offsetOf(0x091C55AC);
constexpr uint32_t SelectIndicator = offsetOf(0x091C543C), IndicatorRecord = offsetOf(0x091C55F4);   /* not decompiled yet */

/* the map scene and the characters on it */
constexpr uint32_t UpdateObjectMatrix = offsetOf(0x091DD638), DrawShadow = offsetOf(0x091E7E24), VectorLength = offsetOf(0x091CD740);
constexpr uint32_t PlayNpcSound = offsetOf(0x091CA140), DrawNpcLoaded = offsetOf(0x091C9438), DrawNpcPlain = offsetOf(0x091C918C);
constexpr uint32_t WorldToCell = offsetOf(0x091C8524), CellToWorld = offsetOf(0x091C84E0), AreaOfCell = offsetOf(0x091C8E04), GroundCells = offsetOf(0x091C7DEC);
constexpr uint32_t WalkabilityGrid = offsetOf(0x091C7E18);
constexpr uint32_t UpdateMapNpcs = offsetOf(0x091C8E74), GroundHeight = offsetOf(0x091C81DC);
constexpr uint32_t TerrainStepA = offsetOf(0x091C96E8), TerrainStepB = offsetOf(0x091C9634), TerrainStepC = offsetOf(0x091C97DC), TerrainState = offsetOf(0x091C98E0);
constexpr uint32_t TerrainStepD = offsetOf(0x091C9AB4), TerrainStepE = offsetOf(0x091C9D00);
constexpr uint32_t CameraStateList = offsetOf(0x091D9610), MarkerState = offsetOf(0x091EC918), DrawMarker = offsetOf(0x091EC994), MarkerEnd = offsetOf(0x091ECCBC);
constexpr uint32_t MapSceneFinish = offsetOf(0x091CA000);
constexpr uint32_t DrawSpriteUnscaled = offsetOf(0x091CD818), DrawSpriteQuad = offsetOf(0x091CD838);

/* the walkers */
constexpr uint32_t MarkCell = offsetOf(0x091DDC7C), CellIsFree = offsetOf(0x091DDC58), CellIsWalkable = offsetOf(0x091C7E44), CellIsReserved = offsetOf(0x091C8550);
constexpr uint32_t AnimateObject = offsetOf(0x091C59EC), DrawObject = offsetOf(0x091C5B28), PointHitsCell = offsetOf(0x091CB04C);
/* the player */
constexpr uint32_t PlayerReadInput = offsetOf(0x091CB930), PlayerCollide = offsetOf(0x091CBC94), PlayerEffects = offsetOf(0x091CC030), PlayerCorners = offsetOf(0x091C62BC);
constexpr uint32_t MarkPlayerCells = offsetOf(0x091CB83C), SurfaceAt = offsetOf(0x091CD574), SetPlayerShadow = offsetOf(0x091CB7C8), ShadowKind = offsetOf(0x091E8434);
constexpr uint32_t PlayEffect = offsetOf(0x091CF828), CellUse = offsetOf(0x091DDC10), ObjectPosition = offsetOf(0x091C5C48);
/* the loader */
constexpr uint32_t CreatePool = offsetOf(0x091CC51C), ResetResourceTable = offsetOf(0x091CCE60), FindRecord = offsetOf(0x091CCFBC);
constexpr uint32_t EnqueueJob = offsetOf(0x091CD09C), AddWaiter = offsetOf(0x091CD1C0), FinishJob = offsetOf(0x091CD178), StartFileRead = offsetOf(0x091CD320);
constexpr uint32_t RegisterResource = offsetOf(0x091CC628), RegisterRecord = offsetOf(0x091CC990), NotifyWaiters = offsetOf(0x091CD29C), StartRecordRead = offsetOf(0x091CD400);
/* resets */
constexpr uint32_t ResetHud = offsetOf(0x091D2EE8), ResetPanel = offsetOf(0x091E7100), ResetPanelData = offsetOf(0x091E7DFC), ResetPlayer = offsetOf(0x091CB274);
constexpr uint32_t ResetFollower = offsetOf(0x091DC8E4), ResetMenuState = offsetOf(0x091D21B8);
/* field state */
constexpr uint32_t ProgressStage = offsetOf(0x091CD518), DrawModelAt = offsetOf(0x091DDAA0);

/* the functions of the other parts of the module (named by what they do; the ones without a swap yet are still generated code) */
constexpr uint32_t UpdateDp = offsetOf(0x091D89D4);
constexpr uint32_t PumpLoader = offsetOf(0x091CC1E4);
constexpr uint32_t UpdateMessage = offsetOf(0x091D7068);
constexpr uint32_t UpdateShake = offsetOf(0x091D959C);
constexpr uint32_t UpdateWorldMap = offsetOf(0x091D9E90);
constexpr uint32_t UpdateMapScene = offsetOf(0x091C76EC);
constexpr uint32_t PlayAmbient = offsetOf(0x091EC204);
constexpr uint32_t UpdateBeacons = offsetOf(0x091CF918);
constexpr uint32_t UpdateWalkers = offsetOf(0x091CA320);
constexpr uint32_t UpdateFlow = offsetOf(0x091D0E14);
constexpr uint32_t UpdateIndicators = offsetOf(0x091C4B34);
constexpr uint32_t UpdateDialog = offsetOf(0x091D80AC);
constexpr uint32_t UpdateBanner = offsetOf(0x091E8740);
constexpr uint32_t UpdateScreenFade = offsetOf(0x091EB67C);
constexpr uint32_t CheckHandlersDone = offsetOf(0x091D0D00);
constexpr uint32_t DrawMessage = offsetOf(0x091D73A0);
constexpr uint32_t DrawShakeScene = offsetOf(0x091D95C0);
constexpr uint32_t DrawWorldMap = offsetOf(0x091DA0D0);
constexpr uint32_t DrawMapScene = offsetOf(0x091C77E8);
constexpr uint32_t DrawBeacons = offsetOf(0x091CFA1C);
constexpr uint32_t DrawDp = offsetOf(0x091D89F8);
constexpr uint32_t DrawFlow = offsetOf(0x091D1544);
constexpr uint32_t DrawScreenFade = offsetOf(0x091EB748);
constexpr uint32_t DrawIndicators = offsetOf(0x091C4DC0);
constexpr uint32_t DrawWalkers = offsetOf(0x091CA704);
constexpr uint32_t DrawEffects = offsetOf(0x091DD758);
constexpr uint32_t DrawDialog = offsetOf(0x091D81F8);
constexpr uint32_t DrawBanner = offsetOf(0x091E8848);
constexpr uint32_t ResetCameraShake = offsetOf(0x091D9578);
constexpr uint32_t ResetEffects = offsetOf(0x091DD5C4);
constexpr uint32_t ResetWorldMap = offsetOf(0x091D9E50);
constexpr uint32_t ResetMapScene = offsetOf(0x091C75C4);
constexpr uint32_t ResetBeacons = offsetOf(0x091CF8CC);
constexpr uint32_t ResetShadowOffset = offsetOf(0x091C5820);
constexpr uint32_t ResetWalkers = offsetOf(0x091CA240);
constexpr uint32_t ResetScreens = offsetOf(0x091D0D5C);
constexpr uint32_t ResetIndicators = offsetOf(0x091C4AD8);
constexpr uint32_t ResetDialog = offsetOf(0x091D806C);
constexpr uint32_t ResetBanner = offsetOf(0x091E8700);
constexpr uint32_t ResetScreenFade = offsetOf(0x091EB63C);
constexpr uint32_t ResetFieldState = offsetOf(0x091CD4F8);
constexpr uint32_t ResetAll = offsetOf(0x091D0B64);
constexpr uint32_t StartLoader = offsetOf(0x091CC198);
constexpr uint32_t HandlerDone = offsetOf(0x091D0688);
constexpr uint32_t InitDp = offsetOf(0x091D88F8);
constexpr uint32_t InitMessage = offsetOf(0x091D6E2C);
constexpr uint32_t StartWorldMap = offsetOf(0x091DA278);
constexpr uint32_t StartBanner = offsetOf(0x091E89D4);
constexpr uint32_t StartHud = offsetOf(0x091D3214);
constexpr uint32_t StartDpAnimation = offsetOf(0x091D8C08);
constexpr uint32_t StartTownMenu = offsetOf(0x091EBFF8);
constexpr uint32_t EnterFlow = offsetOf(0x091CB670);
constexpr uint32_t FlowSetup = offsetOf(0x091CAF20);
constexpr uint32_t WalkingStep = offsetOf(0x091D1AE8);
constexpr uint32_t OpenAsk = offsetOf(0x091ECEA8);
constexpr uint32_t AskReply = offsetOf(0x091ED108);
constexpr uint32_t PlayerWaitDone = offsetOf(0x091CB828);
constexpr uint32_t PrepareTalk = offsetOf(0x091D2740);
constexpr uint32_t TalkTarget = offsetOf(0x091CAE60);
constexpr uint32_t SetTalkTarget = offsetOf(0x091C4F30);
constexpr uint32_t TalkStep = offsetOf(0x091D21E0);
constexpr uint32_t OpenShop = offsetOf(0x091DDCBC);
constexpr uint32_t ShopReply = offsetOf(0x091DDD5C);
constexpr uint32_t MessagePanel = offsetOf(0x091D76C0);
constexpr uint32_t SetMessageMode = offsetOf(0x091D76F8);
constexpr uint32_t StartMessage = offsetOf(0x091D7704);
constexpr uint32_t MessageDone = offsetOf(0x091D76A8);
constexpr uint32_t CloseMessage = offsetOf(0x091D779C);
constexpr uint32_t OpenTownMenu = offsetOf(0x091DFC1C);
constexpr uint32_t TownMenuReset = offsetOf(0x091D346C);
constexpr uint32_t TownMenuReady = offsetOf(0x091D3490);
constexpr uint32_t TownMenuChoice = offsetOf(0x091DFEE8);
constexpr uint32_t TownMenuClose = offsetOf(0x091D3448);
constexpr uint32_t PrepareCallA = offsetOf(0x091E7294);
constexpr uint32_t CallAStep = offsetOf(0x091E736C);
constexpr uint32_t PrepareCallB = offsetOf(0x091E73C0);
constexpr uint32_t CallBStep = offsetOf(0x091E74E0);
constexpr uint32_t OpenPanel = offsetOf(0x091E9368);
constexpr uint32_t PanelReply = offsetOf(0x091E9434);
constexpr uint32_t PanelResult = offsetOf(0x091E96EC);
constexpr uint32_t OpenList = offsetOf(0x091EBA88);
constexpr uint32_t ListReply = offsetOf(0x091EBB24);
constexpr uint32_t EndTalk = offsetOf(0x091CAE98);
constexpr uint32_t PlayerArrived = offsetOf(0x091CB73C);
constexpr uint32_t FollowerArrived = offsetOf(0x091DCDE8);
constexpr uint32_t UpdatePlayer = offsetOf(0x091CB294);
constexpr uint32_t UpdateFollower = offsetOf(0x091DC904);
constexpr uint32_t CharacterShown = offsetOf(0x091D2A60);
constexpr uint32_t CharacterInfo = offsetOf(0x091CAE10);
constexpr uint32_t ShowCharacterInfo = offsetOf(0x091D32FC);
constexpr uint32_t PanelOpen = offsetOf(0x091D3368);
constexpr uint32_t ListOpen = offsetOf(0x091D3434);
constexpr uint32_t UpdatePanel = offsetOf(0x091E7150);
constexpr uint32_t DialogDone = offsetOf(0x091E7534);
constexpr uint32_t HideHudPart = offsetOf(0x091D3344);
constexpr uint32_t SetPlayerControl = offsetOf(0x091CB81C);
constexpr uint32_t WalkerAt = offsetOf(0x091CACEC);
constexpr uint32_t RequestResult = offsetOf(0x091EA7EC);
constexpr uint32_t StartRequest = offsetOf(0x091EA79C);
constexpr uint32_t RequestResource = offsetOf(0x091CC570);
constexpr uint32_t DrawPlayer = offsetOf(0x091CB610);
constexpr uint32_t DrawFollower = offsetOf(0x091DCCFC);
constexpr uint32_t DrawHud = offsetOf(0x091D2FF0);
constexpr uint32_t DrawPanel = offsetOf(0x091E71A4);
constexpr uint32_t DrawMenuLists = offsetOf(0x091D2680);
constexpr uint32_t DrawListStep = offsetOf(0x091EBBE0);
constexpr uint32_t DrawRequest = offsetOf(0x091EA8E0);
constexpr uint32_t DrawShopStep = offsetOf(0x091DDFD4);
constexpr uint32_t DrawAsk = offsetOf(0x091ED160);
constexpr uint32_t DrawScroll = offsetOf(0x091ED28C);
constexpr uint32_t DrawFadeQuad = offsetOf(0x091D963C);
constexpr uint32_t DrawTownMenu = offsetOf(0x091E01D0);
constexpr uint32_t DrawPanelWindow = offsetOf(0x091E9644);
constexpr uint32_t ShowCalendar = offsetOf(0x091D34F4);
constexpr uint32_t ShowCounters = offsetOf(0x091D563C);
constexpr uint32_t ShowBadge = offsetOf(0x091D59B8);
constexpr uint32_t ShowDate = offsetOf(0x091D3704);
constexpr uint32_t ShowWeekday = offsetOf(0x091D4C88);
constexpr uint32_t ShowSlideD = offsetOf(0x091D4F34);
constexpr uint32_t UpdateSlideCalendar = offsetOf(0x091D3600);
constexpr uint32_t UpdateSlideDate = offsetOf(0x091D3714);
constexpr uint32_t UpdateSlideWeekday = offsetOf(0x091D4C98);
constexpr uint32_t UpdateSlideD = offsetOf(0x091D4F44);
constexpr uint32_t UpdateDpFly = offsetOf(0x091D5258);
constexpr uint32_t UpdateSlideCounters = offsetOf(0x091D564C);
constexpr uint32_t UpdateSlideBadge = offsetOf(0x091D5A90);
constexpr uint32_t PassList = offsetOf(0x091D36F4);
constexpr uint32_t DrawCalendar = offsetOf(0x091D3838);
constexpr uint32_t DrawCalendarLanguage0 = offsetOf(0x091D39AC);
constexpr uint32_t DrawCalendarEnglish = offsetOf(0x091D3C8C);
constexpr uint32_t DrawCalendarLanguage2 = offsetOf(0x091D3F54);
constexpr uint32_t DrawCalendarLanguage3 = offsetOf(0x091D41A4);
constexpr uint32_t DrawCalendarLanguage4 = offsetOf(0x091D439C);
constexpr uint32_t DrawCalendarLanguage5 = offsetOf(0x091D4594);
constexpr uint32_t DrawClock = offsetOf(0x091D4B88);
constexpr uint32_t DrawDigitalClock = offsetOf(0x091D4978);
constexpr uint32_t DrawWeekday = offsetOf(0x091D4D6C);
constexpr uint32_t DrawHudExtraA = offsetOf(0x091D5018);
constexpr uint32_t DrawHudExtraB = offsetOf(0x091D5368);
constexpr uint32_t DrawBadgeIcons = offsetOf(0x091D5784);
constexpr uint32_t DrawCounters = offsetOf(0x091D5BE8);
constexpr uint32_t DrawDateBox = offsetOf(0x091D47E4);
constexpr uint32_t DrawGlyph = offsetOf(0x091D6718);
constexpr uint32_t DrawNumberString = offsetOf(0x091D67D8);
constexpr uint32_t DrawWeekdaySprite = offsetOf(0x091D68FC);
constexpr uint32_t DrawClockHands = offsetOf(0x091D6974);
constexpr uint32_t DrawRotatedSprite = offsetOf(0x091D6A78);
constexpr uint32_t SetDpTexture = offsetOf(0x091D8AD4);
constexpr uint32_t SetMessageSprite = offsetOf(0x091D76B4);
constexpr uint32_t PreparePanel = offsetOf(0x091E71F8);
constexpr uint32_t BannerDone = offsetOf(0x091E8AA8), WorldMapDone = offsetOf(0x091DA488), DpDone = offsetOf(0x091D8BF4), UpdateFlowB = offsetOf(0x091D1E1C), UpdateHud = offsetOf(0x091D2F4C);
constexpr uint32_t UpdateDpCounter = offsetOf(0x091D8C50), UpdateDpFloat = offsetOf(0x091D903C), DigitCount = offsetOf(0x091D953C), DrawBigDigit = offsetOf(0x091D92D4);
constexpr uint32_t DrawDigits = offsetOf(0x091D93B0), DrawDpCounter = offsetOf(0x091D8DA8), DrawDpFloat = offsetOf(0x091D913C);
constexpr uint32_t UpdateShakeValues = offsetOf(0x091D9C80), SetupCamera3D = offsetOf(0x091D9814), SetupLookAt = offsetOf(0x091D9A30), SetupLight = offsetOf(0x091D9B84);
constexpr uint32_t DrawQuad = offsetOf(0x091D9DB0), PlayerPosition = offsetOf(0x091CB798);
constexpr uint32_t BuildMessageWindow = offsetOf(0x091D7840), ResetMessageState = offsetOf(0x091D6ED0), SetMessageFont = offsetOf(0x091D76E4), ShowMessage = offsetOf(0x091D778C);
constexpr uint32_t LayoutMessage = offsetOf(0x091D7B08), RelayoutMessage = offsetOf(0x091D7C40), BeginMessageText = offsetOf(0x091D7E3C), EndMessageText = offsetOf(0x091D7F18);
constexpr uint32_t MeasureMessageLines = offsetOf(0x091D7F3C), TypeNextCharacter = offsetOf(0x091D7990), UpdateMessageArrow = offsetOf(0x091D7954);
constexpr uint32_t MessageBusyA = offsetOf(0x091D77BC), MessageBusyB = offsetOf(0x091D780C), DrawMessageArrow = offsetOf(0x091D7D84);
constexpr uint32_t TimeOfDay = offsetOf(0x091CD548);
constexpr uint32_t PlaceInfo = offsetOf(0x091DB508), PlaceKind = offsetOf(0x091DB2D4), RegisterPlaces = offsetOf(0x091DB344), PlaceText = offsetOf(0x091DB680);
constexpr uint32_t OpenPopup = offsetOf(0x091DB944), PopupWidth = offsetOf(0x091DBB1C), StartFade = offsetOf(0x091DC714), UpdateFade = offsetOf(0x091DC754);
constexpr uint32_t MapNeighbour = offsetOf(0x091DAECC), WorldMapInput = offsetOf(0x091DA604), WorldMapMove = offsetOf(0x091DAFE4), WorldMapWait = offsetOf(0x091DB064);
constexpr uint32_t PlaceAnswer = offsetOf(0x091DB438), AnimateCursor = offsetOf(0x091DB118), ConvertAxis = offsetOf(0x091CD6F8), VectorLength2D = offsetOf(0x091CD740);
constexpr uint32_t DrawPlaceIcon = offsetOf(0x091DC3F0), DrawMapBase = offsetOf(0x091DBD88), DrawPlaceInfo = offsetOf(0x091DBF90), DrawPlaceMarks = offsetOf(0x091DC0BC);
constexpr uint32_t DrawArrows = offsetOf(0x091DC45C), DrawCursorTrail = offsetOf(0x091DC60C), DrawPopupRow = offsetOf(0x091DBBB8), OnMapSprites = offsetOf(0x091DB770);
constexpr uint32_t MoveOnGround = offsetOf(0x091C5F30), FindNearObjects = offsetOf(0x091CA7D4), NearestDirection = offsetOf(0x091C5E40), FollowerAim = offsetOf(0x091DD050);
constexpr uint32_t PrepareEffectCamera = offsetOf(0x091DD094), SetEffectFade = offsetOf(0x091DD1B4), BeginEffectMask = offsetOf(0x091DD1C0), DrawEffectVeil = offsetOf(0x091DD478), DrawEffect = offsetOf(0x091DD23C);
constexpr uint32_t UpdateDialogText = offsetOf(0x091D87A4), DialogTextDraw = offsetOf(0x091D85E4), DialogWindowDraw = offsetOf(0x091D85EC);
constexpr uint32_t PanelUpdateWindow = offsetOf(0x091E76F4), PanelUpdateText = offsetOf(0x091E7570), PanelDrawWindow = offsetOf(0x091E7ACC), PanelDrawText = offsetOf(0x091E75B0), PanelExtra = offsetOf(0x091CC564);
constexpr uint32_t ColourLerp = offsetOf(0x091CD7B8), DrawBannerLine = offsetOf(0x091E8ABC), FadeStep = offsetOf(0x091EB9CC), FadeFinished = offsetOf(0x091CCDD4);
}  // namespace fn
}  // namespace field
