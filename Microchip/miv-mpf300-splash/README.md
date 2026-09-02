# wolfTPM fwTPM on PolarFire MPF300 Splash Kit (Mi-V RV32 soft core)

A firmware TPM (fwTPM) - a software TPM 2.0 engine backed by wolfCrypt - running on a soft **MIV_RV32** RISC-V core instantiated in the fabric of a Microchip PolarFire **MPF300 Splash Kit**. The MPF300 is a pure FPGA (`MPF300T-1FCG484E`) with no hardened CPU, so a soft core is built into the fabric first, then bare-metal firmware runs on it.

The example is built up in independently verifiable stages, all running and verified on MPF300 Splash Kit hardware:

- **Hello-world** (`firmware/hello/`): a CoreUARTapb banner and LED heartbeat, proving the soft-core platform.
- **wolfCrypt** (`firmware/wolfcrypt-test/`): `wolfcrypt_test` and `benchmark` from 512 KB LSRAM, covering the algorithm families the fwTPM uses (RSA, ECC, AES, SHA-2/3, HMAC, CMAC) plus post-quantum ML-DSA / ML-KEM, with a performance pass. It is a perf/coverage benchmark, not a bit-exact mirror of the fwTPM config (see `wolfcrypt-test/user_settings.h`).
- **fwTPM** (`firmware/fwtpm/`): a TPM 2.0 server driven over UART, seeding wolfCrypt's Hash-DRBG from the PolarFire System Controller Nonce Service (NRBG output), with persistent NV in on-die secure NVM (sNVM) - both reached through one `CoreSysServices_PF` block.

## wolfCrypt on the Mi-V

wolfCrypt 5.9.2 runs on the Mi-V RV32 soft core with the full 512 KB of LSRAM as
execution memory. Reaching this required FPGA-side changes beyond the stock
tutorial design (all scripted via Libero `configure_core`, see `fpga/README.md`):
the on-chip SRAM was enlarged from 64 KB to 512 KB; the MIV_RV32 AHB memory
window was widened from its default 16 KB to 512 KB (`AHB_END_ADDR = 0x8007FFFF`)
so the CPU can address all of it; and for performance the MIV_RV32
pipeline-forwarding (`FWD_REGS`) and instruction cache (`ICACHE_EN`) were enabled.
Firmware is `-march=rv32imc`, portable-C math (SP small, 32-bit); see the
performance notes below for the toolchain and `-O2` choice.

Console output (the CoreUARTapb console, 115200 8N1) - all cases pass:

```
--- wolfcrypt_test ---
 wolfSSL version 5.9.2
macro/error/MEMORY/base64/asn        test passed!
SHA/SHA-256/384/512/512-224/512-256  test passed!
SHA-3 / SHAKE128 / SHAKE256          test passed!
RANDOM/Hash                          test passed!
HMAC-SHA/256/384/512/SHA3/KDF/PRF    test passed!
GMAC                                 test passed!
AES/192/256/CBC/CTR/GCM              test passed!
CMAC                                 test passed!
RSA                                  test passed!
ECC / ECC buffer                     test passed!
ML-KEM (Kyber) / ML-DSA (Dilithium)  test passed!
```

(ECC P-384 initially failed `ecc_test_curve_size` with `-234`; the fix was to
add `WOLFSSL_SP_384` to `user_settings.h` so the SP-math P-384 curve code is
built - needed anyway for the fwTPM.)

Benchmark of the default optimized build (xPack GCC 15.2.0 `-O2 -flto` with the
GCM 4-bit-table GHASH, **plus** the MIV_RV32 pipeline-forwarding + instruction-
cache options enabled, portable C, 80 MHz Mi-V). The `vs -Os` column is the total
gain over the stock config (SoftConsole GCC 8.3.0 `-Os`, forwarding/cache off,
`GCM_SMALL`) - see the performance notes below:

```
wolfCrypt Benchmark (block bytes 1024, min 1.0 sec each)                vs -Os
RNG SHA-256 DRBG           148.864 KiB/s                                +40%
AES-128-CBC-enc             12.058 KiB/s                                +51%
AES-256-CBC-enc              8.650 KiB/s                                +51%
AES-128-GCM-enc             11.677 KiB/s                                +54% (4-bit table)
AES-128-CTR                 12.051 KiB/s                                +51%
GMAC Table 4-bit           348.672 KiB/s                                (4-bit table)
SHA                        918.455 KiB/s                                +14%
SHA-256                    425.929 KiB/s                                +37%
SHA-512                    242.844 KiB/s                                +11%
SHA3-256                   199.275 KiB/s                                (mixed)
SHAKE256                   199.242 KiB/s
HMAC-SHA256                422.285 KiB/s                                +37%
RSA 2048  public             4.372 ops/sec  (avg   229 ms)              +25%
RSA 2048 private             0.073 ops/sec  (avg 13644 ms)              +28%
ECC   SECP256R1 key gen      1.270 ops/sec  (avg   788 ms)              +46%
ECDHE SECP256R1 agree        1.271 ops/sec  (avg   787 ms)              +46%
ECDSA SECP256R1 sign         1.129 ops/sec  (avg   886 ms)              +43%
ECDSA SECP256R1 verify       0.625 ops/sec  (avg  1599 ms)              +44%
ML-KEM 768   key gen        21.616 ops/sec  (avg    46 ms)              post-quantum
ML-KEM 768   encap          17.619 ops/sec  (avg    57 ms)              post-quantum
ML-KEM 768   decap          13.305 ops/sec  (avg    75 ms)              post-quantum
ML-DSA 44    key gen         9.376 ops/sec  (avg   107 ms)              post-quantum
ML-DSA 44    sign            1.528 ops/sec  (avg   655 ms)              post-quantum
ML-DSA 44    verify          8.487 ops/sec  (avg   118 ms)              post-quantum
ML-DSA 65    key gen         5.539 ops/sec  (avg   181 ms)              post-quantum
ML-DSA 65    sign            0.942 ops/sec  (avg  1062 ms)              post-quantum
ML-DSA 65    verify          5.225 ops/sec  (avg   191 ms)              post-quantum
Benchmark complete
```

The standout is post-quantum vs classical public-key on this soft core: ML-KEM-768
key-gen (21.6 ops/sec) is ~17x an ECC P-256 key-gen (1.27) and ML-DSA-44 verify
(8.5) ~14x an ECDSA verify (0.63), because ECC runs slow single-precision
big-integer math while ML-DSA/ML-KEM lean on SHA-3/SHAKE and small polynomial
arithmetic. On the classical side three optimization levers stack: the modern-GCC
`-O2` toolchain (~+7-25%); the soft-core `FWD_REGS` (pipeline forwarding) +
`ICACHE_EN` (instruction cache), the larger share - e.g. AES-128-CBC
7.99 -> 8.84 (`-O2`) -> 12.05 KiB/s (`+FWD/ICACHE`); and the `GCM_TABLE_4BIT`
GHASH replacing the tiny `GCM_SMALL` bit-serial loop, lifting GMAC ~17x
(20.7 -> 349 KiB/s) and AES-GCM ~+54%. `-flto` mainly buys ~18 KB of code size,
which is what makes ML-DSA/ML-KEM plus the GCM tables fit in 512 KB LSRAM. One
caveat: `-O2` slightly *slows* the bulky ML-DSA/ML-KEM code (its larger footprint
pressures the small Mi-V instruction cache - e.g. ML-DSA-44 sign is ~2.8 ops/sec
at `-Os` vs 1.5 at `-O2`); `-O2` is kept as the default because it is a clear net
win for the classical suite that dominates the benchmark.

### Performance notes (what helps, what doesn't)

All levers were evaluated on hardware. The headline result: **`-O2` with a modern
GCC gives ~7-25%** over the SoftConsole `-Os` baseline, and it is the config the
numbers above use. The RISC-V crypto assembly does *not* apply to this core.

- **Toolchain + `-O2` (the win).** `-O2` speeds up everything, but the bundled
  SoftConsole **GCC 8.3.0 miscompiles** the SP-math P-384 code at `-O2` (the
  `asn`/ECC tests fail with the `-234` key-size error; `-fno-strict-aliasing`
  does not help). Bisected to the compiler: `-Os` is correct on 8.3.0, `-O2` is
  not. The fix is a newer GCC. The **xPack GNU RISC-V Embedded GCC 15.2.0**
  (a prebuilt multilib `riscv-none-elf` toolchain) compiles `-O2` correctly and
  yields the gains above. Notes for using it: pass
  `-march=rv32imc_zicsr_zifencei` (GCC >= 12 split `zicsr`/`zifencei` out of the
  base ISA), and add a prototype for the `CUSTOM_RAND_GENERATE` function (GCC 15
  makes implicit declarations an error). The Ubuntu `riscv64-unknown-elf-gcc`
  13.2 has no newlib, and a `--with-arch=rv32gc` single-config toolchain won't
  have the soft-float `double` libgcc the benchmark's stats need - use a real
  multilib newlib toolchain (xPack, or a `riscv-gnu-toolchain` newlib multilib
  build).
