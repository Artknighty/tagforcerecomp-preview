/* libfield: the flow of the map: a state machine (`UpdateFlow`) that follows what the player does: walking, talking to the character they stand next to,
 * the card shop sign, the place menu... Each step starts a screen or waits for one to finish, and a finished screen leaves its answer in `flow.answer`
 * before step 0x17 hands it to the story. */
#include "../swap.h"
#include "../ehsys.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

enum FlowStep : int32_t {
    kStepStart = 0, kStepWalking = 1, kStepAskTalk = 2, kStepAskTalkWait = 3, kStepWait4 = 4, kStepTalk = 5, kStepTalkWait = 6, kStepShop = 7, kStepShopWait = 8,
    kStepMessage = 9, kStepMessageWait = 10, kStepMenu = 11, kStepMenuWait = 12, kStepCallA = 13, kStepCallAWait = 14, kStepCallB = 15, kStepCallBWait = 16,
    kStepPanel = 17, kStepPanelWait = 18, kStepList = 19, kStepListWait = 20, kStepRequestA = 21, kStepRequestB = 22, kStepAnswer = 23,
};
constexpr uint32_t kSystemSpriteTexts = 5;                            /* the system sprite archive that holds the field's texts */
constexpr uint32_t kTextAskTalk = 0x66, kTextAskTalkMore = 0x65;
constexpr int32_t kNoCharacter = -1;

/* the answers the story gets (flow.answer) */
enum Answer : int32_t { kAnswerTalk = 1, kAnswerMenuA = 2, kAnswerMenuB = 3, kAnswerMenuC = 4, kAnswerMessageA = 5, kAnswerMenu8 = 8, kAnswerMenu9 = 9, kAnswerList = 0x14 };

bool isMessageAnswer(int32_t answer) { return answer == 5 || answer == 4 || answer == 3 || answer == 2; }

uint32_t systemText(SwapCtx &x, uint32_t number) { return x.callNid(ehsys::nid::GetText, x.callLocal(fn::SystemSprite, kSystemSpriteTexts), number); }

/* ---- FUN_091d173c(arguments): the story hands the field its arguments (the clock's numbers, the callbacks); the field asks for the system sprites ---- */
void swap_AcceptArguments(SwapCtx &x) {
    flow(x).argumentsPending = 1;
    memcpy(x.mem(guest(x, kFieldArguments)), x.mem(x.arg(0)), sizeof(FieldArguments));
    memcpy(x.mem(guest(x, 0x09996FA8)), x.mem(x.arg(0) + 0x30), 0x10);
    x.callLocal(fn::RequestResource, 0, guest(x, 0x091D211C), 0);
}
SwapReg reg_AcceptArguments({"libfield.AcceptArguments", MODULE, offsetOf(0x091D173C), swap_AcceptArguments});

/* ---- FUN_091d211c(archive): the system sprites are loaded: gives the DP counter and the message box theirs and opens the town menu on the full map ---- */
void swap_OnSystemSprites(SwapCtx &x) {
    FieldFlow &state = flow(x);
    state.systemArchive = x.arg(0);
    if (state.systemArchive == 0) return;
    const uint32_t messageSprite = x.callLocal(fn::SystemSprite, 0);
    const uint32_t dpSprite = x.callLocal(fn::SystemSprite, 2);
    x.callLocal(fn::SetDpTexture, dpSprite);
    x.callLocal(fn::SetMessageSprite, messageSprite);
    x.callLocal(fn::PreparePanel);
    const bool town = state.arguments.flag == 1;
    if (town) x.callLocal(fn::OpenTownMenu, x.callLocal(fn::HeapHandle), *view<uint32_t>(x, state.systemArchive), 1);
    state.townMenuOpen = town ? 1 : 0;
    state.argumentsPending = 0;
}
SwapReg reg_OnSystemSprites({"libfield.OnSystemSprites", MODULE, offsetOf(0x091D211C), swap_OnSystemSprites});

