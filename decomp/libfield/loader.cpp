/* libfield: the file loader of the field: a queue of jobs (read a file of the resource table into the module's heap, then call back), the table of the nine
 * resource files and small lookups the map uses (the player's progress stage, the kind of ground under the player). */
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

constexpr uint32_t kHeapMemory = 0x09212900, kHeapBytes = 0x780000;   /* where the module's heap lives and how big it is */
constexpr uint32_t kPoolMemory = 0x0920B700, kPoolBytes = 0x7200;
constexpr uint32_t kFileDevice = 3;
constexpr uint32_t kPathFormat = 0x091FDE98;   /* "%s/%s": directory and file of a resource entry */

/* a file of the table (0x28 bytes each); entry 0's file name is picked by the language when the loader starts */
struct ResourceEntry {
    int32_t id;
    uint32_t directory, file;      /* +0x04 guest pointers to the names the path is made of */
    uint32_t registerArchive;      /* +0x0C nonzero: the loaded file is also registered with the engine as an archive */
    uint32_t buffer;               /* +0x10 where the file was read */
    uint32_t archive;              /* +0x14 the loaded archive (callbacks get its address) */
    int32_t requested;             /* +0x18 the load was queued */
    uint32_t waitersHead, waitersTail;   /* +0x1C others that asked for the same file while it loads */
    int32_t registered;            /* +0x24 */
};
static_assert(sizeof(ResourceEntry) == 0x28, "ResourceEntry layout");
constexpr uint32_t kResourceTable = 0x091FD5F8;
constexpr int32_t kResourceCount = 9;

/* one queued job; kind 0 loads an entry of the table, kinds 1..5 load other records */
struct LoadJob {
    int32_t kind;
    int32_t state;                 /* +0x04 0 = not started, 1 = the file is in memory */
    uint32_t target;               /* +0x08 the entry (kind 0) or a record */
    uint32_t extra;                /* +0x0C */
    uint32_t argument;             /* +0x10 handed to the callback */
    uint32_t next;                 /* +0x14 */
    uint32_t callback;             /* +0x18 guest function: callback(archive address, argument) */
};
static_assert(sizeof(LoadJob) == 0x1C, "LoadJob layout");
struct LoaderState {
    uint32_t heap, pool;
    uint32_t queueHead, queueTail;
    int32_t reading;               /* +0x10 a file read is under way */
};
static_assert(sizeof(LoaderState) == 0x14, "LoaderState layout");
constexpr uint32_t kLoaderState = 0x09992900;
LoaderState &loader(SwapCtx &x) { return datum<LoaderState>(x, kLoaderState); }

/* ---- FUN_091cc198: starts the loader: the heap, the pool and the resource table ---- */
void swap_StartLoader(SwapCtx &x) {
    memset(&loader(x), 0, sizeof(LoaderState));
    loader(x).heap = ehsys::InitHeap(x, kHeapMemory, kHeapBytes);
    x.callLocal(fn::CreatePool);
    x.callLocal(fn::ResetResourceTable);
}
SwapReg reg_StartLoader({"libfield.StartLoader", MODULE, offsetOf(0x091CC198), swap_StartLoader});

/* ---- FUN_091cc51c / FUN_091cc564: a pool of fixed blocks, made once; its handle ---- */
void swap_CreatePool(SwapCtx &x) {
    if (loader(x).pool == 0) loader(x).pool = x.callNid(engine::nid::CreatePool, kPoolMemory, kPoolBytes, 0);
}
SwapReg reg_CreatePool({"libfield.CreatePool", MODULE, offsetOf(0x091CC51C), swap_CreatePool});
void swap_PoolHandle(SwapCtx &x) { x.ret(loader(x).pool); }
SwapReg reg_PoolHandle({"libfield.PoolHandle", MODULE, offsetOf(0x091CC564), swap_PoolHandle});

/* ---- FUN_091ccfbc(id, table, count, stride): the index of the record whose first word is the id; -1 when there is none ---- */
void swap_FindRecord(SwapCtx &x) {
    const uint32_t id = x.arg(0), table = x.arg(1), stride = x.arg(3);
    const int32_t count = (int32_t)x.arg(2);
    for (int32_t i = 0; i < count; i++) {
        if (*view<uint32_t>(x, table + i * stride) == id) { x.ret((uint32_t)i); return; }
    }
    x.ret(0xFFFFFFFFu);
}
SwapReg reg_FindRecord({"libfield.FindRecord", MODULE, offsetOf(0x091CCFBC), swap_FindRecord});