- **RISC-V scalar-crypto / bit-manip / vector ASM** (`--enable-riscv-asm`
  `zkned`/`zbb`/`zba`/`zbkb`/`zv*`): **not usable.** This MIV_RV32 is `rv32imc`
  (`misa=0x42001104` = I+M+C only, confirmed on-target) and does not implement
  the Zk*/Zb*/V extensions those paths require, and the MIV_RV32 core cannot add
  them (its only optional ISA extension is `F`, single-precision float, which
  integer crypto does not use). That wolfSSL asm is also RV64-only.
- **`WOLFSSL_SP_RISCV32`** (generic single-precision asm in `sp_int.c`):
  **no effect.** RSA and ECC run through the curve/size-specific `sp_c32.c`, not
  the generic `sp_int.c` path, so defining it produced a byte-identical binary.
- **`WOLFSSL_SP_384`** (applied): a correctness fix so the ECC P-384 test builds
  and passes (needed for the fwTPM); it does not change the benchmark numbers.
- **`GCM_TABLE_4BIT` + `-flto` (applied, default).** Swapping the tiny
  `GCM_SMALL` bit-serial GHASH for the 4-bit-table version (256 B/key) is the one
  large wolfCrypt-config win that fits: GMAC ~17x, AES-GCM ~+54%, all verified on
  hardware. `-flto` trims ~18 KB, which is exactly what frees the room for those
  tables. `-O3` overflows 512 KB (aggressive inlining) and is not used.
- **`WOLFSSL_SP_SMALL` stays on (size-bound, not a choice).** The fast
  (non-small) SP path would speed up RSA/ECC substantially, but it needs ~77 KB
  more than the 512 KB LSRAM has once RSA-2048 + P-256 + P-384 are all present.
  So RSA-2048 private (~13.6 s) and ECC stay compute-bound here; the practical
  lever for public-key on this core is post-quantum (ML-DSA verify is ~14x an
  ECDSA verify - see the benchmark above).
- **Soft-core microarchitecture (the biggest win).** The stock MIV_RV32 config
  ships with pipeline register-forwarding off (`FWD_REGS=0`) and the instruction
  cache off (`ICACHE_EN=0`) to minimize fabric. Turning both on
  (`configure_core -component_name {MIV_RV32_C0} -params {"FWD_REGS:true"
  "ICACHE_EN:true"}` - note these are booleans, `:true` not `:1`) raises the
  core's IPC substantially: on top of `-O2` it takes AES to +51% and ECC P-256
  to +46% over the stock `-Os` build. Cost is ~1,300 extra LUTs and one LSRAM
  block (the design is still ~3% of the MPF300T). The multiplier already uses the
  hardware MACC/DSP blocks (`NO_MACC_BLK=0`), so there is nothing to gain there;
  `F_EXT` (single-precision float) is the core's only other optional extension and
  integer crypto does not use it. (With `ICACHE_EN`, JTAG reloads need a reset -
  the `monitor reset halt` in the load flow invalidates the cache, so no stale
  instructions.)

### Post-quantum crypto (ML-DSA / ML-KEM)

ML-DSA (Dilithium) and ML-KEM (Kyber) are built in by default - no flag - and are
in the test and benchmark above; this example targets a PQC-capable TPM. They are
trimmed to ML-DSA-44/-65 and ML-KEM-768 (ML-DSA-87 and ML-KEM-512/1024 are omitted
only to fit 512 KB LSRAM beside RSA/ECC; the fwTPM PQC build exercises all three
ML-DSA levels), using the small-memory sign/verify/make-key paths so every enabled
level runs on this core. This bare-core build uses the placeholder counter RNG -
it measures the crypto itself, not the fwTPM's System-Controller entropy.

## fwTPM on the Mi-V

