/* Shared declarations of the libtitle swaps (the title flow, its menus and screens): the module base, the table of game functions, the game state and
 * the helpers more than one file uses. Each swaps file in this folder replaces generated game functions with readable C++ (docs/DECOMP.md). */
#pragma once
#include "../swap.h"
#include "../pc_style.h"
#include "../ehsys.h"
#include "../duelists.h"   /* character ids and deck ids (who a duelist is vs which deck file he uses) */
#include "title_state.h"
#include <string.h>
#include <algorithm>
#include <stdlib.h>
#include <iterator>
#include "gfx_options.h"   /* runtime/: the graphics options as choices (for the Graphics screen) */
#include "text_input.h"   /* runtime/: the player's name as typed on the PC keyboard (for the name box) */

bool display_confirm(const wchar_t *text);   /* runtime/ui/display.cpp: Yes/No box, the game waits while it is open */
void display_quit(void);                     /* runtime/ui/display.cpp: log, pending recordings, then ExitProcess */

#define MODULE "libtitle"
using namespace libtitle;
namespace nid = ehsys::nid;

constexpr uint32_t kBase = 0x08efca00;
constexpr uint32_t off(uint32_t addr) { return addr - kBase; }

/* The game functions this module calls, by address. Each one is a Swap below (callLocal(fn::X, ...) runs the same code as the hook). */
namespace fn {
constexpr uint32_t Main = off(0x08efca34);
constexpr uint32_t InitFlow = off(0x08efcc30);
constexpr uint32_t EnterListScreen = off(0x08efcd58);
constexpr uint32_t OpeningStep = off(0x08efcfc0);
constexpr uint32_t MenuScreenStep = off(0x08efd090);
constexpr uint32_t Slot13Progress = off(0x08efd1a8);
constexpr uint32_t BuildTitleList = off(0x08efd20c);
constexpr uint32_t OpeningMenuInput = off(0x08efd334);
constexpr uint32_t DrawListLabel = off(0x08efd538);
constexpr uint32_t BuildSubList3 = off(0x08efd644);
constexpr uint32_t SubList3Input = off(0x08efd794);
constexpr uint32_t DrawSubList3Row = off(0x08efd8a8);
constexpr uint32_t BuildSubList5 = off(0x08efd9a0);
constexpr uint32_t SubList5Input = off(0x08efda88);
constexpr uint32_t DrawSubList5Row = off(0x08efdbf8);
constexpr uint32_t BuildSubList6 = off(0x08efdcf0);
constexpr uint32_t SubList6Input = off(0x08efddec);
constexpr uint32_t DrawSubList6Row = off(0x08efdffc);
constexpr uint32_t StartLoadB = off(0x08efe248);
constexpr uint32_t LoadDoneB = off(0x08efe29c);
constexpr uint32_t CharListStep = off(0x08efe2f4);
constexpr uint32_t StartLoadA = off(0x08efe794);
constexpr uint32_t LoadDoneA = off(0x08efe7f0);
constexpr uint32_t LimitListScreen = off(0x08efe83c);
constexpr uint32_t LimitListStep = off(0x08efe914);
constexpr uint32_t LanguageReturnStep = off(0x08efecbc);
constexpr uint32_t SaveDialogStep = off(0x08efedd8);
constexpr uint32_t DrawSaveDialog = off(0x08efef40);
constexpr uint32_t UpdateFlow = off(0x08efefa8);
constexpr uint32_t StartLogoWith = off(0x08eff0b8);
constexpr uint32_t SwitchScreen = off(0x08eff100);
constexpr uint32_t QuitDialogStep = off(0x08eff45c);
constexpr uint32_t Tick = off(0x08eff908);
constexpr uint32_t LoadingStep = off(0x08effb44);
constexpr uint32_t StartScreen = off(0x08effc3c);
constexpr uint32_t IntroStep = off(0x08effc74);
constexpr uint32_t StartScreenFaded = off(0x08effd08);
constexpr uint32_t LogoSequence = off(0x08effd94);
constexpr uint32_t EnterLogoScreen = off(0x08f000ac);
constexpr uint32_t LeaveLogoScreen = off(0x08f00160);
constexpr uint32_t RunScreen = off(0x08f00238);
constexpr uint32_t LogoStep = off(0x08f00744);
constexpr uint32_t FirstMessageText = off(0x08f007d8);
constexpr uint32_t MessageText = off(0x08f0082c);
constexpr uint32_t LoadMenuArchives = off(0x08f008f0);
constexpr uint32_t LoadMenuArchive = off(0x08f009c0);
constexpr uint32_t RegisterArchive = off(0x08f00a60);
constexpr uint32_t CurrentLogoResource = off(0x08f00ad4);
constexpr uint32_t LogoName = off(0x08f00af4);
constexpr uint32_t ImageName = off(0x08f00b1c);
constexpr uint32_t MessageRecord = off(0x08f00b74);
constexpr uint32_t DrawSpritePlain = off(0x08f00ba0);
constexpr uint32_t DrawSpriteAt = off(0x08f00bf0);
constexpr uint32_t DrawSprite = off(0x08f00c28);
constexpr uint32_t DrawLogos = off(0x08f00e88);
constexpr uint32_t StartColourFade = off(0x08f011e8);
constexpr uint32_t ActivateColourFade = off(0x08f01208);
constexpr uint32_t StepColourFade = off(0x08f01214);
constexpr uint32_t FadeToBlack16 = off(0x08f01394);
constexpr uint32_t FadeToBlack = off(0x08f0139c);
constexpr uint32_t StartFade = off(0x08f013ac);
constexpr uint32_t RequestScreen16 = off(0x08f01550);
constexpr uint32_t RequestScreen = off(0x08f015a0);
constexpr uint32_t GetPhase = off(0x08f015f0);
constexpr uint32_t RegisterWindow = off(0x08f015fc);
constexpr uint32_t InitWindow = off(0x08f01628);
constexpr uint32_t CentreAndRun = off(0x08f017f4);
constexpr uint32_t InitList = off(0x08f01860);
constexpr uint32_t ListUpdate = off(0x08f019ac);
constexpr uint32_t ListBackToThree = off(0x08f01f9c);
constexpr uint32_t ResetList = off(0x08f01fc0);
constexpr uint32_t StepFadeTable = off(0x08f01ff8);
constexpr uint32_t DrawList = off(0x08f020b8);
constexpr uint32_t ItemColor = off(0x08f0270c);
constexpr uint32_t SetupDrawState = off(0x08f02770);
constexpr uint32_t PushScene13 = off(0x08f02884);
constexpr uint32_t SlotLoad = off(0x08f02edc);
constexpr uint32_t SlotSetRange = off(0x08f02f68);
constexpr uint32_t SlotQuery = off(0x08f02ff4);
constexpr uint32_t SlotDraw = off(0x08f0302c);
constexpr uint32_t DrawMenuArrow = off(0x08f030bc);
constexpr uint32_t StartLoadedModule = off(0x08f03388);
constexpr uint32_t UnloadHelperModule = off(0x08f03410);
constexpr uint32_t WaitLoadsAndPlay = off(0x08f03484);
constexpr uint32_t AnyCardOwned = off(0x08f03518);
constexpr uint32_t SaveFlag8 = off(0x08f03564);
constexpr uint32_t CentredTextPlacement = off(0x08f03624);
constexpr uint32_t DrawStyledText = off(0x08f036e8);
constexpr uint32_t DrawMessageText = off(0x08f0378c);
constexpr uint32_t DrawAlignedText = off(0x08f03840);
constexpr uint32_t StoreProgress = off(0x08f039cc);
constexpr uint32_t EnterTitleMenu = off(0x08f03a7c);
constexpr uint32_t TitleScreen = off(0x08f03b90);
constexpr uint32_t DrawBackground = off(0x08f03de0);
constexpr uint32_t InitTitleList = off(0x08f03ed8);
constexpr uint32_t TitleMenuInput = off(0x08f03f60);
constexpr uint32_t DrawTitleItem = off(0x08f040e0);
constexpr uint32_t LanguageFlow = off(0x08f04528);
constexpr uint32_t RewriteSettings = off(0x08f046b4);
constexpr uint32_t SetupMenuInput = off(0x08f04c5c);
constexpr uint32_t StartMenuInput = off(0x08f04d00);
constexpr uint32_t ClearMenuInput = off(0x08f04e5c);
constexpr uint32_t SetTextBoxFontSize = off(0x08f04e98);
constexpr uint32_t ResetMenuInput = off(0x08f04eac);
constexpr uint32_t RequestChange = off(0x08f04f34);
constexpr uint32_t BuildMenuInput = off(0x08f04f44);
constexpr uint32_t LayoutRows = off(0x08f05058);
constexpr uint32_t LayoutRowsFloat = off(0x08f05190);
constexpr uint32_t PrepareMenuDraw = off(0x08f052d4);
constexpr uint32_t SetTextBoxFont = off(0x08f053b0);
constexpr uint32_t DrawMenuRows = off(0x08f053d4);
constexpr uint32_t EnterOptionsScreen = off(0x08f05504);
constexpr uint32_t OptionsStep = off(0x08f055c8);
constexpr uint32_t DrawOptions = off(0x08f056c4);
constexpr uint32_t InitOptionsScreen = off(0x08f0573c);
constexpr uint32_t ReadSettings = off(0x08f057ec);
constexpr uint32_t WriteSettings = off(0x08f058f8);
constexpr uint32_t FreeOptionsScreen = off(0x08f059a4);
constexpr uint32_t OptionsInput = off(0x08f059b4);
constexpr uint32_t DrawOptionsRows = off(0x08f05fc0);
constexpr uint32_t DrawOptionRow = off(0x08f06c20);
constexpr uint32_t DrawVolumeBar = off(0x08f07074);
constexpr uint32_t DrawArrows = off(0x08f071c4);
constexpr uint32_t DrawToggle = off(0x08f07394);
constexpr uint32_t SetupSprites = off(0x08f074a0);
constexpr uint32_t OptionTextCentred = off(0x08f07554);
constexpr uint32_t OptionText = off(0x08f07684);
constexpr uint32_t EnterLanguageSelect = off(0x08f0768c);
constexpr uint32_t LanguageSelectStep = off(0x08f077e8);
constexpr uint32_t DrawLanguageSelect = off(0x08f07acc);
constexpr uint32_t DrawLanguageRow = off(0x08f07b64);
constexpr uint32_t DrawLanguageCursor = off(0x08f07cac);
constexpr uint32_t DrawLanguageFlag = off(0x08f07e60);
constexpr uint32_t TitleStep = off(0x08f07ff4);
constexpr uint32_t TitleDraw = off(0x08f08084);
constexpr uint32_t LanguageStep = off(0x08f080f0);
constexpr uint32_t EnterMainMenu = off(0x08f0877c);
constexpr uint32_t MainMenuStep = off(0x08f087cc);
constexpr uint32_t BuildMainMenu = off(0x08f0886c);
constexpr uint32_t ResetMainMenuWindow = off(0x08f088e0);
constexpr uint32_t OpenDialog = off(0x08f08a34);
constexpr uint32_t MainMenuOwnStep = off(0x08f08d58);
constexpr uint32_t DialogAnswer = off(0x08f090b0);
constexpr uint32_t DrawMainMenu = off(0x08f0913c);
constexpr uint32_t ResetInfoBox = off(0x08f09574);
constexpr uint32_t ResetPanel = off(0x08f096c8);
constexpr uint32_t InitPanelSet = off(0x08f0973c);
constexpr uint32_t InitPanelWindow = off(0x08f09978);
constexpr uint32_t DrawPanel = off(0x08f09b78);
constexpr uint32_t ForwardToCardList = off(0x08f0a71c);
constexpr uint32_t SetFadeMode = off(0x08f0a870);
constexpr uint32_t DrawFrame = off(0x08f0a928);
constexpr uint32_t DrawTextPanel = off(0x08f0b510);
constexpr uint32_t DrawOptionsPanel = off(0x08f0b690);
constexpr uint32_t DrawPulsingPanel = off(0x08f0b7ac);
constexpr uint32_t InitMessageWindow = off(0x08f0b954);
constexpr uint32_t DrawMessageBar = off(0x08f0bae8);
constexpr uint32_t DrawListPanel = off(0x08f0bc9c);
constexpr uint32_t ClearWorkBlock = off(0x08f0cc84);
constexpr uint32_t EnterDeckScreen = off(0x08f0f298);
constexpr uint32_t NewOptionsBlock = off(0x08f1803c);
constexpr uint32_t FreeOptionsBlock = off(0x08f180b0);
constexpr uint32_t InitOptionsBlock = off(0x08f180c0);
constexpr uint32_t SetOptionsByte = off(0x08f1854c);
constexpr uint32_t ConfigureColumn = off(0x08f1855c);
constexpr uint32_t SetOptionsEntry = off(0x08f18650);
constexpr uint32_t DrawCentredText = off(0x08f1aaac);
constexpr uint32_t DrawBoxedText = off(0x08f1abfc);
constexpr uint32_t DrawRect = off(0x08f1ac64);
constexpr uint32_t DrawTextLine = off(0x08f1ace4);
constexpr uint32_t EnterCardListScreen = off(0x08f1ad4c);
constexpr uint32_t BuildCardColumns = off(0x08f1af30);
constexpr uint32_t OptionsScreenStep = off(0x08f1afc0);
constexpr uint32_t EnterOptionsKind = off(0x08f1b24c);
constexpr uint32_t ClearCardFlags = off(0x08f1b588);
constexpr uint32_t FreeCardListBuffers = off(0x08f1b5b0);
constexpr uint32_t OptionsKindStep = off(0x08f1b65c);
constexpr uint32_t SetupCardListWindow = off(0x08f1b6d8);
constexpr uint32_t DuelSettingsStep = off(0x08f1b858);
constexpr uint32_t OptionsImageStep = off(0x08f1bca8);
constexpr uint32_t DrawCardListPage = off(0x08f1bd74);
constexpr uint32_t DrawCardHeader = off(0x08f1bec4);
constexpr uint32_t DrawOptionsScreen = off(0x08f1c0a0);
constexpr uint32_t OptionsSpriteRow = off(0x08f1c1c4);
constexpr uint32_t DrawOptionsLabel = off(0x08f1cc04);
constexpr uint32_t StartLoadC = off(0x08f1d9e8);
constexpr uint32_t LoadDoneC = off(0x08f1da58);
constexpr uint32_t FillColumn = off(0x08f1dba8);
constexpr uint32_t DrawCardLabel = off(0x08f1dd24);
constexpr uint32_t BuildCardListEntries = off(0x08f1df4c);
constexpr uint32_t CardClass = off(0x08f1e244);
constexpr uint32_t LastTypedEntryValue = off(0x08f1e28c);
/* Functions of the original game that the code above still calls: none of the recorded sessions runs them (or only the keyboard does), so
 * they stay generated code until a session goes through them. The names come from reading them. */
constexpr uint32_t CardAlbumFrame = off(0x08efe604);
constexpr uint32_t DuelRecordFrame = off(0x08efe768);
constexpr uint32_t TutorialListFrame = off(0x08efeb1c);
constexpr uint32_t DeckTutorialFrame = off(0x08efec90);
constexpr uint32_t StartupSequenceStep = off(0x08eff3b0);
constexpr uint32_t FreeSlotBlock = off(0x08f00ab4);
constexpr uint32_t DrawWindowBlock = off(0x08f017d0);
constexpr uint32_t StartFreeDuel = off(0x08f02bc0);
constexpr uint32_t NameEntryScreenStep = off(0x08f042c8);
constexpr uint32_t NameEntryLoadStep = off(0x08f0442c);
constexpr uint32_t LanguageChangedStep = off(0x08f08214);
constexpr uint32_t MainMenuStartStep = off(0x08f093f4);
constexpr uint32_t PlayerNameIsSet = off(0x08f094e4);
constexpr uint32_t DrawFramedBox = off(0x08f0a73c);
constexpr uint32_t ScrollThumbPosition = off(0x08f0af7c);
constexpr uint32_t DrawScrollBar = off(0x08f0b09c);
constexpr uint32_t InitDeckPanel = off(0x08f0f398);
constexpr uint32_t InitHelpWindow = off(0x08f0f500);
constexpr uint32_t Screen14Step = off(0x08f162f0);
constexpr uint32_t SetupPartnerColumns = off(0x08f18268);
constexpr uint32_t SetColumnTexture = off(0x08f18734);
constexpr uint32_t LoadColumnImage = off(0x08f18f3c);
constexpr uint32_t LoadCardListData = off(0x08f1b34c);
constexpr uint32_t EnterCardListKind3 = off(0x08f1c2ac);
constexpr uint32_t CardListSlideStep = off(0x08f1c4a8);
constexpr uint32_t CardListChoiceStep = off(0x08f1c6d8);
constexpr uint32_t DrawCardListKind3 = off(0x08f1ca6c);
constexpr uint32_t EnterCardListKind4 = off(0x08f1cca8);
constexpr uint32_t CardListKind4Step = off(0x08f1ce28);
constexpr uint32_t DrawCardListKind4 = off(0x08f1d7c8);
constexpr uint32_t StepNothing = off(0x08f1d9d8);
constexpr uint32_t DrawNothing = off(0x08f1d9e0);
constexpr uint32_t CardListWorkStep = off(0x08f1dab0);
constexpr uint32_t SetCardListFilter = off(0x08f1dc84);
constexpr uint32_t CardListPickStep = off(0x08f1e310);
constexpr uint32_t DrawCardListPage2 = off(0x08f1ebcc);
/* Original code of the screens that no recorded session has opened yet (story, duel start, deck, shop...). `Original_<address>` marks a function
 * that is still generated code; it gets a real name when a session goes through it and it becomes a Swap. */
constexpr uint32_t Original_08efe0f4 = off(0x08efe0f4);
constexpr uint32_t Original_08efe190 = off(0x08efe190);
constexpr uint32_t Original_08efe3a0 = off(0x08efe3a0);
constexpr uint32_t Original_08efe4c8 = off(0x08efe4c8);
constexpr uint32_t Original_08efe568 = off(0x08efe568);
constexpr uint32_t Original_08efe630 = off(0x08efe630);
constexpr uint32_t Original_08efe6d0 = off(0x08efe6d0);
constexpr uint32_t Original_08efe940 = off(0x08efe940);
constexpr uint32_t Original_08efea04 = off(0x08efea04);
constexpr uint32_t Original_08efeb48 = off(0x08efeb48);
constexpr uint32_t Original_08efebf0 = off(0x08efebf0);
constexpr uint32_t Original_08eff364 = off(0x08eff364);
constexpr uint32_t Original_08eff870 = off(0x08eff870);
constexpr uint32_t Original_08f00fa4 = off(0x08f00fa4);
constexpr uint32_t Original_08f010c0 = off(0x08f010c0);
constexpr uint32_t Original_08f02b34 = off(0x08f02b34);
constexpr uint32_t Original_08f02e50 = off(0x08f02e50);
constexpr uint32_t Original_08f04704 = off(0x08f04704);
constexpr uint32_t Original_08f0478c = off(0x08f0478c);
constexpr uint32_t Original_08f048bc = off(0x08f048bc);
constexpr uint32_t Original_08f07fe8 = off(0x08f07fe8);
constexpr uint32_t Original_08f0bf24 = off(0x08f0bf24);
constexpr uint32_t Original_08f0bfd0 = off(0x08f0bfd0);
constexpr uint32_t Original_08f0c2fc = off(0x08f0c2fc);
constexpr uint32_t Original_08f0cc90 = off(0x08f0cc90);
constexpr uint32_t NetworkSceneStep = off(0x08f0ce64);
constexpr uint32_t Original_08f0e9c4 = off(0x08f0e9c4);
constexpr uint32_t Original_08f16c18 = off(0x08f16c18);
/* Functions the game calls through pointers (callbacks stored in its blocks). */
constexpr uint32_t SceneEnterCallback = off(0x08efcd18);
constexpr uint32_t SceneLeaveCallback = off(0x08efcd20);
constexpr uint32_t DrawWindowCallback = off(0x08f0be00);   /* draw callback of the card list / dialog windows (draws their content) */
constexpr uint32_t DrawWindowCallbackB = off(0x08f1bf18);   /* second draw callback of the card list window */
constexpr uint32_t DrawCardCell = off(0x08f1bfbc);   /* draws one cell of the card list */
constexpr uint32_t DrawInfoWindowCallback = off(0x08f1c258);   /* draw callback of the card list info window */
constexpr uint32_t StartNameEntry = off(0x08f09280);       /* not a Swap yet: no recorded session reaches it */
constexpr uint32_t NameEntryStep = off(0x08f084dc);         /* not a Swap yet: drives the on-screen keyboard */
constexpr uint32_t MainMenuNameStep = off(0x08f092f4);
constexpr uint32_t MainMenuNameFinished = off(0x08f09384);
constexpr uint32_t StartRandomDuel = off(0x08f028e4);
constexpr uint32_t DrawTabHeader = off(0x08f04b90);
}  // namespace fn

