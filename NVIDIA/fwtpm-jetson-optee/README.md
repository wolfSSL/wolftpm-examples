# fwTPM on the NVIDIA Jetson Orin, as an OP-TEE trusted application

The wolfTPM firmware TPM 2.0 running in the secure world on a Jetson Orin, as
an OP-TEE trusted application, with unmodified wolfTPM examples driving it
from Linux over `libteec`.

The Jetson already has a firmware TPM: NVIDIA ships Microsoft's TPM 2.0
reference as an early trusted application, reached through `tpm_ftpm_tee` as
`/dev/tpmrm0`. It reports **TPM 2.0 spec revision 1.62**, and ML-DSA
(FIPS 204) and ML-KEM (FIPS 203) arrive in revision **1.85**, so it has no
post-quantum support until NVIDIA ships new firmware. This port puts a 1.85
TPM on the same silicon alongside it.

## Tested target

| Board | Status | Date |
|-------|--------|------|
| Jetson Orin Nano Engineering Reference Developer Kit Super, L4T R36.4.4, kernel 5.15.148-tegra | `Mfg WOLF (0), Vendor wolfTPM`, `TPM_PT_REVISION` 185. `caps`, `getrandom`, `native_test`, `wrap_test`, `pqc/mldsa_sign`, `pqc/mlkem_encap` and both PQC negative-control examples pass. NV persists across processes. | 2026-10-28 |

## Prerequisites

- `aarch64-linux-gnu-gcc`. Nothing else is needed on the build host.
- An OP-TEE TA devkit (`export-ta_arm64`), optee_os 4.4.0 verified. The arm64
  TA devkit targets the TEE Internal Core API, so the platform it was built
  for does not matter for an ordinary trusted application.
- wolfTPM with `WOLFTPM_OPTEE` support, which supplies
  `src/fwtpm/ports/optee/` and `wolftpm/fwtpm/fwtpm_optee.h`.
- wolfSSL with ML-DSA and ML-KEM (`--enable-mldsa --enable-mlkem
  --enable-experimental`), and with the `WOLFSSL_OPTEE` heap and RNG support,
  which supplies the allocator and entropy so this port needs no shims. Only
  the sources are used for the trusted application; the install is for the
  Linux-side examples.
- On the board, `nvidia-l4t-optee`, which ships `tee_client_api.h` and
  `libteec`, so the client builds natively there.

`/dev/tee0` and `/dev/teepriv0` are mode `0600` and root owned, so **every
client runs under sudo**.

## Signing

A filesystem-loaded trusted application has to be signed with a key the
platform's OP-TEE core trusts. Check what your image uses, against a trusted
application it already ships, before building anything:

```sh
python3 scripts/sign_encrypt.py verify \
    --key keys/default_ta.pem \
    --uuid 5b9e0e40-2636-11e1-ad9e-0002a5d5c51b \
    --in 5b9e0e40-2636-11e1-ad9e-0002a5d5c51b.ta
```

Exit 0 means that key. **Run the negative control too**, or exit 0 tells you
nothing: repeat with a freshly generated key (`openssl genrsa -out wrong.pem
2048`) and confirm it reports a verification failure.

On L4T R36.4.4 the shipped trusted applications verify against upstream
OP-TEE's `keys/default_ta.pem`, so the devkit's default key works unchanged.

## Build and run

```sh
export WOLFSSL=../../../../wolfssl
export WOLFTPM=../../../../wolftpm
export TA_DEV_KIT_DIR=/path/to/optee_os/out/.../export-ta_arm64
```

### 1. Build and sign the trusted application

```sh
cd ta && WOLFSSL=$WOLFSSL WOLFTPM=$WOLFTPM ./prep.sh
make TA_DEV_KIT_DIR=$TA_DEV_KIT_DIR CROSS_COMPILE=aarch64-linux-gnu-
```

`prep.sh` symlinks the wolfCrypt, fwTPM and OP-TEE port sources plus the three
include roots into the build directory, and generates `uuid.mk` and
`uuid_macro.inc` from wolfTPM. The OP-TEE build resolves every `srcs-y` and
`global-incdirs-y` entry against the `sub.mk` directory, so nothing outside it
can be named directly. Re-run `prep.sh` after moving either tree. Signing is
automatic and uses the devkit key.