The wolfTPM firmware TPM (`firmware/fwtpm/`) runs on the Mi-V soft core - the
full TPM 2.0 command engine backed by wolfCrypt, in 490 KB of the 512 KB LSRAM.
It registers an sNVM-backed persistent NV HAL (RAM-shadowed) and a CoreTimer clock HAL, runs `FWTPM_Init`,
then serves TPM2 commands over the CoreUARTapb console using the raw swtpm /
Microsoft-simulator framing (same as the STM32H5 port), so a host can drive it.

On-device self-test and host client (`host-client/fwtpm_uart_test.py`):

```
fwTPM 0.1.0 initialized (CTX 96168 bytes)
TPM2_Startup            rc=0x00000000 OK
TPM2_GetRandom          rc=0x00000000 OK bytes=454571EA23AF...
---- driven from a host over UART ----
TPM2_GetCapability MAN  manufacturer=b'WOLF'
TPM2_GetCapability FW1/FW2  firmware version 0x0 / 0x1
TPM2_PCR_Read PCR0      rc=0  (PCR0 = all-zero SHA-256, initial state)
TPM2_GetRandom 16       74f0aa3afc761b30075d608af95d3e88
```

### Entropy source (System Controller Nonce Service)

The TPM's randomness is seeded from the PolarFire System Controller **Nonce
Service** (NRBG output), reached from the fabric through a
**`CoreSysServices_PF`** block on APB slot 3 (`0x70003000`). The Nonce Service
returns 32 bytes of hardware entropy, which
`firmware/fwtpm/fwtpm_rng_sysserv.c` feeds to wolfCrypt's Hash-DRBG as seed
material (`CUSTOM_RAND_GENERATE_SEED`); the DRBG then produces the TPM's random
stream and reseeds from the Nonce Service on schedule.

Verified on hardware - the boot self-test reports the live nonce, and the seed
is non-deterministic across resets (two cold boots):

```
Entropy: System Controller nonce OK: 544b0d99f59f6f89...   (boot 1)
Entropy: System Controller nonce OK: a2eed7bc639418cc...   (boot 2)
TPM2_GetRandom 16      7DFE2E2E511F802B7E705D0E18816CED  (boot 1)
TPM2_GetRandom 16      7FE637F2CE629985714059F6CACB6EF8  (boot 2)
```

**Part-variant note on the entropy source.** The Splash Kit uses the non-S
**MPF300T**, whose Nonce Service NRBG is an iRNG seeded from the SRAM-PUF. Its
entropy is believed good, but it is **not SP800-90A/B certified** and no entropy
characterization data is available for it. The security-enabled **MPF300TS**
("-S") variant instead has a certified TRNG. In this example the nonce is used
only as **seed** material for wolfCrypt's SP800-90A Hash-DRBG - the DRBG, not the
raw Nonce Service, produces all TPM randomness and reseeds on schedule - so the
raw source's lack of certification is mitigated by the certified-design DRBG in
front of it.

### Persistent NV in on-die sNVM

TPM NV is persistent, using the PolarFire
**secure NVM (sNVM)** - on-die non-volatile storage reached through the same
`CoreSysServices_PF` mailbox as the Nonce Service (`SYS_secure_nvm_write`/`read` System
Services). No extra FPGA IP, pins, or bitstream change is needed: NV is
firmware-only and rides the same bitstream.

`firmware/fwtpm/fwtpm_nv_snvm.c` implements the fwTPM NV HAL over a block of
sNVM pages (base page 128, 65 pages = ~16 KB), high in the 221-page sNVM array
and clear of the pages the FPGA design uses. A RAM shadow is loaded once at boot
so reads are fast; a write re-programs only the touched sNVM page(s) and commits
each page into the shadow only after its sNVM write succeeds, so the shadow never
gets ahead of flash and the sNVM write count stays low.

The NV journal is integrity-checked. The fwTPM core HMACs its journal with a key
the HAL supplies, and this build wires that key on. By default the key is device-
derived: a per-device value is generated once from the hardware NRBG and persisted
in a dedicated sNVM key page so the MAC is stable across power cycles. What that
protects depends on where the key lives. In the default plaintext build the key
sits in plaintext sNVM next to the data, so it is not secret from an attacker with
sNVM read access: the MAC catches accidental corruption and rejects an NV journal
transplanted from another device, but it does not stop rollback or tampering by an
attacker who can read sNVM (they can read the key and recompute the MAC). For
genuine tamper and rollback resistance, either enable
`EXTRA_CFLAGS=-DMIV_SNVM_NV_AUTH` (the key page is then stored as authenticated-
ciphertext under the on-die factory key) or provision a protected key from
fuses/PUF with `EXTRA_CFLAGS=-DMIV_FWTPM_NV_KEY='{...}'`. The key is acquired
fail-closed at boot: if it cannot be read the device refuses to run rather than
serve NV unauthenticated. In authenticated mode a status-2 sNVM read is an
authentication failure (not a blank page), so a tampered or unprovisioned page
also fails closed; authenticated mode therefore expects its NV and key pages to be
provisioned once before first use.

