/* Typed names for the calls into ehsys, the game's own engine library (BOOT; every Module calls it). Each wrapper only marshals
 * arguments into the PSP registers and calls the generated function, so behaviour is unchanged; what is known about each one comes
 * from reading its code. Names are by what the function does; "unk" marks what is not understood yet. The NID is the function's
 * identity in the game's export table. Grown on demand as Modules are decompiled (docs/DECOMP.md). */
#pragma once
#include "swap.h"

namespace ehsys {
/* What GetInput returns the address of. */
struct PadState {
    uint32_t unknown0;
    uint32_t pressed;                 /* +4 buttons that went down this frame */
    uint32_t unknown8;
    uint32_t pressedOrRepeating;      /* +12 buttons that went down or are auto-repeating */
};
static_assert(sizeof(PadState) == 16, "PadState layout");

namespace nid {
constexpr uint32_t AlphaFunc = 0xC8DF61CD;   /* (function, reference, mask) alpha test; function 6 = greater */
constexpr uint32_t DepthFunc = 0x348F1A33;   /* depth test function; 7 = always */
constexpr uint32_t DepthMask = 0xC0AB60EF;   /* 1 = depth writes disabled */
constexpr uint32_t DrawArray = 0x97DD82F2;   /* (primitive, vertex type, count, indices, vertices) draws vertices straight from memory */
constexpr uint32_t FitFontSize = 0x5F00A362;   /* (maxWidth in 1/64 pixel, text) the font size at which the text fits */
constexpr uint32_t GetUiFlag4 = 0x2F510B80;   /* the value SetUiFlag4 last set (read to restore it later) */
constexpr uint32_t GetWideScreenFlag = 0x5B08FB0D;   /* the value SetWideScreenFlag last set */
constexpr uint32_t MeasureTextBox = 0x87A64CC6;   /* (width, height in 1/64 pixel, text) size of the text laid out in a box: width << 16 | height */
constexpr uint32_t PrepareDuel = 0xF11F0D9A;   /* (setup block) commits the duel setup before scene 6 starts */
constexpr uint32_t MeasureTextInStyle = 0x63E8C033;   /* (style, text) width of a text in a style */   /* engine function ids (NIDs), by what the function does; "(guess)" = inferred from how the title code uses it */
constexpr uint32_t AddWindow = 0x50B8DF28;   /* (set, window) puts a window in the set; 0 when full */
constexpr uint32_t AdvanceFlow = 0xC5BDA06E;
constexpr uint32_t Alloc = 0x41AABF28;
constexpr uint32_t BeginList = 0xDFCA450B;
constexpr uint32_t BeginLoadWait = 0x6DD74ECC;   /* (guess) starts waiting for the loads */
constexpr uint32_t BeginSpriteDraw = 0x85F89FE8;   /* starts drawing a sprite resource */
constexpr uint32_t BeginText = 0xB8AD96EA;   /* starts a text run on the list */
constexpr uint32_t BindTexture = 0x9EA6989A;   /* texture handle */
constexpr uint32_t BuildSavePath = 0x68728B41;   /* (buffer, device, name) joins the device path and a file name */
constexpr uint32_t CardName = 0xAD0E1E03;
constexpr uint32_t CardAttribute = 0xE62CCE79;   /* (card id) attribute: 1 light, 2 dark, 3 water, 4 fire, 5 earth, 6 wind, 8 spell, 9 trap */
constexpr uint32_t CardIcon = 0xA72D71BF;   /* (card id) icon/type code; 2 selects the field-spell zone in the duel engine */
constexpr uint32_t CardIdsEquivalent = 0x6F2C8118;   /* (card id, card id): same id or a known alternate/related card id */
constexpr uint32_t CardIsMonster = 0xE5738C32;   /* (card id) true when its attribute code is below 8 */
constexpr uint32_t CardCategoryInEffectSet = 0xD934BB27;   /* (guess) (card id) true for categories 1,3,5..8,10..14; false for normal, fusion, ritual and token */
constexpr uint32_t CardState = 0x4F570B2C;   /* 3 = fully owned */
constexpr uint32_t CopyText = 0x025FE238;   /* (guess) (destination, source) copies a UTF-16 text */
constexpr uint32_t RaiseSaveValue = 0x617B0C6D;   /* (guess) (value, minimum) raises one of the save data's values to at least this */
constexpr uint32_t SetSaveFlag = 0xE9BCE3DF;   /* (flag) turns a save flag on */
constexpr uint32_t GetStorySaveBlock = 0x4605E594;   /* (guess) the address of the story's part of the save data */
constexpr uint32_t GetStartRequest = 0x885E0882;   /* (guess) the record that says how the story is entered (+0x20 the kind, +0x24 .. +0x2C its values) */
constexpr uint32_t GetSaveState = 0xD8A0DB10;   /* (guess) the save data's state record (+8 bit 0: set once the story has been played) */
constexpr uint32_t FinishStoryStart = 0x9A80BEE4;   /* (guess) tells the engine the story is ready */
constexpr uint32_t QueueLoad = 0x2BACAD04;   /* (kind, file name, callback, argument) puts a file or module in the load queue (kind 2 = module) */
constexpr uint32_t FadeOutSound = 0x088F768A;   /* (guess) fades the current sound scene out */
constexpr uint32_t IsSoundSceneBusy = 0xED17CBAC;   /* (guess) (scene) 1 while the scene is still loading or stopping */
constexpr uint32_t StopSoundScene = 0x65DA0419;   /* (guess) (scene) */
constexpr uint32_t UnloadSoundScene = 0xD5D33185;   /* (guess) (scene) */
constexpr uint32_t SetLoadWaitTime = 0xDF5CB459;   /* (guess) (frames) */
constexpr uint32_t CharacterCategory = 0x1EDFA6C8;   /* (guess) (character, out) the character's category 1..6 in *out; returns a 16-bit id */
constexpr uint32_t CharacterKind = 0x9D535D04;   /* (guess) which table a character id belongs to: 1 or 2 = the module's own table, else the engine's */
constexpr uint32_t CharAlbumStep = 0x99D1857C;
constexpr uint32_t CharaListStep = 0xFD65B042;   /* one frame of the character list (libcharalist): 1 or 2 when finished */
constexpr uint32_t CheckDeckRules = 0xDAEC7B62;   /* validates a deck record (sizes 40..80, extra 0..30) */
constexpr uint32_t Clear = 0xC62F9EAC;
constexpr uint32_t ClearSoundScene = 0x4433B331;   /* (guess) drops the current sound scene before a screen change */
constexpr uint32_t ClearWindow = 0xE1139F1A;   /* frees the window's items and sets its state to 0 */
constexpr uint32_t CopyDeckBuffer = 0xCD6A6481;   /* (out) copies the 0x108-byte deck buffer */
constexpr uint32_t DisableState = 0x4175E3DB;   /* GE state off */
constexpr uint32_t DrawFade = 0x878EE1D3;
constexpr uint32_t DrawSprite = 0xFDD168CA;   /* textured rectangle */
constexpr uint32_t DrawSpriteResource = 0x7187E0F0;   /* (list, sprite, frameState) draws one sprite resource */
constexpr uint32_t DrawText = 0xB89D38DC;
constexpr uint32_t DrawTextAligned = 0x2A36F037;
constexpr uint32_t DrawTextBox = 0x3BB2BAC6;   /* (x, y, width, height in 1/64 pixel, text, colour) text laid out in a box */
constexpr uint32_t DrawWindow = 0xEF9B5D06;   /* draws a window block onto the command list */
constexpr uint32_t EnableState = 0xF462EE6E;   /* GE state on */
constexpr uint32_t EndList = 0x1EC5342B;
constexpr uint32_t EndLoadWait = 0x74CA1190;   /* (guess) ends the wait for loads */
constexpr uint32_t EndSpriteDraw = 0x89812D8A;   /* ends the sprite drawing */
constexpr uint32_t EndText = 0xA4AFF8E6;   /* ends a text run */
constexpr uint32_t FadeFrom = 0xD470D0B2;
constexpr uint32_t FadeTo = 0x5998BFD7;
constexpr uint32_t FileSize = 0xD2A768F4;
constexpr uint32_t FilterInput = 0xF843E20A;   /* (pad) clears the pad bits while a dialog is open */
constexpr uint32_t FindTexture = 0xC6C15111;   /* texture handle inside an archive */
constexpr uint32_t Format = 0xE50B9594;   /* sprintf with arguments */
constexpr uint32_t FrameCounter = 0xB2B9FEFA;   /* guest address of {+0 frame number, +4 blink period} */
constexpr uint32_t Free = 0x20E340D9;
constexpr uint32_t FreeWindowSet = 0x5DF04F49;   /* frees the 16 windows of a set */
constexpr uint32_t GetButtonMask = 0x8171F765;   /* pad mask of a button role: 0 = confirm, 1 = cancel (the confirm button depends on the region) */
constexpr uint32_t GetCardAnimation = 0x085FA98E;   /* 0..3 */
constexpr uint32_t GetCardIndex = 0x3CD5EF4C;   /* card id -> table index; 0 when out of range */
constexpr uint32_t GetCardRecordData = 0xE1677D35;   /* (count) data of a record count */
constexpr uint32_t GetClock = 0xED2013FB;   /* (out) copies the 8 byte clock */
constexpr uint32_t GetDuelOption = 0xD6E88BCA;   /* (0 duel effects, 2 activation confirm, 3 auto draw) 0/1 */
constexpr uint32_t GetFontFlags = 0x0D961646;   /* 16-bit font flag word */
constexpr uint32_t GetInput = 0xF5E3080C;   /* pad state */
constexpr uint32_t GetLanguage = 0x31454993;   /* system language 0..5 */
constexpr uint32_t GetLanguageText = 0x4510ECA2;   /* (table, index) string of the current language */
constexpr uint32_t GetLineHeight = 0x2FD4863E;   /* text line height in 1/64 pixels */
constexpr uint32_t GetMessageSpeed = 0x7790CFC1;   /* 0..2 */
constexpr uint32_t IsNetworkHost = 0x906A916B;   /* (guess) in a network duel, whether this console is the host */
constexpr uint32_t GetNetworkScene = 0x1460C6FD;   /* (guess) value set by SetNetworkScene */
constexpr uint32_t GetPlayerName = 0x1BFCC25C;   /* guest address of the player's name */
constexpr uint32_t GetProgressFlags = 0xF6213DFA;   /* (out) 16-bit progress flags */
constexpr uint32_t GetProgressRecord = 0xE29EEFC7;   /* address of the 18-byte progress record of a character */
constexpr uint32_t GetProgressSlot = 0x8A5C1959;   /* address of a progress slot */
constexpr uint32_t GetSaveBlock = 0xA64E402F;
constexpr uint32_t GetSaveKind = 0x342A4009;   /* kind of the loaded save */
constexpr uint32_t GetSaveProgress = 0xD09C2502;   /* 0..2 */
constexpr uint32_t GetSaveRecordOffset = 0xB6E8E7AE;   /* (index) offset of a record in the save */
constexpr uint32_t GetSaveSource = 0xBAC2BA34;   /* where the save came from (0 save, 1 card data, 2 and 3 none) */
constexpr uint32_t GetSettingsBlock = 0x88B6C7F0;   /* guest address of the engine's settings block */
constexpr uint32_t LeaveNetworkScene = 0xA94DF30B;   /* (guess) called when a network duel's scene ends */
constexpr uint32_t SetSceneMode = 0xD7F13632;   /* (guess) (mode) the result screen's exit sets 0 */
constexpr uint32_t ReleaseSceneResources = 0x2E783F70;   /* (guess) the last call of the duel's exit */
constexpr uint32_t GetNetworkPlayerTarget = 0xF59F3BD3;   /* (guess) how many players' answers a network duel waits for */
constexpr uint32_t GetNetworkPlayerAnswers = 0x6919FD3F;   /* (guess) how many have answered so far */
constexpr uint32_t ReceiveNetworkMessage = 0x3F68D487;   /* (guess) (type, unknown, unknown): nonzero when such a message arrived */
constexpr uint32_t SetNetworkFlag = 0x71B237CA;   /* (guess) a flag of the network layer, set by the result screen */
constexpr uint32_t SendNetworkMessage = 0x0EBC2488;   /* (guess) (message of 0x100 bytes: kind, arguments) */
constexpr uint32_t GetSpritePlayback = 0xB8770B92;   /* (guess) (sprite, out, out) writes two values of a sprite resource's animation; the second one is the frame it is on */
constexpr uint32_t GetSpriteFrameCount = 0x558B2DCB;   /* frame count of a sprite resource; -1 when null */
constexpr uint32_t GetText = 0xE58C0FDC;   /* string from a string table */
constexpr uint32_t GetTutorial = 0xFD0CE644;   /* 0/1 */
constexpr uint32_t GetUiMode = 0x1F190478;   /* layout mode of the menus (0, 1 or 3 pick the shift/offsets; the PSP build returns 6) */
constexpr uint32_t GetVolume = 0x94A4EC34;   /* (channel) 0..15 */
constexpr uint32_t GetWindowContentRect = 0x6B9DF7E8;   /* (windows, style, outX, outY, outW, outH) */
constexpr uint32_t InitHeap = 0x1856E536;
constexpr uint32_t InitWindowSet = 0x30FECAEF;   /* (set, heap) clears a window set and loads the window sheet */
constexpr uint32_t IsFadeDone = 0x450CD6C5;
constexpr uint32_t IsLoadDone = 0x4E3BEA66;   /* (guess) true when the intro's loading has finished */
constexpr uint32_t IsSoundLoadIdle = 0xDCA0DCE1;   /* (guess) 1 when no sound bank is loading */
constexpr uint32_t LayoutWindow = 0x2012884C;   /* sizes the window from its items and sets its state to 1 */
constexpr uint32_t LimitListPoll = 0x56E1CA7A;
constexpr uint32_t LimitListStart = 0xE69A45D2;
constexpr uint32_t LimitListStep = 0x780BA2AD;   /* one frame of the limit list (liblimitlist) */
constexpr uint32_t LoadModule = 0x2BACAD04;
constexpr uint32_t LoadSoundBanks = 0x70CB33BE;   /* loads sound banks 2, 3, 4, 5 */
constexpr uint32_t MeasureTextWidth = 0x66F8E113;   /* text width in 1/64 pixels */
constexpr uint32_t MemSet = 0x10F3BB61;   /* ehsys memset (goes through the guest stores) */
constexpr uint32_t PendingLoadsA = 0x06A1D1D8;   /* number of loads still running (group A) */
constexpr uint32_t PendingLoadsB = 0x04E1CB0F;   /* number of loads still running (group B) */
constexpr uint32_t WaitForLoadGroupA = 0x8A309EAF;   /* (guess) wait for field resource loads */
constexpr uint32_t PlayerLevel = 0x61AE6357;
constexpr uint32_t PlaySound = 0x2AE3C0AD;   /* sound effect id */
constexpr uint32_t PollDataLoad = 0x778817A2;   /* 1 when the started load has finished */
constexpr uint32_t PrepareSaveDialog = 0xB48A11FB;   /* (path, flags, size) sets up the save/load dialog with the current volume */
constexpr uint32_t PushScene = 0x551A8EF2;   /* (scene, a, b) pushes a scene on the scene stack */
constexpr uint32_t RandomNumber = 0x60054052;   /* 31-bit random number */
constexpr uint32_t NextRandom = 0x74510178;   /* (guess) random number; the field module uses its low 16 bits */
constexpr uint32_t ReadDeckRecord = 0x85A292D7;   /* (id, out) reads a deck record from the archive */
constexpr uint32_t ReadFileAsync = 0x8288EEBC;
constexpr uint32_t RecipeViewerStep = 0x363D43C5;
constexpr uint32_t RecordChapterCard = 0xDF779CF3;   /* (chapter, card) */
constexpr uint32_t RegisterArchive = 0x4B0DABFA;
constexpr uint32_t ReleaseScreenResources = 0xB80F59AD;   /* (guess) frees what the screen loaded before it unloads its helper module */
constexpr uint32_t ReserveListData = 0xDCFC7586;   /* (address of the cursor, bytes) reserves room for vertices inside the display list; returns its address */
constexpr uint32_t ResetCursorLimit = 0xC721E899;   /* (guess) keeps the cursor index within the list size */
constexpr uint32_t ResetWindow = 0xB0F320E6;   /* puts a window block back to its first state */
constexpr uint32_t SaveFlag = 0xE4D6C77A;
constexpr uint32_t SaveHasErrors = 0xE36AFCA5;   /* 0 when the save is fine */
constexpr uint32_t SeedRandom = 0xA200D82D;   /* seeds the random generator with the current time */
constexpr uint32_t SetAttractTimer = 0x6367AE39;   /* (guess) frames until the title goes to its demo again; set to 600 after a menu action */
constexpr uint32_t SetBlend = 0x5CBB9E43;   /* blend equation */
constexpr uint32_t SetBlendColor = 0x23E69878;   /* colour the next sprites are blended with */
constexpr uint32_t SetCardAnimation = 0xBBAEFACC;   /* 0..3 */
constexpr uint32_t SetCardState = 0x91517142;   /* (card, state) raises the state and updates the save flags */
constexpr uint32_t SetClearColor = 0x1D98951B;   /* state only */
constexpr uint32_t SetClearDepth = 0x42110BF9;   /* state only */
constexpr uint32_t SetClearStencil = 0x399AC68A;   /* state only */
constexpr uint32_t SetColor = 0xCABEA834;   /* ARGB of the next primitives */
constexpr uint32_t SetDuelOption = 0x0EDCABED;   /* (index as GetDuelOption, on) */
constexpr uint32_t SetFontFlags = 0x06380DFA;   /* 16-bit font flag word */
constexpr uint32_t SetFontSize = 0x942B03D0;   /* width, height */
constexpr uint32_t SetLayerFade = 0x0D3E2FA6;   /* (guess) (layer, frames) */
constexpr uint32_t SetMessageSpeedSetting = 0xF2E1778A;   /* 0..2 */
constexpr uint32_t SetNetworkScene = 0x8F7249A7;   /* (guess) 1 while a scene that needs the network is entered */
constexpr uint32_t SetSceneCallbacks = 0xC2E59A21;   /* (scene, onEnter, onLeave) slot of the scene stack; returns the previous one */
constexpr uint32_t SetScissor = 0x20560342;
constexpr uint32_t SetScratchBuffer = 0x50EA4D71;   /* (address, size) the block the engine uses as scratch memory */
constexpr uint32_t SetScreenOverlay = 0x13A4081A;   /* on/off */
constexpr uint32_t SetSpriteFrame = 0x831C5769;   /* (sprite, frame, last frame) restarts an animation */
constexpr uint32_t SetSpriteSize = 0x8EA6B1E7;   /* source and destination size of the next sprites */
constexpr uint32_t SetTextColor = 0x60B55A50;   /* ARGB */
constexpr uint32_t SetTextFlags = 0xAB962AE7;   /* text drawing flags */
constexpr uint32_t SetTextureFilter = 0xE6E4401F;   /* min and mag filter */
constexpr uint32_t SetTextureFunction = 0x09794DF0;   /* texture function and alpha use */
constexpr uint32_t SetTextureMode = 0x572A577B;   /* (pixel format, mip levels, unknown, swizzle) */
constexpr uint32_t SetTextureWrap = 0xF17C5C28;   /* (u, v) wrap mode; 0 = repeat */
constexpr uint32_t SetTutorial = 0xEFA3AD6C;   /* 0/1 */
constexpr uint32_t SetTutorialAndMessageSpeed = 0x1F710553;   /* low 3 bits of the first settings byte: tutorial (bit 0) and message speed (bits 1-2) */
constexpr uint32_t SetUiFlag1 = 0x33ED567B;   /* bit of the global UI flag word */
constexpr uint32_t SetUiFlag2 = 0x4AA58320;   /* bit of the global UI flag word */
constexpr uint32_t SetUiFlag4 = 0xC9D9E989;   /* bit of the global UI flag word */
constexpr uint32_t SetUiFlag8 = 0xD853F15C;   /* bit of the global UI flag word */
constexpr uint32_t SetVolume = 0xFFA11C10;   /* (channel, 0..15): stores the setting and applies it */
constexpr uint32_t SetWideScreenFlag = 0xC792FA38;   /* (guess) bit 15 of the font flags: 1 = widened line height */
constexpr uint32_t Sort = 0x1B00FB95;
constexpr uint32_t StartCharaList = 0x0B63A183;   /* (mode, heap) starts the character list module */
constexpr uint32_t StartDataLoad = 0x6FFA2A28;   /* (kind, heap) allocates the buffers and starts reading the data archive; 0 when out of memory */
constexpr uint32_t StartLanguageDataLoad = 0x5836AFF1;   /* starts loading the card/text archive of the current language */
constexpr uint32_t StartSaveReader = 0xE11636EC;   /* (heap) starts reading the save's data */
constexpr uint32_t StartSoundScene = 0xC859D5FE;   /* (guess) starts the sound scene with this id (330 = menu, 327 = menu music) */
constexpr uint32_t StopSoundChannel = 0xBE3756D0;   /* (guess) 4 / 5 = the two menu loops */
constexpr uint32_t StringLength = 0x82E739A4;   /* length of a 16-bit string */
constexpr uint32_t UnloadSoundBanks = 0x245526EE;   /* unloads sound banks 5, 4, 3, 2 */
constexpr uint32_t UpdateSaveChecksum = 0xA045CACE;   /* recomputes the save's checksum */
constexpr uint32_t UpdateWindow = 0x39ADDA9F;   /* one frame of a window block; its answer */
constexpr uint32_t WaitFrame = 0xF1BC43DB;   /* lets one frame pass (ends the thread's frame) */
}  // namespace nid



/* ---- memory: a stack-like heap over a block of memory (a "heap handle" is a guest pointer) ---- */
inline uint32_t InitHeap(SwapCtx &x, uint32_t memory, uint32_t size) { return x.callNid(nid::InitHeap, memory, size); }
inline uint32_t Alloc(SwapCtx &x, uint32_t heap, uint32_t size) { return x.callNid(nid::Alloc, heap, size); }   /* 16-byte aligned; 0 on failure */
inline void Free(SwapCtx &x, uint32_t heap, uint32_t block) { x.callNid(nid::Free, heap, block); }              /* block = pointer returned by Alloc */

/* ---- utilities ---- */
inline void Sort(SwapCtx &x, uint32_t base, uint32_t count, uint32_t elementSize, uint32_t compare) { x.callNid(nid::Sort, base, count, elementSize, compare); }   /* qsort; compare is a guest address */
inline uint32_t GetLanguage(SwapCtx &x) { return x.callNid(nid::GetLanguage); }                                        /* system language 0..5 */

/* ---- files and archives ---- */
inline uint32_t FileSize(SwapCtx &x, uint32_t device, uint32_t path) { return x.callNid(nid::FileSize, device, path); }                                                 /* -1 when missing */
inline void ReadFileAsync(SwapCtx &x, uint32_t device, uint32_t path, uint32_t buffer, uint32_t size, uint32_t onDone, uint32_t userArg) { x.callNid(nid::ReadFileAsync, device, path, buffer, size, onDone, userArg); }
inline uint32_t RegisterArchive(SwapCtx &x, uint32_t slot, uint32_t archive) { return x.callNid(nid::RegisterArchive, slot, archive); }               /* an .ehp loaded in memory; 0 when the slot is taken */
inline uint32_t FindTexture(SwapCtx &x, uint32_t archive, uint32_t name, uint32_t flags) { return x.callNid(nid::FindTexture, archive, name, flags); }   /* texture handle inside an archive */

/* ---- save data, text and card collection ---- */
inline uint32_t GetSaveBlock(SwapCtx &x) { return x.callNid(nid::GetSaveBlock); }       /* guest address of the in-memory save data */
inline bool SaveFlag(SwapCtx &x, uint32_t flag) { return x.callNid(nid::SaveFlag, flag) != 0; }
inline uint32_t PlayerLevel(SwapCtx &x) { return x.callNid(nid::PlayerLevel); }
inline uint32_t CardState(SwapCtx &x, uint32_t card) { return x.callNid(nid::CardState, card); }   /* 3 = fully owned */
inline uint32_t GetUiFlag4(SwapCtx &x) { return x.callNid(nid::GetUiFlag4); }   /* read it before changing it and set it back afterwards */
inline uint32_t GetWideScreenFlag(SwapCtx &x) { return x.callNid(nid::GetWideScreenFlag); }
inline void SetWideScreenFlag(SwapCtx &x, uint32_t on) { x.callNid(nid::SetWideScreenFlag, on); }
inline void SetFontSize(SwapCtx &x, uint32_t w, uint32_t h) { x.callNid(nid::SetFontSize, w, h); }
inline int32_t GetLineHeight(SwapCtx &x) { return (int32_t)x.callNid(nid::GetLineHeight); }
inline uint32_t GetText(SwapCtx &x, uint32_t table, uint32_t index) { return x.callNid(nid::GetText, table, index); }   /* string from a string table */
inline int32_t MeasureText(SwapCtx &x, uint32_t style, uint32_t text) { return (int32_t)x.callNid(nid::MeasureTextInStyle, style, text); }
inline uint32_t CardName(SwapCtx &x, uint32_t card) { return x.callNid(nid::CardName, card); }                        /* guest address of a card's name */
inline void Format(SwapCtx &x, uint32_t out, uint32_t format, uint32_t arg) { x.callNid(nid::Format, out, format, arg); }   /* sprintf with one argument */

/* ---- input and sound ---- */
inline uint32_t GetInput(SwapCtx &x) { return x.callNid(nid::GetInput); }          /* guest address of the pad state: +4 buttons pressed this frame, +12 pressed or auto-repeating */
inline void PlaySound(SwapCtx &x, uint32_t effect) { x.callNid(nid::PlaySound, effect); }

/* ---- screen transitions and global switches ---- */
inline void FadeFrom(SwapCtx &x, uint32_t mode, uint32_t color, uint32_t frames) { x.callNid(nid::FadeFrom, mode, color, frames); }   /* fade in from a colour */
inline void FadeTo(SwapCtx &x, uint32_t mode, uint32_t color, uint32_t frames) { x.callNid(nid::FadeTo, mode, color, frames); }       /* fade out to a colour */
inline bool IsFadeDone(SwapCtx &x) { return x.callNid(nid::IsFadeDone) & 1; }
inline void SetUiFlag1(SwapCtx &x, bool on) { x.callNid(nid::SetUiFlag1, on); }   /* bits of a global UI flag word */
inline void SetUiFlag2(SwapCtx &x, bool on) { x.callNid(nid::SetUiFlag2, on); }
inline void SetUiFlag4(SwapCtx &x, bool on) { x.callNid(nid::SetUiFlag4, on); }
inline void SetUiFlag8(SwapCtx &x, bool on) { x.callNid(nid::SetUiFlag8, on); }
inline void SetScreenOverlay(SwapCtx &x, bool enable) { x.callNid(nid::SetScreenOverlay, enable); }

/* The states EnableState / DisableState switch (the numbers of the PSP's graphics library). */
enum GuState : uint32_t { kAlphaTest = 0, kDepthTest = 1, kScissorTest = 2, kStencilTest = 3, kBlend = 4, kCullFace = 5, kDither = 6, kFog = 7, kClipPlanes = 8, kTexture2D = 9 };

/* ---- display lists. A "cursor" is the write position in the GE command list; most calls append commands and return the new cursor.
 * Several only set global drawing state (marked), and the text calls work between BeginText and EndText. ---- */
inline uint32_t BeginList(SwapCtx &x, uint32_t list, uint32_t remainingOut = 0) { return x.callNid(nid::BeginList, list, remainingOut); }   /* start recording list 0..; returns the cursor */
inline void EndList(SwapCtx &x, uint32_t cursor) { x.callNid(nid::EndList, cursor); }
inline uint32_t SetClearColor(SwapCtx &x, uint32_t cur, uint32_t argb) { return x.callNid(nid::SetClearColor, cur, argb); }   /* state only */
inline uint32_t SetClearDepth(SwapCtx &x, uint32_t cur, uint32_t v) { return x.callNid(nid::SetClearDepth, cur, v); }        /* state only */
inline uint32_t SetClearStencil(SwapCtx &x, uint32_t cur, uint32_t v) { return x.callNid(nid::SetClearStencil, cur, v); }      /* state only */
inline uint32_t Clear(SwapCtx &x, uint32_t cur, uint32_t flags) { return x.callNid(nid::Clear, cur, flags); }        /* flags 0x17: colour, stencil and depth */
inline uint32_t EnableState(SwapCtx &x, uint32_t cur, uint32_t state) { return x.callNid(nid::EnableState, cur, state); }  /* 2 = scissor */
inline uint32_t DisableState(SwapCtx &x, uint32_t cur, uint32_t state) { return x.callNid(nid::DisableState, cur, state); }
inline uint32_t SetTextureFunction(SwapCtx &x, uint32_t cur, uint32_t func, uint32_t useAlpha) { return x.callNid(nid::SetTextureFunction, cur, func, useAlpha); }
inline uint32_t SetTextureFilter(SwapCtx &x, uint32_t cur, uint32_t min, uint32_t mag) { return x.callNid(nid::SetTextureFilter, cur, min, mag); }
inline uint32_t SetBlend(SwapCtx &x, uint32_t cur, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { return x.callNid(nid::SetBlend, cur, a, b, c, d, e); }
inline uint32_t SetScissor(SwapCtx &x, uint32_t cur, int32_t px, int32_t py, int32_t w, int32_t h) { return x.callNid(nid::SetScissor, cur, px, py, w, h); }
inline uint32_t BindTexture(SwapCtx &x, uint32_t cur, uint32_t texture) { return x.callNid(nid::BindTexture, cur, texture); }
inline uint32_t SetColor(SwapCtx &x, uint32_t cur, uint32_t argb) { return x.callNid(nid::SetColor, cur, argb); }
inline uint32_t SetBlendColor(SwapCtx &x, uint32_t cur, uint32_t argb) { return x.callNid(nid::SetBlendColor, cur, argb); }   /* colour the next sprites are blended with */
inline uint32_t SetSpriteSize(SwapCtx &x, uint32_t cur, int32_t srcW, int32_t srcH, int32_t dstW, int32_t dstH) { return x.callNid(nid::SetSpriteSize, cur, srcW, srcH, dstW, dstH); }   /* state only: used by the next DrawSprite calls */
inline uint32_t DrawSprite(SwapCtx &x, uint32_t cur, int32_t px, int32_t py, int32_t z, int32_t u, int32_t v, int32_t mirrorU, int32_t mirrorV) { return x.callNid(nid::DrawSprite, cur, px, py, z, u, v, mirrorU, mirrorV); }   /* textured rectangle at (px,py) from texture (u,v) */
inline uint32_t AlphaFunc(SwapCtx &x, uint32_t cur, uint32_t func, uint32_t ref, uint32_t mask) { return x.callNid(nid::AlphaFunc, cur, func, ref, mask); }
inline uint32_t DepthFunc(SwapCtx &x, uint32_t cur, uint32_t func) { return x.callNid(nid::DepthFunc, cur, func); }
inline uint32_t DepthMask(SwapCtx &x, uint32_t cur, uint32_t disable) { return x.callNid(nid::DepthMask, cur, disable); }
inline uint32_t SetTextureMode(SwapCtx &x, uint32_t cur, uint32_t format, uint32_t mipLevels, uint32_t unknown, uint32_t swizzle) { return x.callNid(nid::SetTextureMode, cur, format, mipLevels, unknown, swizzle); }
inline uint32_t SetTextureWrap(SwapCtx &x, uint32_t cur, uint32_t u, uint32_t v) { return x.callNid(nid::SetTextureWrap, cur, u, v); }
inline uint32_t DrawArray(SwapCtx &x, uint32_t cur, uint32_t primitive, uint32_t vertexType, uint32_t count, uint32_t indices, uint32_t vertices) { return x.callNid(nid::DrawArray, cur, primitive, vertexType, count, indices, vertices); }
inline uint32_t ReserveListData(SwapCtx &x, uint32_t cursorAddress, uint32_t bytes) { return x.callNid(nid::ReserveListData, cursorAddress, bytes); }
inline uint32_t DrawFade(SwapCtx &x, uint32_t cursorAddress) { return x.callNid(nid::DrawFade, cursorAddress); }       /* the fade overlay; takes the address of the cursor and updates it */
inline uint32_t FrameCounter(SwapCtx &x) { return x.callNid(nid::FrameCounter); }                                           /* guest address of {+0 frame number, +4 blink period} */

/* ---- sprite resources: BeginSpriteDraw ... EndSpriteDraw around the draws of one or more sprite resources ---- */
inline uint32_t BeginSpriteDraw(SwapCtx &x, uint32_t cur) { return x.callNid(nid::BeginSpriteDraw, cur); }
inline uint32_t DrawSpriteResource(SwapCtx &x, uint32_t cur, uint32_t sprite, uint32_t frameState) { return x.callNid(nid::DrawSpriteResource, cur, sprite, frameState); }   /* frameState: {+0 the animation frame, ...} */
inline uint32_t EndSpriteDraw(SwapCtx &x, uint32_t cur) { return x.callNid(nid::EndSpriteDraw, cur); }
inline void GetSpritePlayback(SwapCtx &x, uint32_t sprite, uint32_t outA, uint32_t outB) { x.callNid(nid::GetSpritePlayback, sprite, outA, outB); }   /* (guess) see the nid */
inline void SetAttractTimer(SwapCtx &x, uint32_t frames) { x.callNid(nid::SetAttractTimer, frames); }   /* frames until the demo starts when nobody presses anything */
inline bool IsNetworkHost(SwapCtx &x) { return x.callNid(nid::IsNetworkHost) != 0; }
inline bool GetNetworkScene(SwapCtx &x) { return x.callNid(nid::GetNetworkScene) != 0; }   /* the scene is a network duel */
inline int32_t UpdateWindow(SwapCtx &x, uint32_t window) { return (int32_t)x.callNid(nid::UpdateWindow, window); }   /* one frame of a window block; -1 while it is open, otherwise the chosen button */
inline void ClearWindow(SwapCtx &x, uint32_t window) { x.callNid(nid::ClearWindow, window); }
inline uint32_t GetNetworkPlayerTarget(SwapCtx &x) { return x.callNid(nid::GetNetworkPlayerTarget); }
inline int32_t GetNetworkPlayerAnswers(SwapCtx &x) { return (int32_t)x.callNid(nid::GetNetworkPlayerAnswers); }
inline bool ReceiveNetworkMessage(SwapCtx &x, uint32_t type, uint32_t a, uint32_t b) { return x.callNid(nid::ReceiveNetworkMessage, type, a, b) != 0; }
inline void SetNetworkFlag(SwapCtx &x, uint32_t value) { x.callNid(nid::SetNetworkFlag, value); }
inline void SendNetworkMessage(SwapCtx &x, uint32_t message) { x.callNid(nid::SendNetworkMessage, message); }
inline void LeaveNetworkScene(SwapCtx &x) { x.callNid(nid::LeaveNetworkScene); }   /* (guess) see the nid */
inline void SetSceneMode(SwapCtx &x, uint32_t mode) { x.callNid(nid::SetSceneMode, mode); }   /* (guess) see the nid */
inline void ReleaseSceneResources(SwapCtx &x) { x.callNid(nid::ReleaseSceneResources); }   /* (guess) see the nid */
inline uint32_t GetSettingsBlock(SwapCtx &x) { return x.callNid(nid::GetSettingsBlock); }   /* guest address of the in-memory settings (+0x119, +0x11d: the duel's network mode bytes) */
inline uint32_t GetStartRequest(SwapCtx &x) { return x.callNid(nid::GetStartRequest); }   /* guest address of the record describing what was asked to start (the duel setup) */
inline uint32_t GetButtonMask(SwapCtx &x, uint32_t player) { return x.callNid(nid::GetButtonMask, player); }   /* the buttons that count as input for a player */
inline void SetSpriteFrame(SwapCtx &x, uint32_t sprite, uint32_t frame, uint32_t lastFrame) { x.callNid(nid::SetSpriteFrame, sprite, frame, lastFrame); }   /* restarts an animation at frame, ending at lastFrame */
inline void LayoutWindow(SwapCtx &x, uint32_t window) { x.callNid(nid::LayoutWindow, window); }   /* sizes a window block from its text and buttons */
inline uint32_t GetSpriteFrameCount(SwapCtx &x, uint32_t sprite) { return x.callNid(nid::GetSpriteFrameCount, sprite); }   /* frame count of a sprite resource; -1 when null */

/* ---- text, drawn between BeginText and EndText; positions and widths in 1/64 pixel ---- */
inline void BeginText(SwapCtx &x, uint32_t cur) { x.callNid(nid::BeginText, cur); }
inline uint32_t EndText(SwapCtx &x) { return x.callNid(nid::EndText); }                                                /* returns the cursor */
inline void SetTextColor(SwapCtx &x, uint32_t argb) { x.callNid(nid::SetTextColor, argb); }
inline void SetTextFlags(SwapCtx &x, uint32_t flags) { x.callNid(nid::SetTextFlags, flags); }                               /* 0x280 / 0x180: alignment by language */
inline void DrawText(SwapCtx &x, int32_t px, int32_t py, int32_t maxWidth, uint32_t text) { x.callNid(nid::DrawText, px, py, maxWidth, text); }
inline void DrawTextAligned(SwapCtx &x, int32_t px, int32_t py, int32_t width, uint32_t text) { x.callNid(nid::DrawTextAligned, px, py, width, text); }
inline void DrawTextBox(SwapCtx &x, int32_t px, int32_t py, int32_t w, int32_t h, uint32_t text, uint32_t argb) { x.callNid(nid::DrawTextBox, px, py, w, h, text, argb); }
inline uint32_t MeasureTextBox(SwapCtx &x, int32_t width, int32_t height, uint32_t text) { return x.callNid(nid::MeasureTextBox, width, height, text); }
inline uint32_t FitFontSize(SwapCtx &x, uint32_t maxWidth, uint32_t text) { return x.callNid(nid::FitFontSize, maxWidth, text); }
inline int32_t TextWidth(SwapCtx &x, uint32_t text) { return (int32_t)x.callNid(nid::MeasureTextWidth, text); }

}  // namespace ehsys
