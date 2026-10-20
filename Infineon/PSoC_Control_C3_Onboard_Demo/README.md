# PSOC Control C3 on-board demo

Work towards running the post-quantum TPM demo on the MCU itself rather than on a host, with the host reduced to a screen. Validated against an Infineon SLB9678 TPM 2.0 on the mikroBUS connector, firmware 45.91.

## Driving the TPM from the board

`tpm_identity.c` reads the TPM's identity with no host involved. It needs no changes to wolfTPM: `WOLFTPM_EXAMPLE_HAL` declares `TPM2_IoCb` without compiling any of wolfTPM's own HAL sources, and `tpm_io_i2c_c3.c` supplies it on top of wolfBoot's I2C driver.

The transfer sequence is wolfBoot's rather than a rewrite: the register write is followed by a stop and a guard delay instead of a repeated start, because the part NAKs briefly after being addressed while it wakes and the specification asks for a guard time that a repeated start cannot provide.

```
make tpm WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl> WOLFTPM_DIR=<wolftpm>
```

Measured on a C3M6, 1 October 2026:

```
TPM identity, read on the PSOC Control C3
  TPM2_Init_ex (no selftest): 0x00000000  bus and TIS OK
  wolfTPM2_Init            : 0x00000000  started
  wolfTPM2_GetCapabilities : 0x00000000
  Mfg IFX Vendor <vendor string> Fw <version>
done
```

The two initialisation calls are reported separately on purpose. `TPM2_Init_ex` brings up the TIS layer without issuing a startup, so a part that answers its registers but refuses commands can be told apart from a bus that does not work. From the demo's point of view those two failures look identical, and they call for completely different responses.

### Footprint

```
   text    8520    data 1348    bss 12040
```

Under 10 KB of flash and 13.4 KB of RAM. Most of the RAM is the 5 KB command and response buffers, sized in `user_settings_tpm.h` to hold a parameter set 87 signature; an identity read alone would need far less. The code is small because this path needs no cryptography and the linker discards what it does not use, so expect it to grow once keys, sealing and sessions are exercised.

A board-side act costs roughly 10 KB of flash for the TPM path and about 24 KB of RAM. On a part with 512 KB of flash and 128 KB of SRAM, neither is a constraint.

### The strap, when there is no bootloader

A standalone image has to drive the interface-select strap itself, and it is the first thing `main()` does. The TPM latches its bus from that pin while its own reset is low, and its reset trails the CPU's by about a millisecond, so there is very little time. With wolfBoot in front of the application `hal_init()` has already done it; the standalone path only just gets there.

## The whole act, on the board

`mldsa_onboard.c` runs the demo's central beat with no host taking part in the cryptography: the TPM creates a Hash-ML-DSA key and signs a SHA-256 digest, and the MCU verifies that signature itself with wolfCrypt. Only the public key ever leaves the TPM.

```
make onboard WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl> WOLFTPM_DIR=<wolftpm>
```

A run on a C3M6, with the `ms` timing fields each event also carries elided:

```
{"event":"run.start","host":"psoc_c3","alg":"HASH_MLDSA","param_set":87}
{"event":"tpm.startup","ok":true}
{"event":"key.create","alg":"HASH_MLDSA","pub_bytes":2592}
{"event":"sign.digest","alg":"HASH_MLDSA","param_set":87,"sig_bytes":4627}
{"event":"sign.bytes","alg":"HASH_MLDSA","bytes":4627,"b64":"..."}
{"event":"verify.host","backend":"wolfcrypt","on":"psoc_c3","result":"valid"}
{"event":"result","pass":true}
{"event":"run.end"}
```

These are the same newline-delimited JSON events the host-side example emits, so an existing UI consumes them unchanged. The base64 encoder streams three bytes at a time with no staging buffer, which is what lets a 4627 byte signature leave a part that has nowhere to hold the encoded form.

### Footprint

```
   text   52676    data 1360    bss 51164
```

About 54 KB of flash and 52 KB of RAM, the latter including a 24 KB heap for wolfTPM's internal allocations, the 9 KB ML-DSA key object, the signature buffer and the 5 KB TPM command and response buffers. On a part with 512 KB of flash and 128 KB of SRAM there is room for the remaining acts.