/* ---- what more than one file of this folder uses ---- */

inline State &state(SwapCtx &x) { return x.at<State>(kStateAddr); }
inline void touch(Regions &r, uint32_t addr, uint32_t len) { r.emplace_back(addr, len); }

/* The game's sound numbers (nid::PlaySound) */
constexpr uint32_t kSoundCursor = 0x6212, kSoundConfirm = 0x6213, kSoundCancel = 0x6214, kSoundVolume = 0x6218, kSoundDialogOpen = 0x621A;

/* The image archive of the current language (the "logo index" is the language): the textures of every menu come from it */
inline uint32_t currentArchive(SwapCtx &x) { return x.at<uint32_t>(kLogoTableAddr + 4 * x.at<uint32_t>(kLogoIndexAddr)); }
inline const LogoSprite &logoSprite(SwapCtx &x, uint32_t i) { return x.at<LogoSprite>(x.addr(off(kLogoSprites)) + i * sizeof(LogoSprite)); }
inline uint32_t logoSpriteName(SwapCtx &x, uint32_t i) { return x.addr(off(kLogoSprites)) + i * sizeof(LogoSprite) + offsetof(LogoSprite, fileName); }

constexpr uint32_t kReadyAddr = 0x09B2CBF4;                /* s16 */
constexpr uint32_t kCharAlbumReadyAddr = 0x09B2C7A9;       /* byte: the character list/recipe viewer is up */