Configuration is one `user_settings.h` for both wolfCrypt and wolfTPM, reached
through `WOLFSSL_USER_SETTINGS` and `WOLFTPM_USER_SETTINGS`. There is no
generated `options.h` here, and that is also what keeps an installed
`/usr/local/include/wolftpm/options.h` out of the build.

### 2. Deploy

```sh
scp 9013be0e-39b0-4ed4-80e0-1242c0daf382.ta board:/tmp/
ssh board "sudo install -m 0644 -o root -g root \
    /tmp/9013be0e-39b0-4ed4-80e0-1242c0daf382.ta /lib/optee_armtz/"
```

No service restart: `tee-supplicant` reads `/lib/optee_armtz/` per request.

**Before installing, run the client once and confirm it fails with
`TEEC_OpenSession 0xffff0008` (`TEEC_ERROR_ITEM_NOT_FOUND`), origin 3.**
Otherwise a later success may be a stale trusted application rather than the
one you just built.

### 3. Drive it

The minimal `libteec` client, built on the board:

```sh
make -C host && sudo ./host/fwtpm_client
```

Or unmodified wolfTPM examples, which is the point of the port:

```sh
cd wolftpm
./configure --enable-optee --disable-swtpm --enable-v185 \
    CFLAGS="-I$WOLFSSL_PREFIX/include" LDFLAGS="-L$WOLFSSL_PREFIX/lib"
make
sudo ./examples/wrap/caps
sudo ./examples/pqc/mldsa_sign
```

`--disable-swtpm` is required: the software TPM is the default on Linux
aarch64 and would win. Build the client **without** `--enable-fwtpm`; the
trusted application is the server.

## The trusted application's UUID

OP-TEE identifies a trusted application by UUID, not by name, and the UUID is
not cosmetic: the TEE loads a filesystem trusted application from
`/lib/optee_armtz/<uuid>.ta`, and a client opens a session by UUID. So the
same value has to appear in three places that are built separately, and a
mismatch shows up only at run time as `TEEC_OpenSession` returning
`0xffff0008` (`TEEC_ERROR_ITEM_NOT_FOUND`).

There is one definition, `WOLFTPM_OPTEE_TA_UUID_STR` in wolfTPM's
`wolftpm/fwtpm/fwtpm_optee.h`, and everything here derives from it:

| Where | How it gets the UUID |
|-------|----------------------|
| `TA_UUID` in the trusted application | `ta/uuid_macro.inc`, generated by `prep.sh` |
| the signed binary's filename | `BINARY` in `ta/Makefile`, from `ta/uuid.mk`, generated by `prep.sh` |
| the client | `WOLFTPM_OPTEE_TA_UUID` in `wolftpm/tpm2_optee.h`, built from the same macros |

`ta/uuid.mk` and `ta/uuid_macro.inc` are generated, not checked in. This is
the UUID wolfTPM's fwTPM trusted application uses, so a stock client and a
stock trusted application interoperate with no configuration. To ship your own
identity, override `WOLFTPM_OPTEE_TA_UUID_STR` and the `_P1` to `_P4` macros
beside it; all three places then follow.

## Developing or porting this

`native/` compiles the same source list with the same `user_settings.h` for
the host and drives `FWTPM_ProcessCommand` through a RAM NV HAL:

```sh
make -C native && ./native/fwtpm_native
```

Nothing about running the fwTPM on a board needs it. It is for changing the
port: a configuration or source-list mistake surfaces in seconds there
instead of after a cross-compile, copy and deploy cycle, and it runs under a
host debugger and the sanitizers.

## How it works

`FWTPM_ProcessCommand` takes a command buffer and returns a response buffer,
which is exactly the shape of `TA_InvokeCommandEntryPoint`, so one
`TEEC_InvokeCommand` carries one whole TPM command. There is no TIS register
emulation, which would cost a world switch per register access.

`FWTPM_IO_ServerLoop` and `FWTPM_TIS_ServerLoop` both block, which a trusted
application cannot do, so `fwtpm_io.c`, `fwtpm_tis.c`, `fwtpm_tis_shm.c` and
`fwtpm_main.c` are not part of this build.