Pages use non-authenticated plaintext mode by default. For a production TPM that
needs confidentiality at rest, build with `EXTRA_CFLAGS=-DMIV_SNVM_NV_AUTH` to use
the authenticated-ciphertext service, so each page (including the integrity-key
page) is encrypted and integrity-checked with the on-die factory key plus a
unique application key (`-DMIV_SNVM_USK='{...}'`, required - there is no built-in
default). Authenticated mode additionally requires the FPGA design's security
policy to permit authenticated sNVM writes (configured in the Libero Security
Policy Manager); without that provisioning the System Controller rejects the
write with `SNVM_WRITE_NOT_PERMITTED`, so it is off by default here.

The mailbox and persistence failure paths (controller busy/timeout, blank-page
handling, partial multi-page writes, a stalled UART frame) are exercised and
validated on hardware rather than by mocked unit tests.

Note the MT25Q SPI flash on this board is wired to the PolarFire dedicated
System-Controller SPI (shared with the programming/SPI-boot path), not to
general fabric pins, so sNVM is the clean NV target here. sNVM is limited-
endurance flash - fine for demonstrating persistence, but heavy NV churn should
move to a dedicated external high-endurance flash.

Verified on hardware with a standalone sNVM boot counter (a page outside the TPM
NV region, built with `EXTRA_CFLAGS=-DMIV_SNVM_BOOTCNT_TEST` - off by default so
the shipped example does not write sNVM every boot). Reloading the firmware
reinitializes RAM but not sNVM, so the counter surviving a reload proves the
data is truly non-volatile:

```
sNVM persistence: boot counter = 0 (page blank, first boot)   (boot 1)
sNVM persistence: stored boot counter = 1 (survives power cycle)
sNVM persistence: boot counter = 1                            (boot 2, RAM wiped)
sNVM persistence: stored boot counter = 2 (survives power cycle)
```

The TPM's own NV survives too. `host-client/fwtpm_nv_persist_test.py` defines an
owner NV index and writes a marker; after a firmware reload it reads back the
same value without redefining it:

```
(run 1)  TPM2_NV_DefineSpace rc=0  TPM2_NV_Write 0xDEADBEEF  TPM2_NV_Read 0xDEADBEEF
(reload firmware - RAM wiped, sNVM retained)
(run 2)  TPM2_NV_Read rc=0  value=0xDEADBEEF   <= persisted from the previous run
```

### Post-quantum crypto (ML-DSA / ML-KEM)

Built with `EXTRA_CFLAGS=-DMIV_FWTPM_PQC`, the fwTPM becomes an ECC + post-quantum
TPM: it adds ML-DSA (signatures) and ML-KEM (key encapsulation) via wolfTPM's
v1.85 support, and drops RSA to fit the 512 KB LSRAM. The full wolfTPM example
suite drives it over the `host-client/swtpm_uart_bridge.py` bridge, which
forwards wolfTPM's swtpm socket transport (`localhost:2321`) to the device UART:

```
# device: JTAG-load the PQC firmware
make EXTRA_CFLAGS="-DMIV_FWTPM_PQC"
# host: bridge, then run any wolfTPM example against it
python3 host-client/swtpm_uart_bridge.py /dev/ttyUSBx 2321 &
cd ../../../../../wolftpm
LD_LIBRARY_PATH=<pqc-wolfssl>/lib ./examples/pqc/pqc_mssim_e2e     # ML-KEM + ML-DSA
LD_LIBRARY_PATH=<pqc-wolfssl>/lib ./examples/keygen/keygen -ecc    # ECC
```

Verified on hardware (wolfTPM examples end-to-end through the fwTPM engine):

| Operation | Result |
|-----------|--------|
| ECC (P-256) CreatePrimary + create/load key | pass |
| ML-KEM-768 CreatePrimary + Encapsulate + Decapsulate | pass (shared secrets match) |
| ML-DSA-44 / -65 / -87 CreatePrimary + Sign + Verify | pass (signatures verified) |
| PCR Extend + Read | pass |
| NV Store + Read (ECC keyblob) | pass |