/* The save's record of one card (18 bytes, GetProgressRecord): how far the card is collected. */
struct ProgressRecord {
    uint16_t state;       /* +0 0..3, 3 = fully owned (CardState) */
    uint16_t count;       /* +2 non-zero when the player has the card */
    uint16_t card[7];     /* +4 the cards of a list record */
};
static_assert(sizeof(ProgressRecord) == 18, "ProgressRecord layout");

/* The items of the main menu (the list built by BuildTitleList), in screen order. kGraphics is the slot "Save Data" had in the original
 * game (it only refreshed the list); here it opens the Graphics screen. Its label is drawn by DrawListLabel. */
enum MainMenuItem : uint8_t { kStoryMode, kFreeDuel, kDeckMenu, kDatabase, kNetworkMode, kOptions, kGraphics, kLanguageSettings };

/* The Graphics screen is not in the original game: an in-game version of the window's Graphics menu (runtime/gpu/gfx_options.h). It runs as the
 * Options screen (17) with `active` set: same flow (fade in, input, fade out, back to the title list), another body. The main menu's Graphics
 * item sets `active`, its Options item clears it. Changes apply at once; confirm and cancel both leave the screen. */
struct GraphicsScreen {
    bool active = false;
    bool shown = false;        /* the first input step ran: draw the highlight bar */
    int32_t cursor = 0;        /* selected option, 0 .. GFXOPT_COUNT - 1 */
    uint32_t archive = 0;      /* the texture archive the Options sprites come from */
    uint32_t blink = 0;        /* frame counter of the blinking arrows */
};