NV lives in OP-TEE secure storage in the journal's default byte-addressable
mode; the append-only option is for write-once flash and is not used here. The
journal integrity key is derived from the hardware unique key through the
system PTA, falling back to a random key in its own storage object on
platforms built without that PTA.

`locality` is fixed and never taken from the client, because it is stored
verbatim into the command context without validation.

## Sizes

Measured on this board, bounded from both sides, acceptance being
`native_test` plus `pqc/mldsa_sign`:

| Axis | Passes | Fails |
|------|--------|-------|
| `TA_DATA_SIZE` (heap) | 96 KB | 88 KB |
| `TA_STACK_SIZE` | 8 KB | 4 KB |

ML-DSA is the binding constraint: at 64 KB of heap `native_test` still passes
and only ML-DSA fails. The acceptance test signs with ML-DSA-65, so ML-DSA-87
needs more.

`FWTPM_CTX` is a 161 KB static in `.bss` and does not come out of this heap,
which is why a 96 KB heap works at all. The trusted application load ceiling
on this board is between 47 and 48 MB.

Shipping defaults are 32 KB of stack and 512 KB of heap. `sweep.sh` re-measures
on another board; sweep until something fails, or you have not found a floor.

The heap and stack floors are the same with the assembly in or out, because
ML-DSA binds the heap and the single-precision code allocates its temporaries
rather than using stack.

### Assembly, and what it costs

Both are on by default on aarch64 and each has an opt-out, so a target that
wants a smaller image or lacks the instructions can drop either:

| Build | `.text` | `.rodata` | TA image |
|-------|---------|-----------|----------|
| `FWTPM_TA_NO_SP_ASM` and `FWTPM_TA_NO_ARMASM` | 480624 | 34241 | 604872 |
| `FWTPM_TA_NO_ARMASM` only | 586772 | 530361 | 1215176 |
| default, both enabled | 641120 | 532841 | 1272600 |

The single-precision assembly is what costs the space, and nearly all of it is
`.rodata`: the ECC precomputed base-point tables, plus RSA 4096 and P-384
support. It buys RSA-2048 private 10.9x and ECDSA P-256 sign 46x. The ARMv8
crypto extensions add 4.7% for ML-KEM encapsulate +32% and decapsulate +48%,
ECC P-256 key generation +38%, AES +15% and SHA-256 +8%.

ML-DSA gains nothing measurable from either: there is no aarch64 ML-DSA
polynomial assembly, and Keccak is not where its time goes.

`FWTPM_TA_NO_ARMASM` also drops the `-march=armv8.2-a+crypto` requirement in
`ta/sub.mk`, which exists because the ML-KEM transform uses SQRDMLSH from
FEAT_RDM and the devkit names no architecture.

## Benchmarks

`examples/bench/bench`, same tool and board as the vendor fTPM figures, second
run kept so the first pays the trusted application load and the frequency ramp.

| Operation | This port | NVIDIA vendor fTPM |
|-----------|----------:|-------------------:|
| RSA-2048 key gen | 1043 ms | 736 ms |
| RSA-2048 public | 3038 ops/sec | 1144 ops/sec |
| RSA-2048 private | 56.8 ops/sec | 84 ops/sec |
| ECDSA P-256 sign | 204 ops/sec | 22.2 ops/sec |
| ECDSA P-256 verify | 316 ops/sec | 31.5 ops/sec |
| ECDHE P-256 agree | 104 ops/sec | 11.2 ops/sec |
| AES-256-CBC | 15.6 MB/s | 2.9 MB/s |
| SHA-256 | 7.79 MB/s | 1.57 MB/s |
| ML-DSA-65 sign | 1012 ops/sec | not supported |
| ML-DSA-65 verify | 2072 ops/sec | not supported |
| ML-KEM-768 encapsulate | 6956 ops/sec | not supported |
| ML-KEM-768 decapsulate | 4684 ops/sec | not supported |
| SHA-1 | not supported | supported |
| AES-CTR | not supported | supported |

Faster than the vendor part on ECC, AES and SHA-256, **slower on RSA private
and RSA key generation**, and it does not implement SHA-1 or AES-CTR where the
vendor fTPM skips nothing.