All three ML-DSA security levels fit only with the sign / verify / make-key
small-memory paths enabled (set in `user_settings.h` under `MIV_FWTPM_PQC`).
ML-DSA-44/-65 run in seconds; ML-DSA-87 is functionally correct but slow and
high-variance on this core - the small-memory code recomputes rather than
caching, and ML-DSA's rejection-sampling loop is itself variable, so a single
ML-DSA-87 sign can range from a second to several minutes. ML-KEM is trimmed to
the 768 parameter set.

The default (RSA + ECC) firmware also runs the wolfTPM suite, but note RSA-2048
key generation on this software-math core is slow and high-variance (~97 s to
minutes), so ECC/PQC is the practical path for full-suite testing here.

## Hardware

- Board: PolarFire MPF300 Splash Kit, FPGA `MPF300T-1FCG484E`.
- Debug/console: on-board FT4232H providing JTAG (FlashPro) plus UART channels; the CoreUARTapb console appears as one of the enumerated `ttyUSB` devices.
- Memory: LSRAM/uSRAM fabric blocks, 2x DDR4, on-board Micron MT25Q SPI flash, sNVM (56 KB). No eMMC/SD.

## Soft-core platform (FPGA prerequisite)

Any firmware here needs a Mi-V soft core in the fabric first. See [`fpga/README.md`](fpga/README.md) for building and programming the platform. The firmware targets the Microchip "RISC-V for PolarFire" Splash Kit Mi-V reference design: a `MIV_RV32` core (rv32imc) with CoreUARTapb, CoreGPIO and CoreTimer on an APB3 bus, LSRAM at the reset vector.

Reference design memory map (see `firmware/common/miv_board.h`):