extern GraphicsScreen graphics;

/* The items of the title list (the one under "PRESS START"), in screen order. The game itself only has the first two, in the order New Game,
 * Continue: here Continue comes first and Exit to Desktop is new (its label is drawn by drawAscii, the game has no text for it). */
enum TitleMenuItem : uint8_t { kContinue, kNewGame, kExitToDesktop, kTitleItemCount };

/* The game's own numbers for the two original items: its message texts 16 / 17 are their labels, and the list reported 0 / 1 when one was chosen */
constexpr uint32_t kNewGameText = 16, kContinueText = 17;
constexpr int32_t kOriginalNewGame = 0, kOriginalContinue = 1;

inline MenuList &menu(SwapCtx &x, uint32_t address) { return x.at<MenuList>(address); }

/* FUN_08f0139c(a, b) = FUN_08f013ac(a, 1, 0xff000000, b): fade to black; the callee is not swapped yet */
inline void fadeToBlack(SwapCtx &x, uint32_t a, uint32_t b) { x.callLocal(fn::StartFade, a, 1, 0xff000000, b); }

inline void regions_StateHead(SwapCtx &, Regions &r) { touch(r, kStateAddr, 0x24); }

/* FUN_08f02ff4(slot): size/handle query on the resource in a slot, -1 when empty */
uint32_t slotQuery(SwapCtx &x, uint32_t slot);