/* ---- FUN_091cce60: forgets the loaded files; picks the file name of entry 0 by the console's language and region ---- */
void swap_ResetResourceTable(SwapCtx &x) {
    const uint32_t region = x.callNid(ehsys::nid::GetUiMode);
    const uint32_t language = x.callNid(ehsys::nid::GetLanguage);
    ResourceEntry *table = (ResourceEntry *)x.mem(guest(x, kResourceTable));
    for (int32_t i = 0; i < kResourceCount; i++) {
        table[i].buffer = 0;
        table[i].archive = 0;
        table[i].requested = 0;
        memset(&table[i].waitersHead, 0, 8);
        table[i].registered = 0;
    }
    table[0].file = datum<uint32_t>(x, 0x091FD9CC + (region * 6 + language) * 4);   /* "always_xxx_xxx.ehp" by language */
    for (uint32_t i = 0; i < 0x1E; i++) datum<uint32_t>(x, 0x091FDCA4 + i * 12 + 8) = 0;   /* forget the cached sprites */
    memset(x.mem(guest(x, 0x09992AE4)), 0, 0x184);
    memset(x.mem(guest(x, 0x09992AA0)), 0, 0x44);
    memset(x.mem(guest(x, 0x09992A7C)), 0, 0x24);
    memset(x.mem(guest(x, 0x09992938)), 0, 0x144);
}
SwapReg reg_ResetResourceTable({"libfield.ResetResourceTable", MODULE, offsetOf(0x091CCE60), swap_ResetResourceTable});

/* ---- FUN_091cc570(id, callback, argument): asks for a resource file; the callback runs at once when it is loaded, else once it is ---- */
void swap_RequestResource(SwapCtx &x) {
    const uint32_t callback = x.arg(1), argument = x.arg(2);
    const int32_t index = (int32_t)x.callLocal(fn::FindRecord, x.arg(0), guest(x, kResourceTable), kResourceCount, sizeof(ResourceEntry));
    const uint32_t entryAddress = guest(x, kResourceTable) + index * sizeof(ResourceEntry);
    ResourceEntry &entry = *view<ResourceEntry>(x, entryAddress);
    if (entry.archive != 0) {
        x.callAddr(callback, entryAddress + offsetof(ResourceEntry, archive), argument);
        return;
    }
    entry.registered = 0;
    if (entry.requested == 0) {
        entry.requested = 1;
        x.callLocal(fn::EnqueueJob, 0, entryAddress, 0, callback, argument, 0);
    } else {
        x.callLocal(fn::AddWaiter, entryAddress + offsetof(ResourceEntry, waitersHead), callback, argument);
    }
}
SwapReg reg_RequestResource({"libfield.RequestResource", MODULE, offsetOf(0x091CC570), swap_RequestResource});

/* ---- FUN_091cd09c(kind, target, extra, callback, argument, atFront): puts a job in the queue (at its end, or second when atFront); 0 when out of memory ---- */
void swap_EnqueueJob(SwapCtx &x) {
    const uint32_t kind = x.arg(0), target = x.arg(1), extra = x.arg(2), callback = x.arg(3), argument = x.arg(4), atFront = x.arg(5);   /* read before the call below clobbers them */
    const uint32_t jobAddress = x.callLocal(fn::HeapAlloc, sizeof(LoadJob));
    if (jobAddress == 0) { x.ret(0); return; }
    LoadJob &job = *view<LoadJob>(x, jobAddress);
    job.kind = (int32_t)kind;
    job.state = 0;
    job.target = target;
    job.extra = extra;
    job.callback = callback;
    job.argument = argument;
    job.next = 0;
    LoaderState &state = loader(x);
    uint32_t head = jobAddress, tail = jobAddress;
    if (state.queueHead != 0) {
        if (atFront == 0) {
            view<LoadJob>(x, state.queueTail)->next = jobAddress;
            head = state.queueHead;
        } else {
            job.next = view<LoadJob>(x, state.queueHead)->next;
            view<LoadJob>(x, state.queueHead)->next = jobAddress;
            head = state.queueHead;
            tail = state.queueTail;
        }
    }
    state.queueTail = tail;
    state.queueHead = head;
    x.ret(1);
}
SwapReg reg_EnqueueJob({"libfield.EnqueueJob", MODULE, offsetOf(0x091CD09C), swap_EnqueueJob});

/* ---- FUN_091cd178: drops the job at the head of the queue ---- */
void swap_FinishJob(SwapCtx &x) {
    LoaderState &state = loader(x);
    const uint32_t finished = state.queueHead;
    if (finished == 0) return;
    state.queueHead = view<LoadJob>(x, finished)->next;
    x.callLocal(fn::HeapFree, finished);
}
SwapReg reg_FinishJob({"libfield.FinishJob", MODULE, offsetOf(0x091CD178), swap_FinishJob});

