"""Execute the query-only V2 mailbox and its shared reply on the SHARC core."""

import json
import os
import subprocess
from functools import reduce
from operator import xor
from pathlib import Path

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit, pk_sharc, pk_toolchains


@pytest.fixture(scope="module")
def registry_object():
    adapters = REPO_ROOT / "modules/core/adapters"
    source = "\n".join(
        (adapters / f"instrument-registry/src/dsp/{name}.s").read_text()
        for name in ("registry", "lifecycle", "rows", "protected")
    )
    source += "\n" + (adapters / "midi-events/src/dsp/reply.s").read_text()
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20050000
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob) + 64
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_epoch": 0x20040000,
            "_mi_message": 0x20040004,
        },
    )
    return linked, symbols, initial


@pytest.fixture
def mailbox(registry_object):
    linked, symbols, initial = registry_object
    dm = {**initial, 0x20040000 // 4: 0, 0x20040004 // 4: 0}
    reads = {name: (address // 4, 1) for name, address in symbols.items()}
    reads["ack"] = (symbols["_instrument_registry_reply"] // 4, 8)
    reads["wire"] = (symbols["_instrument_reply"] // 4, 16)

    def execute(words, *, full=False, clear_session=False, lifecycle_ready=None):
        if lifecycle_ready is not None:
            dm[symbols["_ir_lifecycle_ready"] // 4] = lifecycle_ready
        if clear_session:
            dm[symbols["_instrument_session"] // 4] = 0
            dm[symbols["_ir_sequence"] // 4] = 0
        if words is not None:
            dm.update({symbols["_dl_irows"] // 4 + i: v for i, v in enumerate(words)})
        for entry in ("_instrument_registry_block", "_mi_reply_build"):
            result = pk_sharc.run_call(
                linked,
                entry,
                regs={"M5": 0, "M6": 1, "L0": 0, "L1": 0},
                dm_words=dm,
                reads=reads,
                max_steps=1500,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name, (address, _) in reads.items():
                dm.update({address + i: v for i, v in enumerate(result[name])})
        wire = result["wire"]
        assert wire[4:12] == result["ack"]
        session = result["_instrument_session"][0]
        assert wire[0] == (0x494EA102 if session else 0x494EA101)
        assert wire[12:15] == [session, 0, 0]
        a = b = 0
        for value in wire[:15]:
            a = (a + value) & 0xFFFFFFFF
            b = (b + a) & 0xFFFFFFFF
        assert wire[15] == a ^ b
        return result if full else result["ack"]

    return execute


def command(sequence=1, operation=1, **fields):
    words = [0x49524731, 1, sequence, 3, 9, operation, 7] + [0] * 9
    for index, value in fields.items():
        words[int(index)] = value
    words[15] = reduce(xor, words[:15])
    return words


@pytest.mark.parametrize("operation,status", [(1, 0), (2, 1), (3, 1), (99, 2)])
def test_query_and_unsupported_lifecycle(mailbox, operation, status):
    assert mailbox(command(operation=operation)) == [
        0x49525231,
        1,
        3,
        9,
        operation,
        7,
        status,
        0,
    ]


@pytest.mark.parametrize(
    "index,value",
    [
        (0, 0),
        (1, 2),
        (2, 0),
        (3, 32),
        (3, 0xFFFFFFFF),
        (4, 0),
        (6, 8),
        (7, 1),
        (14, 1),
    ],
)
def test_invalid_command_retains_ack(mailbox, index, value):
    ack = mailbox(command())
    assert mailbox(command(sequence=2, **{str(index): value})) == ack
    assert mailbox(command(sequence=2))[1] == 2


def test_checksum_failure_does_not_consume_sequence(mailbox):
    words = command()
    words[15] ^= 1
    assert mailbox(words) == [0] * 8
    assert mailbox(command())[1] == 1


def test_replay_old_sequence_and_wrap_are_rejected(mailbox):
    ack = mailbox(command(sequence=2))
    assert mailbox(command(sequence=2, operation=2)) == ack
    assert mailbox(command(sequence=1)) == ack
    ack = mailbox(command(sequence=0xFFFFFFFF))
    assert ack[1] == 0xFFFFFFFF
    assert mailbox(command(sequence=1)) == ack


def bind(sequence=1, token=0xDEADBEEF, **fields):
    return command(sequence, 5, **{"3": 0, "4": token, "6": 0, **fields})


def test_session_bind_consumes_commit_and_survives_ack_replacement(mailbox):
    result = mailbox(bind(), full=True)
    assert result["ack"] == [0x49525231, 1, 0, 0xDEADBEEF, 5, 0, 0, 0]
    assert result["_dl_irows"] == [0]
    assert result["wire"][12] == 0xDEADBEEF
    # A query replaces the ACK but not the per-block session attestation.
    result = mailbox(command(sequence=2), full=True)
    assert result["ack"][4] == 1
    assert result["wire"][12] == 0xDEADBEEF


@pytest.mark.parametrize("token", [0xDEADBEEF, 1, 0xFFFFFFFF])
def test_session_bind_cannot_renew_or_replace(mailbox, token):
    mailbox(bind())
    result = mailbox(bind(sequence=2, token=token), full=True)
    assert result["ack"][6:] == [5, 0]
    assert result["wire"][12] == 0xDEADBEEF
    assert result["_dl_irows"] == [0]
    assert mailbox(None, full=True, clear_session=True)["wire"][12] == 0


@pytest.mark.parametrize("fields", [{"3": 1}, {"6": 1}, {"4": 0}, {"7": 1}, {"14": 1}])
def test_invalid_bind_cannot_establish_session(mailbox, fields):
    result = mailbox(bind(**fields), full=True)
    assert result["ack"] == [0] * 8
    assert result["wire"][12] == 0
    assert mailbox(bind())[6] == 0


def test_consumed_bind_does_not_reestablish_cleared_session(mailbox):
    mailbox(bind())
    result = mailbox(None, full=True, clear_session=True)
    assert result["_instrument_session"] == [0]
    assert result["_dl_irows"] == [0]
    assert result["wire"][0] == 0x494EA101
    # This models a cleared latch, not a claim about hardware reset coverage.


def test_replayed_bind_is_consumed_without_new_ack(mailbox):
    first = mailbox(bind())
    result = mailbox(bind(), full=True)
    assert result["ack"] == first
    assert result["_dl_irows"] == [0]
    assert mailbox(None, full=True, clear_session=True)["wire"][12] == 0


def test_bind_after_lifecycle_admission_is_rejected(mailbox):
    result = mailbox(bind(), full=True, lifecycle_ready=1)
    assert result["ack"][6] == 5
    assert result["_instrument_session"] == [0]
    assert result["_dl_irows"] == [0]
    assert result["wire"][0] == 0x494EA101


@pytest.mark.parametrize("reset_notification", [False, True])
def test_dsp_session_replies_cross_cf_frame_publisher_and_snapshot(
    mailbox, tmp_path, reset_notification
):
    firmware = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
    runner = Path(
        os.environ.get(
            "DIGIKIT_CF_SCRIPT", REPO_ROOT / "target/release/examples/cf_script"
        )
    )
    if not firmware.exists() or not runner.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("DTII firmware, m68k compiler and cf_script required")
    # Actual registry command execution, reply construction and injected latch
    # loss. This does not model or claim a hardware DSP restart path.
    bound = mailbox(bind(sequence=17), full=True)["wire"]
    queried = mailbox(command(sequence=18), full=True)["wire"]
    lost = mailbox(None, full=True, clear_session=True)["wire"]
    helper = tmp_path / "session_boundary.c"
    helper.write_text("""
#include "bridge.h"
int dl_claim(uint32_t *lease);
int dl_send_owned(uint32_t lease, uint32_t win, uint32_t dst, const void *buf,
                  uint32_t len, uint32_t *ticket);
int dl_poll_tagged(uint32_t ticket, uint32_t *status);
int dl_release(uint32_t lease);
void mi_dsp_reset_hook(uint32_t asserted);
static uint32_t lease, ticket, tail_calls, tail_error;
/* Stand in for the stock tail: observe revocation at the point at which the
 * original reset helper would run, not only after its caller returns. */
void mi_dsp_reset_next(uint32_t asserted) {
    uint32_t status[7];
    ++tail_calls;
    if ((asserted & 255u) && ticket) {
        if(dl_poll_tagged(ticket,status)!=0 || status[0]!=1 || status[1]!=102)
            tail_error=1;
        if(dl_release(lease)!=-2) tail_error=2;
    }
}
uint32_t dkrt_irq_mask(void) { return 0; }
void dkrt_irq_restore(uint32_t x) { (void)x; }
int instrument_midi_ready(uint32_t t, uint32_t e, uint32_t g) {
    (void)t; (void)e; (void)g; return 0;
}
void *memcpy(void *d, const void *s, unsigned n) {
    volatile unsigned char *p=d; const volatile unsigned char *q=s;
    while(n--) { *p++=*q++; }
    return d;
}
void *memset(void *d, int c, unsigned n) {
    volatile unsigned char *p=d;
    while(n--) { *p++=(unsigned char)c; }
    return d;
}
uint32_t session_boundary(const uint32_t *words, uint32_t step) {
    static uint16_t record[0xabc/2];
    static uint16_t queue[0x2000] __attribute__((aligned(4)));
    static uint32_t payload=0x12345678;
    uint32_t out[16], i;
    if (step == 1 && RESET_NOTIFICATION) {
        if(dl_claim(&lease) || dl_send_owned(lease,3,0,&payload,4,&ticket)) return 15;
    }
    if (!step) {
        mi_dsp_reset_hook(1); /* initial assertion before adapter use is harmless */
        if (mi_registry_expect_session(0xdeadbeefu,17)) return 1;
    }
    if (step == 2 && RESET_NOTIFICATION) {
        mi_ledger.routes[0].enabled=1;
        mi_ledger.routes[0].track=0;
        mi_ledger.count=1;
        /* A nonzero upper byte is still a stock release, so neither owner
         * may be revoked. Then assert through the actual assembly hook. */
        mi_dsp_reset_hook(0x100);
        if(mi_registry_reply(out,16)!=16 || !mi_ledger.routes[0].enabled) return 18;
        if(dl_poll_tagged(ticket,out)!=0 || out[1]) return 19;
        uint32_t before=tail_calls;
        mi_dsp_reset_hook(1);
        uint32_t epoch=mi_ledger.epoch;
        mi_dsp_reset_hook(0x12340080);
        if(tail_calls!=before+2 || tail_error) return 20;
        if(mi_ledger.epoch!=epoch) return 14;
        if(mi_ledger.count || mi_ledger.routes[0].enabled) return 12;
    }
    for(i=0;i<MT_WORDS*2;++i) {
        queue[2u*(0x9fc/2+i)]=0xabcd;
        queue[2u*(0x9fc/2+i)+1u]=0xffff;
    }
    for (i=0;i<16;++i) {
        uint32_t h=(0x1cu+0x54u*(21u+i/2u)+0x44u+(i&1u)*4u)/2u;
        record[h]=(uint16_t)words[i]; record[h+1]=(uint16_t)(words[i]>>16);
        out[i]=0xccccccccu;
    }
    instrument_write_frame((uintptr_t)queue,0x9fc/2,record);
    int result=mi_registry_reply(out,16);
    if (step < 2) {
        if (result!=16) return 100+step*1000+result;
        for (i=0;i<16;++i) if(out[i]!=words[i]) return 3;
    } else {
        if (result!=-2) return 4;
        for (i=0;i<16;++i) if(out[i]!=0xccccccccu) return 5;
        if(mi_registry_expect_session(0xdeadbeefu,17)!=-2) return 6;
        if (RESET_NOTIFICATION) {
            uint8_t note[3]={0x90,60,100};
            uint32_t value=1;
            for(i=0;i<MT_WORDS*2;++i)
                if(queue[2u*(0x9fc/2+i)]!=0xabcd) return 13;
            for(i=0;i<MT_WORDS;++i) {
                if(queue[2u*(0x9fc/2+2u*i)+1u] ||
                   queue[2u*(0x9fc/2+2u*i+1u)+1u]) return 7;
            }
            if(mi_channel_message(note,3,2)) return 8;
            if(mi_config_submit(0,1,1,&value,1)!=-2) return 9;
            if(mi_route_enable(0,0,0,0)!=-2) return 10;
            mi_transport_recover();
            if(mi_registry_reply(out,16)!=-2) return 11;
            if(dl_poll_tagged(ticket,out)!=0 || out[0]!=1 || out[1]!=102) return 16;
            if(dl_release(lease)!=-2) return 17;
        }
    }
    return 0;
}
""")
    helper.write_text(
        helper.read_text().replace("RESET_NOTIFICATION", str(int(reset_notification)))
    )
    mod = REPO_ROOT / "modules/core/adapters/midi-events"
    linked = patchkit.cf_link(
        [
            mod / "src/cf/ownership.c",
            mod / "src/cf/transport.c",
            mod / "src/cf/adapter.c",
            mod / "src/cf/reset.s",
            REPO_ROOT / "modules/core/adapters/dsp-link/src/cf/dsp-link.c",
            helper,
        ],
        0x47E00000,
        {"_dl_area": 0, "dl_tx_lit": 0, "dl_rec_lit": 0, "pp_write_frame": 0},
        [mod / "src/cf", REPO_ROOT / "modules/core/adapters/dsp-link/build/gen"],
    )
    calls = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]
    for step, words in enumerate(
        [bound, queried, bound if reset_notification else lost, bound]
    ):
        calls.extend(
            [
                {
                    "op": "poke",
                    "addr": 0x47D00000,
                    "hex": b"".join(w.to_bytes(4, "big") for w in words).hex(),
                },
                {
                    "op": "call",
                    "tag": str(step),
                    "pc": linked.symbols["session_boundary"],
                    "args": [0x47D00000, step],
                    "max": 5_000_000,
                },
            ]
        )
    script = tmp_path / "session.json"
    script.write_text(json.dumps(calls))
    result = subprocess.run(
        [str(runner), str(firmware), str(script)],
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    executed = [row for row in rows if row.get("op") == "call"]
    assert [row["tag"] for row in executed] == ["0", "1", "2", "3"]
    for row in executed:
        assert row["ok"] and not row["error"] and row["d0"] == 0, row
