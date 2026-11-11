/* Test-only field queries and opt-in removal actions in isolated TF_CTL sessions. */
#include "tfmod.h"
#include <cstdlib>
#include <cstring>
#include <vector>

static const TfModApi *A;
static uint32_t mailbox;
static const uint32_t SIDES = 0x09f79cec, SIDE_BYTES = 0xfac;
static uint32_t pending_mode, pending_source, pending_side, pending_slot;

static bool capture_self_destruction;
static uint32_t captured_self_mask;
static int self_destruction(TfCpu *c) {
    if (!capture_self_destruction) return 0;
    captured_self_mask |= 1u << ((A->reg(c,4)&1)*16 + A->reg(c,5));
    A->set_reg(c,2,1);
    return 1;
}

static int before_selected_destruction(TfCpu *c) {
    if (!pending_mode || (*(uint16_t *)A->mem(A->reg(c, 4)) & 0x1fff) != pending_source ||
        *(uint32_t *)A->mem(0x09f78784) != 6) return 0;
    const uint32_t mode = pending_mode;
    pending_mode = 0;
    uint32_t regs[32];
    for (int i = 1; i < 32; ++i) regs[i] = A->reg(c, i);
    const uint32_t base = SIDES + pending_side * SIDE_BYTES, entry = base + 0x30 + pending_slot * 20;
    const uint32_t original = *(uint32_t *)A->mem(entry);
    *(uint32_t *)A->mem(mailbox + 48) = original & 0x1fff;
    *(uint32_t *)A->mem(mailbox + 56) = original;
    *(uint32_t *)A->mem(mailbox + 64) = *(uint16_t *)A->mem(regs[4] + 8);
    *(uint32_t *)A->mem(mailbox + 68) = *(uint32_t *)A->mem(0x09f78784);
    if (original & 0x1fff) {
        if (mode == 1) {
            *(uint32_t *)A->mem(entry + 8) = 0;
            *(uint32_t *)A->mem(entry + 16) = 0x100000;
        } else {
            // A second native removal here would overwrite the active driver's
            // shared phase state. Stage the controlled board fixture directly.
            const uint32_t grave = *(uint32_t *)A->mem(base + 20);
            if (grave >= 160) {
                for (int i = 1; i < 32; ++i) if (i != 26 && i != 27) A->set_reg(c, i, regs[i]);
                return 0;
            }
            *(uint32_t *)A->mem(base + 0x710 + grave * 4) = original;
            *(uint32_t *)A->mem(base + 20) = grave + 1;
            std::memset(A->mem(entry), 0, 20);
            *(uint32_t *)A->mem(mailbox + 72) = *(uint16_t *)A->mem(regs[4] + 8);
            if (mode == 3 && *(uint32_t *)A->mem(base + 12)) {
                const uint32_t n = *(uint32_t *)A->mem(base + 12);
                uint32_t card = *(uint32_t *)A->mem(base + 0x120);
                card = (card & ~0x1fffu) | (original & 0x1fff);
                *(uint32_t *)A->mem(mailbox + 60) = card;
                const uint32_t fixture[5] = {card, 1, 1, 0, 0x80};
                std::memcpy(A->mem(entry), fixture, sizeof fixture);
                std::memmove(A->mem(base + 0x120), A->mem(base + 0x124), (n - 1) * 4);
                *(uint32_t *)A->mem(base + 0x120 + (n - 1) * 4) = 0;
                *(uint32_t *)A->mem(base + 12) = n - 1;
                const uint32_t token = (((card >> 16) & 0x3fff) >> 6) * 2 + ((card >> 13) & 1);
                uint32_t *instance = (uint32_t *)A->mem(0x09f7bc58 + token * 4);
                *instance = (*instance & ~0x1fffu) | (card & 0x1fff);
            }
            *(uint32_t *)A->mem(mailbox + 76) = *(uint16_t *)A->mem(regs[4] + 8);
            A->set_reg(c, 4, regs[4]); A->set_reg(c, 5, 0); A->set_reg(c, 6, mailbox + 84);
            A->call(c, 0x09f0e974);
            *(uint32_t *)A->mem(mailbox + 80) = A->reg(c, 2);
        }
        *(uint32_t *)A->mem(mailbox + 52) = mode;
    }
    for (int i = 1; i < 32; ++i) if (i != 26 && i != 27) A->set_reg(c, i, regs[i]);
    return 0;
}