| Resource | Address | Notes |
|----------|---------|-------|
| Core clock | 80 MHz | CCC/PLL output |
| LSRAM (code/data) | `0x80000000` | 512 KB (reset vector; enlarged from the reference design's 64 KB) |
| CoreUARTapb0 | `0x70000000` | Console, 115200 8N1 |
| CoreGPIO (out) | `0x70001000` | LEDs on GPIO_0..3 |
| CoreTimer0 | `0x70002000` | Time base (see note) |
| CoreSysServices_PF | `0x70003000` | System Controller services; Nonce Service (NRBG entropy seed) (APB slot 3) |

If your Libero design differs, override the defaults in `miv_board.h` (or via `-D` in the Makefile) and the linker `ORIGIN`/`LENGTH`.

**Time base note:** the MIV_RV32 in this reference design implements neither the memory-mapped MTIME block (a load from `0x4400BFF8` faults) nor the `mcycle`/`minstret` performance counters (hardwired to 0). Timekeeping therefore uses **CoreTimer0** as a free-running down-counter (`firmware/common/miv_time.c`). The fwTPM clock HAL uses the same source.

## Layout

```
firmware/
  common/     Shared wolfSSL-authored HAL: CoreUARTapb console, CoreGPIO,
              CoreTimer clock/delays, RV32 startup, newlib retarget (printf)
  hello/      banner + LED heartbeat
  wolfcrypt-test/  wolfcrypt_test + benchmark_test wrapper
  fwtpm/      fwTPM server over UART + System-Controller Nonce Service entropy +
              persistent sNVM NV; host-client/ drives it from a PC
fpga/         How to build/program the Mi-V soft-core platform
```

The HAL is written from the Mi-V register interface (no vendor BSP is vendored into this public repo). The wolfSSL and wolfTPM sources are expected as sibling trees (`../wolfssl`, `../wolftpm`); the wolfCrypt and fwTPM Makefiles take `WOLFSSL_DIR`/`WOLFTPM_DIR` overrides.

## Build and run: hello-world

Prerequisites: a RISC-V GCC with rv32imc/ilp32 multilib. The Microchip SoftConsole toolchain works out of the box:

```
export PATH=/opt/Microchip/SoftConsole-v2022.2-RISC-V-747/riscv-unknown-elf-gcc/bin:$PATH
cd firmware/hello
make
```

This produces `miv-hello.elf` / `.hex` (entry `0x80000000`, ~5 KB, fits the stock 16 KB AHB window so it runs on the unmodified reference design). Program the FPGA with the Mi-V platform (see `fpga/README.md`), then JTAG-load the ELF and run (SoftConsole debug, or OpenOCD with a Mi-V config over the FT4232H). If OpenOCD's `fpServer` segfaults on a host with many USB serial devices, launch it via `tools/fp5-jtag.sh` (see the troubleshooting section in `fpga/README.md`). Expected console output at 115200 8N1:

```
========================================================
  wolfSSL / wolfTPM on PolarFire MPF300 Splash Kit
  Mi-V RV32 soft core - Hello World
========================================================
Core clock : 80000000 Hz
Console    : CoreUARTapb @ 0x70000000, 115200 8N1
...
heartbeat 0 (uptime 12 ms)
heartbeat 1 (uptime 1013 ms)
```

with the four LEDs walking.

## Build and run: wolfCrypt test + benchmark

The wolfCrypt and fwTPM builds need the 512 KB LSRAM platform (see
`fpga/README.md`) and the wolfSSL / wolfTPM sources as sibling trees (`../wolfssl`, `../wolftpm`, overridable with
`WOLFSSL_DIR`/`WOLFTPM_DIR`).

```
cd firmware/wolfcrypt-test
make                       # default: xPack GCC, -O2 -flto, PQC built in
                           # (put xpack .../bin on PATH)
# quick wolfcrypt_test-only smoke build on SoftConsole (no benchmark, no floats):
# make CROSS_COMPILE=riscv64-unknown-elf- OPT=-Os ARCH=rv32imc \
#      EXTRA_CFLAGS=-DNO_CRYPT_BENCHMARK
```

JTAG-load `miv-wolfcrypt-test.elf`; the console prints `wolfcrypt_test` results (all
PASS) followed by the `benchmark_test` table.

> **Toolchain note (float printf).** The `benchmark_test` prints its rate stats
> with `%f`, which pulls in newlib's floating-point `printf`. The bundled
> **SoftConsole GCC 8.3.0 was observed to crash in that `_printf_float` path**
> (the firmware traps at the banner before any output). Build the benchmark with
> the **xPack `riscv-none-elf` GCC** (the same modern toolchain used for the
> numbers in this README) to avoid it; `wolfcrypt_test` alone, and the fwTPM
> (which prints no floats), are unaffected and build fine on SoftConsole.

## Build and run: fwTPM

```
cd firmware/fwtpm
make                       # default: plaintext sNVM NV, Nonce Service entropy, no boot-counter
```

JTAG-load `miv-fwtpm.elf` (via `tools/fp5-jtag.sh` if `fpServer` segfaults - see `fpga/README.md`). The banner reports the NV backend and a live Nonce
Service seed, the self-test runs `TPM2_Startup` + `TPM2_GetRandom`, and the device then
serves TPM 2.0 over the UART. Drive it from the PC with the host clients:

```
cd host-client
python3 fwtpm_uart_test.py         /dev/ttyUSBx   # caps/PCR/random
python3 fwtpm_nv_persist_test.py   /dev/ttyUSBx   # NV persistence
```

Optional build flags are documented in `firmware/fwtpm/Makefile` (`EXTRA_CFLAGS`):
authenticated-ciphertext NV (`-DMIV_SNVM_NV_AUTH`) and the sNVM boot-counter
self-test (`-DMIV_SNVM_BOOTCNT_TEST`). Note that `-DMIV_FWTPM_PQC` and
`-DMIV_SNVM_NV_AUTH` change the sNVM NV region geometry, so switching either flag
discards any TPM NV state a differently-configured image had stored.

## Roadmap

All of the above is delivered and verified on hardware. Possible future work:

- **Authenticated NV by default.** The `-DMIV_SNVM_NV_AUTH` build already
  encrypts and integrity-checks each sNVM page, but the FPGA design's security
  policy must permit authenticated sNVM writes (Libero Security Policy Manager).
  Provision that and make it the default.
- **High-endurance NV.** For write-heavy workloads, add a `CoreSPI` master to a
  dedicated SPI flash and back it with the fwTPM append-only NV journal. Note the
  on-board MT25Q is wired to the shared System-Controller SPI (see the sNVM note above),
  so this needs its own flash or careful bus sharing.
- **Measured/secure boot** with wolfBoot, and upstreaming the Mi-V bare-metal
  `user_settings.h` and the fwTPM port notes to wolfSSL / wolfTPM.

## Support

For questions email [support@wolfssl.com](mailto:support@wolfssl.com).