/* Draw an ASCII text (the engine wants UTF-16) at (px, py) in a field `width` pixels wide; with `centre`, a narrower text is centred in it.
 * Returns the new list. For the texts this screen adds to the game: the game's own texts come from its message files. */
uint32_t drawAscii(SwapCtx &x, uint32_t list, int32_t px, int32_t py, int32_t width, const char *text, bool centre);

/* FUN_08f0270c(p, i): colour of item i of a list: selected item's, else the list's normal/disabled colours */
uint32_t itemColor(SwapCtx &x, uint32_t p, uint32_t item);

/* the shared body of FUN_08f01860 (the callers 08f03ed8 and others reuse it) */
void initList(SwapCtx &x, uint32_t p);

inline void setStepResult(SwapCtx &x) { state(x).publishedArg0 = state(x).arg0; }   /* the original copies arg0 to +0x1E at the end */

struct Rgb { uint32_t r, g, b; };

/* A vertex colour (0xAABBGGRR) multiplied by a tint */
inline uint32_t tinted(uint32_t colour, Rgb tint) {
    const uint32_t r = (colour & 0xFF) * tint.r / 255, g = (colour >> 8 & 0xFF) * tint.g / 255, b = (colour >> 16 & 0xFF) * tint.b / 255;
    return (colour & 0xFF000000u) | b << 16 | g << 8 | r;
}

