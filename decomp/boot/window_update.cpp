/* ehsys window update (BOOT): one frame of a window set's state and input: opening, moving the selection, choosing, cancelling, closing. */
#include "boot.h"
#include "window.h"

#define MODULE "BOOT"

namespace {
using namespace boot;

/* what an update answers */
constexpr int32_t kNoAnswer = -1, kCancelled = -2, kBadState = -3;   /* else the chosen cell (or button) */

/* the sound effects of the menus */
constexpr uint32_t kSoundMove = 0x6212, kSoundConfirm = 0x6213, kSoundCancel = 0x6214, kSoundRefuse = 0x6215;

/* the pad buttons a window reads (bits of the pad's `pressedOrRepeating`) */
constexpr uint32_t kPadUp = 0x10, kPadRight = 0x20, kPadDown = 0x40, kPadLeft = 0x80;

constexpr uint32_t kAnswerFrames = 9;   /* how long a chosen cell stays lit (the counter runs while the state is "answered") */

constexpr uint32_t kConfirmCancelMasks = 0x088ECBE4;   /* two pad masks (u16 each) */
constexpr uint32_t kButtonsAvailable = 0x088ED414;     /* per ButtonLayout: which of the eight button slots exist (a bit each) */

/* FUN_08843984 is in window_content.cpp: the slot of the item whose style is `style` (callers outside BOOT, such as the deck editor, pass a style other than the block's own) */
int32_t itemSlot(SwapCtx &x, const WindowBlock &block, uint32_t style) {
    for (int32_t i = 0; i < 16; i++) {
        const uint32_t item = block.item[i];
        if (item != 0 && x.at<WindowItem>(item).style == style) return i;
    }
    return -1;
}
uint32_t ownStyle(uint32_t blockAddress) { return blockAddress + offsetof(WindowBlock, style); }

/* F60A4259(block, style): the window's item has finished appearing or disappearing (it is shown, or closed) */
bool itemSettled(SwapCtx &x, uint32_t blockAddress, uint32_t style) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    const int32_t slot = itemSlot(x, block, style);
    if (slot < 0) return false;
    const uint32_t phase = x.at<WindowItem>(block.item[slot]).phase;
    return phase == 4 || phase == 2;
}

/* FA53F08C(block, style): start the window's item closing */
bool startItemClosing(SwapCtx &x, uint32_t blockAddress, uint32_t style) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    const int32_t slot = itemSlot(x, block, style);
    if (slot < 0) return false;
    WindowItem &item = x.at<WindowItem>(block.item[slot]);
    item.phase = 3;
    item.animationStep = 0;
    return true;
}

/* B0F320E6(block): begin closing the window: its item animates out when the style has a close animation, else it is closed at once */
void beginClose(SwapCtx &x, uint32_t blockAddress) {
    WindowBlock &block = x.at<WindowBlock>(blockAddress);
    startItemClosing(x, blockAddress, ownStyle(blockAddress));
    block.state = block.style.closeAnimation < 1 ? kWindowClosed : kWindowClosing;
}

/* 8171F765(which): the pad mask for confirm (0) or cancel (1); the two come from a pair of masks, swapped by the region setting */
uint32_t confirmCancelMask(SwapCtx &x, int which) {
    const uint16_t masks[2] = {x.at<uint16_t>(kConfirmCancelMasks), x.at<uint16_t>(kConfirmCancelMasks + 2)};
    uint32_t pick = x.callLocal(fn::ConfirmSetting) != 0 ? 1 : 0;   /* 1F190478: the setting (it returns 6) */
    if (which == 1) pick ^= 1;
    return masks[pick];
}

/* 0884a2c8(block): the selection one cell back; from the first cell it wraps to the last (or stays, with kNoWrap) */
void selectPreviousCell(WindowBlock &block) {
    const uint32_t visible = block.visibleCells, count = block.cellCount;
    int16_t first = (int16_t)block.firstCell;
    uint8_t selection = block.selection & 0x1F;
    const uint32_t shown = count < visible ? count : visible;
    if (selection == 0) {
        if (first == 0) {
            if (!(block.flags & kNoWrap)) {
                if (visible == 0) selection = (uint8_t)(count - 1);
                else { first = (int16_t)(count - shown); selection = (uint8_t)(shown - 1); }
            }
        } else first--;
    } else selection--;
    block.selection = (block.selection & 0xE0) | (selection & 0x1F);
    block.firstCell = (uint16_t)first;
}

/* 0884a3d0(block): the selection one cell on; from the last cell it wraps to the first (or stays, with kNoWrap) */
void selectNextCell(WindowBlock &block) {
    const int32_t visible = block.visibleCells, count = block.cellCount, selection = block.selection & 0x1F;
    uint16_t first = block.firstCell;
    const int32_t shown = count < visible ? count : visible;
    int32_t chosen;
    if (visible == 0) {
        const bool wraps = !(block.flags & kNoWrap);
        if (wraps || selection < count - 1) {
            if (count != 0) {
                block.selection = (block.selection & 0xE0) | (((selection + 1) % count) & 0x1F);
                block.firstCell = first;
                return;
            }
            chosen = 0;
        } else chosen = count - 1;
    } else if (selection < shown - 1) {
        chosen = selection + 1;
    } else {
        chosen = selection;
        if ((int32_t)first < count - shown) first++;
        else if (!(block.flags & kNoWrap)) { chosen = 0; first = 0; }
    }
    block.selection = (block.selection & 0xE0) | (chosen & 0x1F);
    block.firstCell = first;
}