static int frame(TfCpu *c) {
    uint32_t *m = (uint32_t *)A->mem(mailbox);
    if (m[1] == m[2]) return 0;
    const uint32_t sequence = m[1];
    m[3] = 1;
    if (!A->module_loaded("libduel_eng")) { m[2] = sequence; return 0; }
    if (m[4] > 1 || m[5] >= (m[7] == 13 || m[7] == 15 ? 12u : m[7] == 6 || m[7] == 14 || m[7] == 16 || m[7] == 19 ? 11u : 5u) || m[6] > 22 || m[7] > 24 ||
        (m[7] == 23 && (m[24] < 4000 || m[24] > 8191)) ||
        (m[7] == 22 && (m[24] > 8191 || m[25] > 1)) ||
        (m[7] == 21 && (m[24] > 1 || m[25] > 2 || m[26] > 1)) ||
        (m[7] == 20 && (m[24] < 4000 || m[24] > 8191 || m[25] > 1)) ||
        (m[7] == 19 && (m[6] || m[5] < 5 || !std::getenv("TF3_TEST_ACTIONS") ||
            (*(uint16_t *)A->mem(SIDES + m[4]*SIDE_BYTES + 0x30 + 20*m[5]) & 0x1fff) != 5001)) ||
        (m[7] == 18 && (m[24] < 4000 || m[24] > 8191)) ||
        (m[7] == 17 && (m[24] < 4000 || m[24] > 8191 || m[25] > 1)) ||
        (m[7] == 16 && (m[6] || m[24] < 1 || m[24] > 3 || m[25] < 4000 || m[25] > 8191 || m[5] < 5)) ||
        (m[7] == 15 && (m[6] || m[5] >= *(uint32_t *)A->mem(SIDES + m[4] * SIDE_BYTES + 12))) ||
        (m[7] == 14 && (m[24] < 4000 || m[24] > 8191 || m[25] > 1)) ||
        (m[7] == 13 && (m[6] || m[24] < 4000 || m[24] > 8191)) ||
        ((m[7] == 6 || m[7] == 8 || m[7] == 9 || m[7] == 10 || m[7] == 11 || m[7] == 16) && !std::getenv("TF3_TEST_ACTIONS")) ||
        (m[7] == 10 && (m[6] || m[24] < 1 || m[24] > 5)) ||
        (m[7] == 8 && (m[6] || m[24] < 4000 || m[24] > 8191 || m[25] > 1)) ||
        (m[7] == 9 && (m[6] || m[24] < 4000 || m[24] > 8191 || m[25] > 1 || (m[26] & ~0x07ff07ffu))) ||
        (m[7] == 6 && (m[6] || m[25] < 11 || m[25] > 15 || (m[26] & ~0x11003u) || (m[24] && (m[24] < 4000 || m[24] > 8191)))) ||
        (m[7] == 1 && (m[24] > 1 || m[25] >= 5))) { m[3] = 2; m[2] = sequence; return 0; }
    for (uint32_t i = 0; i < m[6]; i++) {
        const uint32_t *f = (const uint32_t *)A->mem(mailbox + 128 + i * 28);
        if (f[0] > 1 || f[1] > 10) { m[3] = 2; m[2] = sequence; return 0; }
    }
    std::vector<uint8_t> saved(A->mem(SIDES), A->mem(SIDES) + SIDE_BYTES * 2);
    std::vector<uint8_t> links(A->mem(0x09f7c460), A->mem(0x09f7c460) + 24 * 8);
    if (!(m[7] == 17 && !m[26]) && m[7] != 4 && m[7] != 6 && m[7] != 7 && m[7] != 8 && m[7] != 9 && m[7] != 10 && m[7] != 13 && m[7] != 15 && m[7] != 16 && m[7] != 19) for (uint32_t side = 0; side < 2; side++) std::memset(A->mem(SIDES + side * SIDE_BYTES + 0x30), 0, 20 * 11);
    for (uint32_t i = 0; i < m[6]; i++) {
        const uint32_t *f = (const uint32_t *)A->mem(mailbox + 128 + i * 28);
        std::memcpy(A->mem(SIDES + f[0] * SIDE_BYTES + 0x30 + f[1] * 20), f + 2, 20);
    }
    // Fixture link: 1..5 names an own monster, 6..10 an opposing monster.
    // Materialize native type-10 equipment nodes; +4 remains an unrelated turn number.
    for (uint32_t i = 0; i < m[6]; ++i) {
        const uint32_t *f = (const uint32_t *)A->mem(mailbox + 128 + i * 28);
        if (f[1] < 5 || !f[3] || f[3] > 10) continue;
        const uint32_t target_side = f[0] ^ (f[3] > 5), target_slot = (f[3] - 1) % 5;
        const uint32_t target = SIDES + target_side * SIDE_BYTES + 0x30 + target_slot * 20;
        uint16_t *node = (uint16_t *)A->mem(0x09f7c460 + i * 8);
        node[0] = (uint16_t)(f[0] | (f[1] << 8)); node[1] = 10; node[2] = 0;
        node[3] = *(uint16_t *)A->mem(target + 10);
        *(uint16_t *)A->mem(target + 10) = (uint16_t)(i + 1);
        *(uint32_t *)A->mem(SIDES + f[0] * SIDE_BYTES + 0x30 + f[1] * 20 + 4) = 17;
    }
    uint32_t regs[32];
    for (int i = 1; i < 32; i++) regs[i] = A->reg(c, i);
    std::memset(A->mem(mailbox + 32), 0, 64);
    if (m[7] == 24) {
        const uint32_t saved_mask = *(uint32_t *)A->mem(0x09f7bc48);
        *(uint32_t *)A->mem(0x09f7bc48) = 0;
        captured_self_mask = 0; capture_self_destruction = true;
        A->call(c,0x09f26b70);
        capture_self_destruction = false;
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c,2);
        *(uint32_t *)A->mem(mailbox + 36) = captured_self_mask;
        *(uint32_t *)A->mem(0x09f7bc48) = saved_mask;
    } else if (m[7] == 23) {
        uint8_t object[24] = {};
        *(uint16_t *)object = (uint16_t)m[24];
        object[2] = (uint8_t)(m[4] | (m[5] << 1));
        std::memcpy(A->mem(mailbox + 800), object, sizeof object);
        A->set_reg(c,4,m[4]); A->set_reg(c,5,m[24]); A->set_reg(c,6,0);
        A->call(c,0x09f24428);
        *(uint32_t *)A->mem(mailbox+32) = A->reg(c,2);
        A->set_reg(c,4,mailbox+800);
        A->call(c,0x09ee3038);
        *(uint32_t *)A->mem(mailbox+36) = A->reg(c,2);
    } else if (m[7] == 22) {
        A->set_reg(c,4,m[4]);
        A->call(c,0x09f4da6c);
        *(uint32_t *)A->mem(mailbox+32) = A->reg(c,2);
    } else if (m[7] == 21) {
        // Controlled kind-4 modifier: test only the native amount/layer math.
        // Natural battle scenarios separately prove its creation and cleanup.
        const uint32_t entry = SIDES + m[4]*SIDE_BYTES + 0x30 + 20*m[5];
        uint16_t *node = (uint16_t *)A->mem(0x09f7c458 + 23*8);
        node[0] = m[25] ? 6386 : 6125; node[1] = 4; node[2] = m[25] == 2 ? 1 : 0;
        node[3] = *(uint16_t *)A->mem(entry + 10);
        if (m[24]) *(uint16_t *)A->mem(entry + 10) = 23;
        uint16_t *reverse = (uint16_t *)A->mem(0x09f7c458 + 24*8);
        reverse[0] = 4695; reverse[1] = 0; reverse[2] = 0; reverse[3] = 0;
        *(uint16_t *)A->mem(SIDES + 0x30 + 20*11 + 10) = m[26] ? 24 : 0;
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]); A->set_reg(c, 6, mailbox+32); A->set_reg(c, 7, 0);
        A->call(c, 0x09f56da8);
        *(uint16_t *)A->mem(mailbox+80) = node[0];
        *(uint16_t *)A->mem(mailbox+82) = node[2];
    } else if (m[7] == 20) {
        const uint32_t entry = SIDES + m[25]*SIDE_BYTES + 0x30 + 20*m[5];
        const uint32_t before = *(uint32_t *)A->mem(entry);
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[24]);
        A->set_reg(c, 6, m[25]); A->set_reg(c, 7, m[5]);
        A->call(c, 0x09f4e884);
        *(uint32_t *)A->mem(mailbox+32) = A->reg(c, 2);
        *(uint32_t *)A->mem(mailbox+36) = before;
        *(uint32_t *)A->mem(mailbox+40) = *(uint32_t *)A->mem(entry);
    } else if (m[7] == 19) {
        // Bounded native-reference action: resolve only Windstorm's own record.
        const uint32_t word = *(uint32_t *)A->mem(SIDES + m[4]*SIDE_BYTES + 0x30 + 20*m[5]);
        const uint32_t token = (((word >> 16) & 0x3fff) >> 6)*2 + ((word >> 13) & 1);
        std::memset(A->mem(mailbox+800), 0, 24);
        *(uint16_t *)A->mem(mailbox+800) = 5001;
        A->mem(mailbox+802)[0] = (uint8_t)(m[4] | (m[5] << 1));
        *(uint16_t *)A->mem(mailbox+804) = (uint16_t)(token << 6);
        A->set_reg(c, 4, mailbox+800); A->call(c, 0x09eb4574);
        const uint32_t record = A->reg(c, 2);
        const uint32_t callback = record ? *(uint32_t *)A->mem(record+4) : 0;
        if (callback) { A->set_reg(c, 4, mailbox+800); A->call(c, callback); }
        *(uint32_t *)A->mem(mailbox+32) = callback;
    } else if (m[7] == 18) {
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[4] | (11 << 1));
        A->set_reg(c, 4, mailbox + 800);
        A->call(c, 0x09ed4754);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 17) {
        const uint32_t error = *(uint32_t *)A->mem(0x09f7d4ec);
        for (uint32_t bypass = 0; bypass < 2; ++bypass) {
            A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[24]); A->set_reg(c, 6, bypass);
            A->call(c, 0x09f43db8);
            *(uint32_t *)A->mem(mailbox + 32 + bypass * 4) = A->reg(c, 2);
        }
        const uint32_t base = SIDES + m[4] * SIDE_BYTES;
        *(uint32_t *)A->mem(base + 20) = 1;
        const uint32_t proper = m[25] ? 0 : 0x4000;
        *(uint32_t *)A->mem(base + 0x710) = m[24] | proper | (m[4] << 13);
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[4]); A->set_reg(c, 6, 0);
        A->call(c, 0x09f55288);
        *(uint32_t *)A->mem(mailbox + 40) = A->reg(c, 2);
        *(uint32_t *)A->mem(base + 24) = 1;
        *(uint32_t *)A->mem(base + 0xa80) = m[24] | proper | (m[4] << 13);
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[4]); A->set_reg(c, 6, 0);
        A->call(c, 0x09f5549c);
        *(uint32_t *)A->mem(mailbox + 44) = A->reg(c, 2);
        *(uint32_t *)A->mem(0x09f7d4ec) = error;
    } else if (m[7] == 0 || m[7] == 7) {
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]); A->set_reg(c, 6, mailbox + 32);
        A->call(c, 0x09f56da8);
    } else if (m[7] == 1) {
        A->set_reg(c, 4, m[24]); A->set_reg(c, 5, m[25]);
        A->set_reg(c, 6, m[4]); A->set_reg(c, 7, m[5]); A->set_reg(c, 8, m[26]);
        A->call(c, 0x09f5153c);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 2) {
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]);
        A->call(c, 0x09f53358);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 3) {
        A->set_reg(c, 4, m[24]);
        A->call(c, 0x0884d1b8);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 4) {
        const uint32_t error = *(uint32_t *)A->mem(0x09f7d4ec);
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]);
        A->call(c, 0x09f4fc18);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]); A->set_reg(c, 6, 0x1419);
        A->call(c, 0x09f465c0);
        *(uint32_t *)A->mem(mailbox + 36) = A->reg(c, 2);
        *(uint32_t *)A->mem(0x09f7d4ec) = error;
    } else if (m[7] == 5) {
        memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[25] & 1);
        A->set_reg(c, 4, mailbox + 800); A->set_reg(c, 5, m[4]); A->set_reg(c, 6, m[5]);
        A->call(c, 0x09eb9fc8);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 16) {
        pending_mode = m[24]; pending_source = m[25]; pending_side = m[4]; pending_slot = m[5];
    } else if (m[7] == 15) {
        const uint32_t card = *(uint32_t *)A->mem(SIDES + m[4] * SIDE_BYTES + 0x120 + 4 * m[5]);
        const uint32_t token = (((card >> 16) & 0x3fff) >> 6) * 2 + ((card >> 13) & 1);
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)(card & 0x1fff);
        A->mem(mailbox + 802)[0] = (uint8_t)(m[4] | (11 << 1));
        *(uint16_t *)A->mem(mailbox + 804) = (uint16_t)(token << 6);
        const uint32_t error = *(uint32_t *)A->mem(0x09f7d4ec);
        const uint32_t counter = *(uint32_t *)A->mem(0x09f790e8);
        A->set_reg(c, 4, mailbox + 800); A->set_reg(c, 5, 0);
        A->call(c, 0x09ed0cb0);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
        *(uint32_t *)A->mem(0x09f7d4ec) = error;
        *(uint32_t *)A->mem(0x09f790e8) = counter;
    } else if (m[7] == 14) {
        // Inspect the actual LP cost, native effect record and MST target path.
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[25] | (11 << 1));
        *(uint16_t *)A->mem(mailbox + 804) = 0x7fc0;
        A->set_reg(c, 4, mailbox + 800);
        A->call(c, 0x09ec6d64);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
        A->set_reg(c, 4, mailbox + 800);
        A->call(c, 0x09eb4574);
        const uint32_t record = A->reg(c, 2);
        *(uint32_t *)A->mem(mailbox + 40) = record ? *(uint32_t *)A->mem(record + 16) : 0;
        const uint32_t filter = record ? *(uint32_t *)A->mem(record + 8) : 0;
        A->set_reg(c, 4, mailbox + 800); A->set_reg(c, 5, m[4]); A->set_reg(c, 6, m[5]);
        if (filter) A->call(c, filter);
        *(uint32_t *)A->mem(mailbox + 36) = filter ? A->reg(c, 2) : 0;
    } else if (m[7] == 13) {
        // Query discard legality without paying the cost or changing the duel.
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[4] | (11 << 1));
        *(uint16_t *)A->mem(mailbox + 804) = (uint16_t)(m[5] << 6);
        A->set_reg(c, 4, mailbox + 800);
        A->call(c, 0x09edd77c);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 12) {
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[4] | (5 << 1));
        A->set_reg(c, 4, mailbox + 800);
        A->call(c, 0x09ed26d0);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 11) {
        // A hand-zone event avoids field destruction; this still invokes native event machinery.
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)(m[4] | (11 << 1));
        A->set_reg(c, 4, 0); A->set_reg(c, 5, mailbox + 800);
        A->call(c, 0x09ee55c8);
        *(uint32_t *)A->mem(mailbox + 32) = A->mem(mailbox + 804)[0];
    } else if (m[7] == 10) {
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[24]); A->set_reg(c, 6, 0);
        A->call(c, 0x09f2a7ec);
    } else if (m[7] == 9) {
        std::memset(A->mem(mailbox + 800), 0, 24);
        *(uint16_t *)A->mem(mailbox + 800) = (uint16_t)m[24];
        A->mem(mailbox + 802)[0] = (uint8_t)m[25];
        A->set_reg(c, 4, mailbox + 800); A->set_reg(c, 5, m[26]);
        A->set_reg(c, 6, 14); A->set_reg(c, 7, 2);
        A->call(c, 0x09f35580);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    } else if (m[7] == 8) {
        const uint32_t saved_mask = *(uint32_t *)A->mem(0x09f7bc48);
        *(uint32_t *)A->mem(0x09f7bc48) = m[26];
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]); A->set_reg(c, 6, 2);
        A->set_reg(c, 7, m[24]); A->set_reg(c, 8, m[25]);
        A->call(c, 0x09f3332c);
        *(uint32_t *)A->mem(mailbox + 32) = *(uint32_t *)A->mem(0x09f7bc48);
        *(uint32_t *)A->mem(0x09f7bc48) = saved_mask;
    } else {
        // Explicit action in this isolated duel: Allegrex argument five is t0.
        A->set_reg(c, 4, m[4]); A->set_reg(c, 5, m[5]); A->set_reg(c, 6, m[25]);
        A->set_reg(c, 7, m[26]); A->set_reg(c, 8, m[24]);
        A->call(c, 0x09f34d64);
        *(uint32_t *)A->mem(mailbox + 32) = A->reg(c, 2);
    }
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, regs[i]);
    if (m[7] != 6 && m[7] != 9 && m[7] != 10 && m[7] != 19) std::memcpy(A->mem(SIDES), saved.data(), saved.size());
    if (m[7] != 6 && m[7] != 9 && m[7] != 10 && m[7] != 19) std::memcpy(A->mem(0x09f7c460), links.data(), links.size());
    m[3] = 0; m[2] = sequence;
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api;
    if (api->version < 2 || !std::getenv("TF_CTL") || !std::getenv("TF3_TEST_PROBE")) return 1;
    mailbox = A->guest_alloc(1024);
    if (!mailbox) return 1;
    std::memset(A->mem(mailbox), 0, 1024);
    A->log("TF3 test mailbox %08x", mailbox);
    A->hook(0x08833fc4, frame);
    A->hook(0x09f3592c, self_destruction);
    if (std::getenv("TF3_TEST_ACTIONS")) A->hook(0x09ee38d8, before_selected_destruction);
    return 0;
}