/* One neutral piece of the PC button across the row: the two caps and the stretched column, all from the half-height piece at line `v`.
 * All three are drawn scaled (caps 9 -> 10 px): the renderer samples a scaled sprite half a texel lower than an unscaled one, so mixing the
 * two showed as a step where the caps meet the middle. */
uint32_t drawModernPiece(SwapCtx &x, uint32_t list, int32_t rowX, int32_t rowY, int32_t width, int32_t v);

/* FUN_08f00c28(list, small, index, x, y, w, h, uOffset | stack: vOffset, mirrorU, mirrorV, colour): draw one sprite record at (x, y):
 * LogoSprite `index` (small = 0) or FramePart `index` (small != 0). A colour other than -1 is set first; w/h < 0 mean "the record's own
 * size"; mirrorU/mirrorV >= 0 replace the record's. uOffset/vOffset move the piece in its sheet (another colour set of the same piece). */
uint32_t drawSprite(SwapCtx &x, uint32_t list, uint32_t small, uint32_t index, uint32_t px, uint32_t py, int32_t w, int32_t h, int32_t uOffset, int32_t vOffset, int32_t mirrorU, int32_t mirrorV, uint32_t colour);

/* FUN_08f036e8(list, a1..a6, fontW | stack: fontH, colour): set the font size and colour, then draw FUN_08f0378c(list, a1..a6) */
uint32_t drawStyledText(SwapCtx &x, uint32_t list, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t fontW, uint32_t fontH, uint32_t colour);