/* ---- FUN_091cd034(archive, size): the engine's read callback: the file is in memory; the job moves on ---- */
void swap_OnFileRead(SwapCtx &x) {
    LoaderState &state = loader(x);
    const uint32_t jobAddress = state.queueHead;
    if ((int32_t)x.arg(1) < 1) { x.ret(0); return; }
    if (x.arg(0) != 0) x.callNid(ehsys::nid::RegisterArchive, 0, x.arg(0));
    view<LoadJob>(x, jobAddress)->state++;
    state.reading = 0;
    x.ret(1);
}
SwapReg reg_OnFileRead({"libfield.OnFileRead", MODULE, offsetOf(0x091CD034), swap_OnFileRead});

/* ---- FUN_091cd320(entry): starts reading the file of a resource entry into the heap; 0 when it is missing or the heap is full ---- */
void swap_StartFileRead(SwapCtx &x) {
    const uint32_t entryAddress = x.arg(0);
    ResourceEntry &entry = *view<ResourceEntry>(x, entryAddress);
    GuestStack path(x, 256);
    x.callImport("sprintf", path.addr(), guest(x, kPathFormat), entry.directory, entry.file);
    const int32_t size = (int32_t)x.callNid(ehsys::nid::FileSize, kFileDevice, path.addr());
    if (size < 1) { x.ret(0); return; }
    entry.buffer = x.callLocal(fn::HeapAlloc, (uint32_t)size);
    if (entry.buffer == 0) { x.ret(0); return; }
    const uint32_t block = entry.buffer;
    x.callNid(ehsys::nid::ReadFileAsync, kFileDevice, path.addr(), block, (uint32_t)size, guest(x, 0x091CD034), entry.registerArchive == 0 ? 0 : block);
    loader(x).reading = 1;
    x.ret(1);
}
SwapReg reg_StartFileRead({"libfield.StartFileRead", MODULE, offsetOf(0x091CD320), swap_StartFileRead});

/* ---- FUN_091cc1e4: one frame of the loader: runs the job at the head of the queue ---- */
void runCallback(SwapCtx &x, uint32_t callback, uint32_t archiveAddress, uint32_t argument) { x.callAddr(callback, archiveAddress, argument); }
void swap_PumpLoader(SwapCtx &x) {
    LoaderState &state = loader(x);
    if (state.queueHead == 0 || state.reading != 0) return;
    const uint32_t jobAddress = state.queueHead;
    LoadJob &job = *view<LoadJob>(x, jobAddress);
    if (job.kind == 0) {
        if (job.state == 1) {
            ResourceEntry &entry = *view<ResourceEntry>(x, job.target);
            GuestStack waiters(x, 8);
            *(uint32_t *)x.mem(waiters.addr()) = entry.waitersHead;
            *(uint32_t *)x.mem(waiters.addr() + 4) = entry.waitersTail;
            entry.archive = entry.buffer;
            if (entry.registered != 0) x.callLocal(fn::RegisterResource, (uint32_t)entry.id);
            runCallback(x, job.callback, job.target + offsetof(ResourceEntry, archive), job.argument);
            if (*(uint32_t *)x.mem(waiters.addr()) != 0) x.callLocal(fn::NotifyWaiters, waiters.addr(), job.target + offsetof(ResourceEntry, archive));
            x.callLocal(fn::FinishJob);
        } else if (job.state == 0) {
            x.callLocal(fn::StartFileRead, job.target);
        }
    } else if (job.kind >= 1 && job.kind <= 5) {
        if (job.state == 1) {
            uint32_t *record = view<uint32_t>(x, job.extra);
            GuestStack waiters(x, 8);
            *(uint32_t *)x.mem(waiters.addr()) = record[4];
            *(uint32_t *)x.mem(waiters.addr() + 4) = record[5];
            record[2] = record[1];
            if (record[6] != 0) {
                const uint32_t mapped = job.kind == 5 ? 4 : job.kind == 4 ? 3 : job.kind == 3 ? 2 : job.kind == 2 ? 1 : 0;
                x.callLocal(fn::RegisterRecord, mapped, record[0]);
            }
            runCallback(x, job.callback, job.extra + 8, job.argument);
            if (*(uint32_t *)x.mem(waiters.addr()) != 0) x.callLocal(fn::NotifyWaiters, waiters.addr(), job.extra + 8);
            x.callLocal(fn::FinishJob);
        } else if (job.state == 0) {
            x.callLocal(fn::StartRecordRead, job.target, job.extra + 4, view<uint32_t>(x, job.extra)[7]);
        }
    }
}
SwapReg reg_PumpLoader({"libfield.PumpLoader", MODULE, offsetOf(0x091CC1E4), swap_PumpLoader});

}  // namespace