/* 5B4DADF5 / 0190A327(block): move the selection back or on, skipping cells that cannot be chosen (at most one lap) when the layout asks for it */
template <class Move>
void moveSelection(WindowBlock &block, Move move) {
    if (!(block.layoutFlags & kSkipDisabled)) { move(block); return; }
    for (int32_t tried = 0; tried < block.cellCount; tried++) {
        move(block);
        if (!((block.disabledCells >> ((block.firstCell + (block.selection & 0x1F)) & 0x1F)) & 1)) break;
    }
}

/* what a window answers while it just reports a choice: the chosen cell, or the chosen button, else nothing */
int32_t chosenAnswer(const WindowBlock &block) {
    if (((block.layoutFlags & 0x7F) >> 3) != 0) return (int32_t)(block.firstCell + (block.selection & 0x1F));
    if ((block.buttonFlags & 0xF) != 0) return block.selectedButton & 0xF;
    return kNoAnswer;
}

/* The dialog button to move to when the pad goes left or right: the next of the layout's buttons in that direction (eight slots in a ring). */
uint32_t neighbourButton(uint32_t available, uint32_t current, int32_t direction) {
    for (int32_t step = 1; step < 8; step++) {
        const uint32_t candidate = (current + 8 + direction * step) & 7;
        if (available & (1u << candidate)) return candidate;
    }
    return current;
}

/* 39ADDA9F(block): one frame of a window set's input and state. Returns kNoAnswer while nothing was chosen, kCancelled for a cancel, kBadState when the
 * window is in no state that takes input, else the chosen cell index (or button). The states: opening (2) waits for the items to appear, open (3)
 * reads the pad, answered (4) lights the choice for nine frames, closing (5) waits for the items to disappear, closed (6) keeps answering. */
int32_t updateWindow(SwapCtx &x, uint32_t blockAddress) {
    WindowBlock &block = x.at<WindowBlock>(blockAddress);
    uint32_t inputAddress = block.inputSource;
    if (inputAddress == 0) inputAddress = ehsys::GetInput(x);
    const uint32_t pressed = x.at<uint32_t>(inputAddress + 4), repeating = x.at<uint32_t>(inputAddress + 12);
    const bool hasCells = ((block.layoutFlags & 0x7F) >> 3) != 0, sideways = (block.layoutFlags & 0x08) != 0;

    switch (block.state) {
    case kWindowAnswered: {
        const uint32_t frame = block.counter++;
        if (frame > kAnswerFrames - 1) {
            block.counter = 0;
            if (block.flags & kStayOpen) {
                block.state = kWindowOpen;
                if (block.stateFlags & 1) return kCancelled;
                return chosenAnswer(block);
            }
            beginClose(x, blockAddress);
        }
        return kNoAnswer;
    }
    case kWindowOpening:
        if (itemSettled(x, blockAddress, ownStyle(blockAddress))) block.state = kWindowOpen;
        return kNoAnswer;
    case kWindowClosing:
        if (!itemSettled(x, blockAddress, ownStyle(blockAddress))) return kNoAnswer;
        block.state = kWindowClosed;
        [[fallthrough]];   /* and answers */
    case kWindowClosed:
        if (block.stateFlags & 1) return kCancelled;
        if (chosenAnswer(block) != kNoAnswer) return chosenAnswer(block);
        break;
    case kWindowOpen: break;
    default: return kBadState;
    }

    /* open: the answer if one was already given */
    if (block.stateFlags & 1) return kCancelled;
    if (block.stateFlags & 2) {
        if (chosenAnswer(block) != kNoAnswer) return chosenAnswer(block);
    } else if (block.flags & kNoInput) {
        return kNoAnswer;
    }

    const uint32_t confirmMask = confirmCancelMask(x, 0), cancelMask = confirmCancelMask(x, 1);
    if (pressed & cancelMask & 0xFFFF) {   /* cancel */
        if (block.flags & kNoCancel) { ehsys::PlaySound(x, kSoundRefuse); return kNoAnswer; }
        ehsys::PlaySound(x, kSoundCancel);
        block.stateFlags = (block.stateFlags & 0xFE) | 1;
        if (!(block.flags & kCancelSelectsDefault)) {
            if (!(block.flags & kStayOpen)) { beginClose(x, blockAddress); return kNoAnswer; }
            return kCancelled;
        }
        if (hasCells) {   /* the selection goes to the default cell, and the window answers (lit) */
            if (block.visibleCells == 0) {
                block.selection = (block.selection & 0xE0) | (block.defaultSelection & 0x1F);
            } else if (block.defaultSelection < block.firstCell + block.visibleCells) {
                block.selection = (block.selection & 0xE0) | (block.defaultSelection & 0x1F);
            } else {
                block.selection = (block.selection & 0xE0) | ((block.visibleCells - 1) & 0x1F);
                block.firstCell = (uint16_t)(block.defaultSelection - (block.selection & 0x1F));
            }
            block.state = kWindowAnswered;
        }
        if ((block.buttonFlags & 0xF) != 0) {
            block.selectedButton = (block.selectedButton & 0xF0) | (block.defaultSelection & 0xF);
            block.state = kWindowAnswered;
        }
        return kNoAnswer;
    }
    if (pressed & confirmMask & 0xFFFF) {   /* confirm */
        if (block.flags & kNoConfirm) return kNoAnswer;
        if (hasCells && ((block.disabledCells >> ((block.firstCell + (block.selection & 0x1F)) & 0x1F)) & 1)) {
            ehsys::PlaySound(x, kSoundRefuse);
            return kNoAnswer;
        }
        ehsys::PlaySound(x, kSoundConfirm);
        block.stateFlags = (block.stateFlags & 0xFD) | 2;
        block.state = kWindowAnswered;
        return kNoAnswer;
    }
    if (hasCells) {   /* move the selection along the cells */
        const uint32_t back = sideways ? kPadLeft : kPadUp, forward = sideways ? kPadRight : kPadDown;
        if (repeating & back) { moveSelection(block, selectPreviousCell); ehsys::PlaySound(x, kSoundMove); }
        if (repeating & forward) { moveSelection(block, selectNextCell); ehsys::PlaySound(x, kSoundMove); }
        return kNoAnswer;
    }
    if ((block.buttonFlags & 0xF) == 0) return kNoAnswer;
    const uint32_t available = x.at<uint8_t>(kButtonsAvailable + (block.buttonFlags & 0xF));   /* move among the dialog buttons */
    uint32_t current = block.selectedButton & 0xF;
    if (repeating & kPadLeft) {
        const uint32_t target = neighbourButton(available, current, -1);
        if (target != current) { ehsys::PlaySound(x, kSoundMove); block.selectedButton = (block.selectedButton & 0xF0) | target; current = target; }
    }
    if (repeating & kPadRight) {
        const uint32_t target = neighbourButton(available, current, 1);
        if ((block.selectedButton & 0xF) != target) { ehsys::PlaySound(x, kSoundMove); block.selectedButton = (block.selectedButton & 0xF0) | target; }
    }
    return kNoAnswer;
}