/* The two sub lists of the title menu (screens 5 and 6) are built the same way: a list block (kTitleList2) with `itemCount` rows whose
 * position depends on the language, plus three colours stored in the first list block. Only the numbers differ. */
struct SubListShape {
    uint16_t screen;            /* value for the flow's arg0 */
    uint8_t itemCount;
    uint16_t yFirstLanguage;    /* y of the list when the system language is the first one (0) */
    uint16_t yOtherLanguages;
    uint32_t drawCallback;      /* offset of the function that draws one row */
    bool tightRows;             /* rows 5 and 6 get flag value 2 */
    bool highlightFirst;        /* run one list frame right away so the first row is selected */
};

void buildSubList(SwapCtx &x, const SubListShape &shape);

void leaveSubList(SwapCtx &x, State &s);

/* How the frames the Free Duel / Options / card list screens use look in the PC style: the body (kind 0, variant 3: light glass), the label strips
 * (kind 93, variant 14: pale gold glass) and the title and button bars (kind 84, variant 10: slanted, navy, like the menu buttons). Other frames
 * keep the game's own look. */
struct PanelStyle { bool slanted; Rgb fill, border; };
/* Dark blue glass with a light blue border, for every box that carries white text (questions, the help panel at the bottom of Options): o usuario, 2026-10-12. */
inline const PanelStyle kDarkPanelStyle = {false, {kDarkFill[0], kDarkFill[1], kDarkFill[2]}, {kDarkBorder[0], kDarkBorder[1], kDarkBorder[2]}};