`wolfTPM2_GetKeyTemplate_HASH_MLDSA()` takes the object attributes before the parameter set; reversing them returns `TPM_RC_RESERVED_BITS` on parameter 2.

## Verifying a post-quantum signature on the MCU

`mldsa_bench.c` times `wc_MlDsaKey_VerifyCtx()` on the C3M6 itself, which is
what decides whether the verification can stay on the board rather than going
back to a host. No TPM takes part and none has to be attached: the key,
message and signature are the fixed ML-DSA-87 vector in `mldsa_tv.h`, so the
figure is repeatable, and the build needs no `WOLFTPM_DIR`.

```
make bench WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl>
```

It prints the cycle count for each of `BENCH_ITERATIONS` runs and the best,
worst and mean. Cycles are the measurement; the milliseconds beside them are
only as good as `CPU_HZ`, so the run begins by spinning `CLKCAL_CYCLES`
between two console markers, which lets whatever is reading the console
derive the core clock from the wall time between them and confirm or correct
that constant. The timebase is the DWT cycle counter where the core
implements it and SysTick where it does not, which the first lines of the
output say.

## All the acts, selected by a command byte

`demo_acts.c` puts the acts in one image and picks between them with a single
character on the console, so a host-side demo server becomes a serial reader
rather than something that spawns programs and parses their output.

| Byte | Act |
| --- | --- |
| `i` | identity: vendor and firmware |
| `p` | measured boot: read the PCR bank |
| `x` | extend PCR 16, then re-read the bank |
| `r` | reset PCR 16, then re-read the bank |
| `s` | sign and verify |
| `t` | sign and verify with a tampered signature, which must be rejected |
| `S` | as `s`, with ML-DSA-65 instead of 87 |
| `T` | as `t`, with ML-DSA-65 instead of 87 |
| `l` | sealed secret: seal to a PCR, unseal, extend, fail, reset |
| `o` | as `l`, stopping after the matching unseal |
| `e` | endorsement: read the EK certificates out of NV |
| `d` | SPDM: negotiate an encrypted session with the part, only in the `spdm` build below |
| `k` | report the public key of the TPM-held TLS identity |
| `g` | sign a SHA-256 digest, given as 64 hex characters and a newline, with that key |

```
make acts WOLFBOOT_DIR=<wolfboot> WOLFSSL_DIR=<wolfssl> WOLFTPM_DIR=<wolftpm>
```

Footprint: `text 64228  data 1360  bss 56884`, so about 64 KB of flash and 56 KB of RAM for every act in one image. The acts share `demo_util.c` for console output and timing and `act_sign.c` for the signing beat, so the single-act `mldsa_onboard` image and this one run the same code rather than two copies of it.

Anything other than a listed character is ignored, so a host opening the port and sending line noise cannot start a run.

### Test status

All acts verified on hardware, 1 October 2026, driven by `onboard.py` from the demo repository:

| Act | Result |
| --- | --- |
| identity | `Mfg IFX, Vendor SLB9678 VU, Fw 45.91` |
| measured boot | all 24 PCR indices read |
| sign and verify | valid, `pass: true` |
| sign, tampered | rejected at offset 2313, `pass: true` |
| sealed secret | unsealed while the measurement matched, refused with `TPM_RC_POLICY_FAIL` after it changed, `pass: true` |
| endorsement | 5 certificates, 869 to 1580 bytes |

### What sealing requires

- An ECC storage root key. An RSA SRK is slow enough on this part to look hung, and can leave the TPM in failure mode until power is removed.
- A policy digest computed with a trial session, and `TPMA_OBJECT_userWithAuth` cleared. Deriving the policy from a PCR list alone leaves the object openable with its auth value, so the PCR policy is not what gates it, and every unseal returns `TPM_RC_POLICY_FAIL`.
- `wolfTPM2_SetAuthHandleName()` as well as `wolfTPM2_SetAuthSession()`. The session HMAC covers the name of the object being authorised; without it the TPM returns `TPM_RC_BAD_AUTH` (0x9a2).
- The digest length passed to `wolfTPM2_ReadPCR()` carries the buffer size in as well as the length out.

### Reading the endorsement certificates

Enumerate the TCG NV space with `wolfTPM2_GetHandles()` rather than trying a fixed list of indices, and read the public area first to size each certificate. Which indices a part ships with varies.