/* ---- glue ---- */

void swap_UpdateWindow(SwapCtx &x) { x.ret((uint32_t)updateWindow(x, x.arg(0))); }
SwapReg reg_UpdateWindow({"BOOT.UpdateWindow", MODULE, fn::UpdateWindow, swap_UpdateWindow});

void swap_BeginClose(SwapCtx &x) { beginClose(x, x.arg(0)); }
SwapReg reg_BeginClose({"BOOT.BeginClose", MODULE, fn::BeginClose, swap_BeginClose});

void swap_ItemSettled(SwapCtx &x) { x.ret(itemSettled(x, x.arg(0), x.arg(1))); }
SwapReg reg_ItemSettled({"BOOT.ItemSettled", MODULE, fn::ItemSettled, swap_ItemSettled, true});

void swap_StartItemClosing(SwapCtx &x) { x.ret(startItemClosing(x, x.arg(0), x.arg(1))); }
SwapReg reg_StartItemClosing({"BOOT.StartItemClosing", MODULE, fn::StartItemClosing, swap_StartItemClosing});

void swap_ConfirmCancelMask(SwapCtx &x) { x.ret(confirmCancelMask(x, (int)x.arg(0))); }
SwapReg reg_ConfirmCancelMask({"BOOT.ConfirmCancelMask", MODULE, fn::ConfirmCancelMask, swap_ConfirmCancelMask, true});

void swap_SelectPreviousCell(SwapCtx &x) { selectPreviousCell(x.at<WindowBlock>(x.arg(0))); }
SwapReg reg_SelectPreviousCell({"BOOT.SelectPreviousCell", MODULE, fn::SelectPreviousCell, swap_SelectPreviousCell});

void swap_SelectNextCell(SwapCtx &x) { selectNextCell(x.at<WindowBlock>(x.arg(0))); }
SwapReg reg_SelectNextCell({"BOOT.SelectNextCell", MODULE, fn::SelectNextCell, swap_SelectNextCell});

void swap_MoveSelectionBack(SwapCtx &x) { moveSelection(x.at<WindowBlock>(x.arg(0)), selectPreviousCell); }
SwapReg reg_MoveSelectionBack({"BOOT.MoveSelectionBack", MODULE, fn::MoveSelectionBack, swap_MoveSelectionBack});

void swap_MoveSelectionOn(SwapCtx &x) { moveSelection(x.at<WindowBlock>(x.arg(0)), selectNextCell); }
SwapReg reg_MoveSelectionOn({"BOOT.MoveSelectionOn", MODULE, fn::MoveSelectionOn, swap_MoveSelectionOn});

}  // namespace