**`bench` key generation rows for ML-DSA and ML-KEM are not key generation.**
The loop calls `wolfTPM2_CreatePrimaryKey` on an unchanging template and the
fwTPM has a four-entry primary cache, so every iteration after the first is a
cache hit. `bench -iter=100` shows it: ML-DSA-65 key gen min 0.247 ms, max
72.8 ms, while ECC P-256 key gen stays within 5.065-5.150 ms because it uses
`TPM2_Create` and is not cached. A first, uncached ML-DSA-65 `CreatePrimary`
costs roughly 73-90 ms, part of which is the NV journal write to secure
storage.

## Reaching it as /dev/tpm1, with standard Linux tooling

The libteec client above is not the only way in. Linux's `tpm_ftpm_tee` driver
binds a TPM chip to a trusted application, so a copy of it carrying this
UUID presents the fwTPM as `/dev/tpm1` and `/dev/tpmrm1`, alongside the
vendor part on `/dev/tpm0`. `tpm2-tools` and anything else speaking the
kernel TPM interface then work unmodified.

The driver is GPL-2.0 and is not vendored here. `kernel/tpm_wolftpm_tee.patch`
is a diff against `drivers/char/tpm/tpm_ftpm_tee.c` from Linux v5.15. Build it
out of tree on the board, which has the kernel headers:

```sh
mkdir -p ~/wolftpm-drv && cd ~/wolftpm-drv
# from a Linux v5.15 source tree, or fetch the three files directly
cp <linux>/drivers/char/tpm/tpm_ftpm_tee.c tpm_wolftpm_tee.c
cp <linux>/drivers/char/tpm/tpm_ftpm_tee.h tpm.h .
patch -p1 < <this port>/kernel/tpm_wolftpm_tee.patch
printf 'obj-m += tpm_wolftpm_tee.o\n' > Makefile
make -C /lib/modules/$(uname -r)/build M=$PWD modules
sudo insmod ./tpm_wolftpm_tee.ko
```

The patch does three things: points both UUID sites at this trusted
application, renames the driver and module so they coexist with the vendor
driver rather than fighting over its device tree node, and registers the
platform device itself, since this trusted application has no device tree
node of its own.

What works through the kernel path, confirmed with `tpm2-tools`:

| Command | `/dev/tpmrm1` | `/dev/tpm1` |
|---------|---------------|-------------|
| `tpm2_getcap properties-fixed` | pass, reports `WOLF` at revision 1.85 | pass |
| `tpm2_getrandom`, `tpm2_pcrread`, `tpm2_selftest` | pass | pass |
| `tpm2_createprimary` | **fails** | pass |

Creating a transient object through the resource manager fails because
`tpm2-space` flushes the handle afterwards and the fwTPM answers
`TPM_RC_HANDLE` (139). The raw device has no resource manager and is
unaffected. That is fwTPM behaviour rather than anything about OP-TEE, so the
raw device is the working path until it is addressed.

Two traps if you try this:

- Do not add `TA_FLAG_DEVICE_ENUM_TEE_STORAGE_PRIVATE` to get onto the Linux
  TEE bus. The devkit defines it but the OP-TEE core in L4T R36.4.4 is older
  and rejects the unknown flag, after which the trusted application stops
  loading at all and every session returns `TEEC_ERROR_BAD_FORMAT`.
- The vendor fTPM does not reach Linux over the TEE bus either. It is a
  platform device from a `microsoft,ftpm` device tree node, which is why the
  patch registers a platform device rather than relying on enumeration.

## Limits

- The trusted application has its own UUID and **cannot appear as
  `/dev/tpm0`**. The vendor fTPM owns that UUID as an early trusted
  application compiled into `tos.img`, and an early one always beats a
  filesystem one. Use the `WOLFTPM_OPTEE` transport, not `tpm_ftpm_tee`.
- The vendor fTPM keeps working throughout; the two are independent TPMs on
  one SoC with separate state.
- A firmware TPM shares the SoC with the software it attests. Where the threat
  model includes physical possession, a discrete part remains a different
  proposition.
- Single instance by design: one instance is one TPM, since a second would be
  a second TPM sharing one NV journal.