/* The glass pieces of `style` over the rectangle, in the sheet that is bound now. `colour` is the vertex colour (its alpha fades the panel). */
uint32_t drawGlassPieces(SwapCtx &x, uint32_t list, uint32_t colour, int32_t px, int32_t py, int32_t width, int32_t height, const PanelStyle &style);

/* One frame of the name stage while the player types: set the message bar up the first time, then let the bar run its opening animation. */
void stepNameBox(SwapCtx &x, const std::wstring &description);

/* The name was confirmed or cancelled: give the main menu its own bar back and free the message. */
void closeNameBox(SwapCtx &x);

/* The button sheet of the lists: 20x24 cells, six per row (MenuList::spriteCell and its siblings pick one) */
constexpr int32_t kSheetColumns = 6, kCellWidth = 20, kCellHeight = 24;

/* The PC style (EnhancedTextures, marker menu_estilo_pc.txt; tools/mods/menu_estilo_pc.py draws it). The cells of the button sheet stay the game's:
 * other screens (Options, ...) cut them in the game's own way. The slanted PC button lives in a free corner of the sheet as two neutral pieces,
 * 20x12 each (the 24-line button squeezed to half height; drawn stretched back): the fill (grey, darker downwards) and the border with its glow
 * (white). Each is tinted with a colour derived from the row's cell colour. Left cap = columns 0..8, column 9 stretched across the row, right
 * cap = columns 10..18 (the same shape turned 180 degrees, so both ends match). The main menu's and the title list's icons are 16x16 pieces
 * of the same sheet (white, tinted when drawn), in item order (enum MainMenuItem, enum TitleMenuItem). */
constexpr int32_t kModernSheetU = 104, kModernFillV = 96, kModernBorderV = 108, kModernPieceHeight = 12;
constexpr int32_t kModernCap = 9, kModernStretched = 9, kModernRightCap = 10, kModernIconSize = 16;

/* The main colour of each cell of the button sheet (measured on the game's image; cells 16 and 17 do not exist) */
constexpr Rgb kSheetCellColours[20] = {
    {251, 147, 13}, {183, 79, 128}, {105, 78, 180}, {183, 144, 83}, {84, 175, 183}, {170, 184, 82},
    {86, 96, 134}, {163, 147, 104}, {83, 145, 192}, {129, 77, 193}, {174, 77, 192}, {193, 78, 133},
    {205, 7, 11}, {14, 30, 201}, {205, 7, 11}, {14, 30, 201}, {0, 0, 0}, {0, 0, 0},
    {110, 78, 108}, {81, 103, 108},
};