/* ---- FUN_091d0e14: one frame of the flow ---- */
void swap_UpdateFlow(SwapCtx &x) {
    FieldFlow &state = flow(x);
    const uint32_t playerAddress = playerObject(x);
    state.character = (int32_t)x.callLocal(fn::WalkerAt, playerAddress);
    if (state.active == 0) {
        if (state.requestPending != 0) {
            if (state.step == kStepRequestB) {
                const int32_t picked = (int32_t)x.callLocal(fn::RequestResult);
                if (picked != -1) {
                    x.callAddr(state.arguments.callbacks[1], (uint32_t)picked, 1);
                    state.requestPending = 0;
                    state.lastTarget = picked;
                }
            } else if (state.step == kStepRequestA) {
                x.callLocal(fn::StartRequest, state.argB, state.argC);
                state.step = kStepRequestB;
            }
        }
    } else {
        switch (state.step) {
            case kStepStart:
                x.callLocal(fn::EnterFlow);
                x.callLocal(fn::FlowSetup);
                state.step = kStepWalking;
                break;
            case kStepWalking:
                x.callLocal(fn::WalkingStep, flowAddress(x, offsetof(FieldFlow, step)), flowAddress(x, offsetof(FieldFlow, answer)));
                break;
            case kStepAskTalk:
                x.callLocal(fn::OpenAsk, systemText(x, kTextAskTalk), 3, 2);
                state.step = kStepAskTalkWait;
                break;
            case kStepAskTalkWait: {
                const int32_t reply = (int32_t)x.callLocal(fn::AskReply);
                if (reply == 2) { state.step = kStepAnswer; state.answer = kAnswerTalk; }
                else if (reply == 3 || reply == -2) state.step = kStepStart;
                break;
            }
            case kStepWait4:
                if (x.callLocal(fn::PlayerWaitDone) != 0) state.step = kStepStart;
                break;
            case kStepTalk: {
                x.callLocal(fn::PrepareTalk);
                const int32_t picked = (int32_t)x.callLocal(fn::TalkTarget, (uint32_t)state.character);
                if (picked != 0 && picked != -1) x.callLocal(fn::SetTalkTarget, (uint32_t)picked);
                state.step = kStepTalkWait;
                break;
            }
            case kStepTalkWait:
                x.callLocal(fn::TalkStep, flowAddress(x, offsetof(FieldFlow, step)), flowAddress(x, offsetof(FieldFlow, answer)), flowAddress(x, offsetof(FieldFlow, argFC)));
                if (state.step == kStepStart) x.callLocal(fn::SetTalkTarget, 0xFFFFFFFFu);
                break;
            case kStepShop:
                x.callLocal(fn::OpenShop);
                state.step = kStepShopWait;
                break;
            case kStepShopWait: {
                const int32_t reply = (int32_t)x.callLocal(fn::ShopReply, flowAddress(x, offsetof(FieldFlow, argFC)));
                if (reply == 3) { state.step = kStepAnswer; state.answer = 4; }
                else if (reply == 2) { state.answer = 3; state.step = kStepAnswer; }
                else if (reply == 1) state.step = kStepStart;
                break;
            }
            case kStepMessage: {
                const uint32_t panel = x.callLocal(fn::MessagePanel);
                x.callLocal(fn::SetMessageMode, 1);
                *view<int16_t>(x, panel + 0x44) = 4;
                *view<int16_t>(x, panel + 0x48) = 2;
                if (state.answer == 5) x.callLocal(fn::StartMessage, systemText(x, 0xE), 0);
                else if (state.answer == 4) x.callLocal(fn::StartMessage, systemText(x, 0xD), 0);
                else if (state.answer == 3) x.callLocal(fn::StartMessage, systemText(x, 0xD), 0);
                else if (state.answer == 2) x.callLocal(fn::StartMessage, systemText(x, 0xC), 0);
                state.step = kStepMessageWait;
                break;
            }
            case kStepMessageWait:
                if (x.callLocal(fn::MessageDone) != 0) {
                    x.callLocal(fn::CloseMessage);
                    state.step = kStepStart;
                }
                break;
            case kStepMenu:
                x.callLocal(fn::OpenTownMenu, x.callLocal(fn::HeapHandle), *view<uint32_t>(x, state.systemArchive), 0);
                x.callLocal(fn::TownMenuReset);
                state.step = kStepMenuWait;
                break;
            case kStepMenuWait: {
                state.townMenuOpen = 0;
                if (x.callLocal(fn::TownMenuReady) != 0) {
                    const uint32_t choice = x.callLocal(fn::TownMenuChoice);
                    switch (choice) {
                        case 1: x.callLocal(fn::TownMenuClose); state.step = kStepStart; break;
                        case 3: state.step = kStepAnswer; state.answer = kAnswerMenu8; state.townMenuOpen = 1; break;
                        case 4: state.step = kStepAnswer; state.answer = kAnswerMenu9; state.townMenuOpen = 1; break;
                        case 5: state.step = kStepAnswer; state.answer = kAnswerList; break;
                    }
                }
                break;
            }
            case kStepCallA:
                x.callLocal(fn::PrepareCallA, state.callArgA);
                x.callAddr(state.arguments.callbacks[0], 0, state.callArgA, 1, state.callArgB, state.callArgC);
                state.step = kStepCallAWait;
                break;
            case kStepCallAWait:
                x.callLocal(fn::CallAStep, flowAddress(x, offsetof(FieldFlow, step)));
                break;
            case kStepCallB:
                x.callLocal(fn::PrepareCallB, state.callArgD, 1, 1);
                x.callAddr(state.arguments.callbacks[0], 1, state.callArgD, 1, state.callArgE, state.callArgF);
                state.step = kStepCallBWait;
                break;
            case kStepCallBWait:
                x.callLocal(fn::CallBStep, flowAddress(x, offsetof(FieldFlow, step)));
                break;
            case kStepPanel:
                x.callLocal(fn::OpenPanel);
                state.step = kStepPanelWait;
                break;
            case kStepPanelWait: {
                const int32_t reply = (int32_t)x.callLocal(fn::PanelReply);
                if (reply == 0) state.step = kStepStart;
                else if (reply != -1) {
                    state.step = kStepAnswer;
                    state.answer = reply == 3 ? 0xC : reply == 2 ? 0xB : reply == 1 ? 10 : state.answer;
                    x.callLocal(fn::PanelResult, flowAddress(x, offsetof(FieldFlow, pick1)), flowAddress(x, offsetof(FieldFlow, pick2)));
                }
                break;
            }
            case kStepList:
                x.callLocal(fn::OpenList);
                state.step = kStepListWait;
                break;
            case kStepListWait: {
                const int32_t reply = (int32_t)x.callLocal(fn::ListReply);
                if (reply == 0) state.step = kStepStart;
                else if (reply != -1) {
                    state.step = kStepAnswer;
                    state.answer = reply == 5 ? 0x13 : reply == 4 ? 0x12 : reply == 3 ? 0x11 : reply == 2 ? 0x10 : 0xF;
                }
                break;
            }
            case kStepAnswer:
                if (isMessageAnswer(state.answer)) {
                    x.callLocal(fn::EndTalk, (uint32_t)state.lastCharacter, playerAddress);
                    state.active = 0;
                } else {
                    state.active = 0;
                }
                break;
        }
        if (state.step == kStepAnswer && state.active == 1) {
            state.lastCharacter = state.character;
            if (state.character == kNoCharacter && isMessageAnswer(state.answer)) state.step = kStepMessage;
        }
    }
    x.callLocal(fn::UpdateFlowB);
    if (x.callLocal(fn::PlayerArrived) != 0) state.playerArrivedPending = 0;
    if (x.callLocal(fn::FollowerArrived) != 0) state.followerPending = 0;
}
SwapReg reg_UpdateFlow({"libfield.UpdateFlow", MODULE, offsetOf(0x091D0E14), swap_UpdateFlow});

/* ---- FUN_091d1e1c: the rest of the frame of the map: the player and the follower move, the character next to the player is shown in the HUD ---- */
constexpr int32_t kPlainCharacters[] = {0x97, 0x96, 0x95, 0x94, 0x93, 0x92, 0x91, 0x90, 0x8F, 0x27, 0x22, 0x1D, 0x17, 0x14};   /* characters that show no card numbers */
constexpr int32_t kScrollFast = 0xF, kScrollSlow = 7;
void swap_UpdateFlowB(SwapCtx &x) {
    FieldFlow &state = flow(x);
    if (state.step != kStepWalking) x.callLocal(fn::SetPlayerControl, 0);
    x.callLocal(fn::UpdatePlayer);
    x.callLocal(fn::UpdateFollower);
    const bool noCharacter = state.active == 0 || state.character == kNoCharacter || x.callLocal(fn::CharacterShown) == 2 ||
                             state.step == kStepShopWait || state.step == kStepCallAWait || state.step == kStepCallBWait || state.step == kStepPanelWait;
    if (noCharacter) {
        x.callLocal(fn::HideHudPart);
    } else {
        GuestStack frame(x, 32);
        uint32_t *info = (uint32_t *)x.mem(frame.addr());   /* [0] category result, [1] info C, [2] info 8 */
        info[0] = 0; info[1] = 0; info[2] = 0;
        const uint32_t resultC = frame.addr() + 4, resultB = frame.addr() + 8, category = frame.addr();
        x.callLocal(fn::CharacterInfo, (uint32_t)state.character, resultB, resultC);
        const int32_t categoryReturn = (int32_t)x.callNid(ehsys::nid::CharacterCategory, (uint32_t)state.character, category);
        bool plain = false;
        for (int32_t id : kPlainCharacters) if (state.character == id) plain = true;
        if (plain || (categoryReturn > -1 && info[0] == 0)) {
            info[1] = 0;
            x.callLocal(fn::ShowCharacterInfo, info[2], 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
        } else {
            const uint32_t statA = x.callNid(engine::nid::CharacterStat, 0, (uint32_t)state.character) & 0xFFFF;
            const uint32_t statB = x.callNid(engine::nid::CharacterStat, 1, (uint32_t)state.character) & 0xFFFF;
            const uint32_t statC = x.callNid(engine::nid::CharacterStat, 2, (uint32_t)state.character) & 0xFFFF;
            const uint32_t owned = x.callNid(ehsys::nid::CardState, (uint32_t)state.character);
            x.callLocal(fn::ShowCharacterInfo, info[2], info[1], statA, statB, statC, owned);
        }
    }
    if (state.panelPending != 0 && x.callLocal(fn::PanelOpen) == 0) state.panelPending = 0;
    if (state.listPending != 0 && x.callLocal(fn::ListOpen) != 0) state.listPending = 0;
    x.callLocal(fn::UpdateHud);
    x.callLocal(fn::UpdatePanel);
    if (x.callLocal(fn::DialogDone) != 0) state.dialogPending = 0;
    fieldState(x).mapIdle = state.step == kStepWalking ? 1 : 0;
    state.scrollSpeed = fieldState(x).flag14 == 0 ? kScrollFast : kScrollSlow;
}
SwapReg reg_UpdateFlowB({"libfield.UpdateFlowB", MODULE, offsetOf(0x091D1E1C), swap_UpdateFlowB});

/* ---- FUN_091d2680: draws the windows of the menu the flow is in ---- */
void swap_DrawMenuLists(SwapCtx &x) {
    GuestStack cursor(x, 16);
    uint32_t &list = *(uint32_t *)x.mem(cursor.addr());
    list = gu::BeginList(x, 3, 0);
    x.callNid(gu::nid::DrawWindow, cursor.addr(), guest(x, 0x09993390));
    const int32_t menu = datum<int32_t>(x, 0x0999338C);
    if (menu == 5) list = x.callLocal(fn::DrawAsk, list);
    else if (menu == 4 || menu == 3) x.callNid(gu::nid::DrawWindow, cursor.addr(), guest(x, 0x099934E0));
    else if (menu == 2) list = x.callLocal(fn::DrawShopStep, list);
    gu::EndList(x, list);
}
SwapReg reg_DrawMenuLists({"libfield.DrawMenuLists", MODULE, offsetOf(0x091D2680), swap_DrawMenuLists});

/* ---- FUN_091d1544: draws what belongs to the step the flow is in ---- */
constexpr uint32_t kDimColour = 0x80000000, kDimMask = 0xFFFF;
void drawDimmer(SwapCtx &x) {
    uint32_t list = gu::BeginList(x, 3, 0);
    list = x.callLocal(fn::DrawFadeQuad, list, kDimMask, kDimColour);
    gu::EndList(x, list);
    x.callLocal(fn::DrawTownMenu);
}
void swap_DrawFlow(SwapCtx &x) {
    FieldFlow &state = flow(x);
    x.callLocal(fn::DrawPlayer);
    x.callLocal(fn::DrawFollower);
    x.callLocal(fn::DrawHud);
    x.callLocal(fn::DrawPanel);
    x.callLocal(fn::DrawMenuLists);
    const int32_t step = state.step;
    if (step == kStepListWait) {
        gu::EndList(x, x.callLocal(fn::DrawListStep, gu::BeginList(x, 3, 0)));
    } else if (step == kStepRequestB) {
        x.callLocal(fn::DrawRequest);
    } else {
        bool drawMenu = step == kStepPanelWait;
        if (!drawMenu) {
            if (step != kStepMenuWait) {
                if (step == kStepShopWait) gu::EndList(x, x.callLocal(fn::DrawShopStep, gu::BeginList(x, 3, 0)));
                else if (step == kStepAskTalkWait) gu::EndList(x, x.callLocal(fn::DrawAsk, gu::BeginList(x, 3, 0)));
                else if (step == kStepWalking && (*view<uint32_t>(x, gu::GetInput(x)) & 0x200) != 0) {
                    uint32_t list = gu::BeginList(x, 4, 0);
                    list = x.callLocal(fn::DrawScroll, list, (uint32_t)state.scrollSpeed);
                    gu::EndList(x, list);
                }
            } else if (state.townMenuOpen == 0) {
                drawDimmer(x);
            } else {
                drawMenu = true;
            }
        }
        if (drawMenu) gu::EndList(x, x.callLocal(fn::DrawPanelWindow, gu::BeginList(x, 3, 0)));
    }
    if (state.townMenuOpen != 0) drawDimmer(x);
}
SwapReg reg_DrawFlow({"libfield.DrawFlow", MODULE, offsetOf(0x091D1544), swap_DrawFlow});

}  // namespace
