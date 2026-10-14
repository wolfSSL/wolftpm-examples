# Patches

## wolfssl-hash-mldsa-tls.patch

Adds TLS 1.3 signature algorithms for the pre-hash form of ML-DSA, taken from
the private-use range RFC 8446 reserves for exactly this: `0xFE01`, `0xFE02`
and `0xFE03` for HashML-DSA-44, 65 and 87, with SHA-256 as the pre-hash at
every level.

The PQ TLS tab needs it because the TPM on the board signs HashML-DSA and not
pure ML-DSA, and no registered TLS signature algorithm names the pre-hash
form. Nothing else in the demo needs it.

Three things to know before using it anywhere else:

- **It is not interoperable.** A peer without the patch does not recognise the
  code points and the handshake falls back or fails. Both ends of the demo's
  handshake are `board_tls`, so that is contained.
- **It changes what a patched build offers.** With
  `WOLFSSL_HASH_MLDSA_TLS` defined, the pre-hash algorithms are offered ahead
  of the pure ones for an ML-DSA certificate, so two patched peers negotiate
  the private-use code point rather than the standard one. A build without
  the define is a stock wolfSSL and refuses them.
- **It is expected to be temporary.** Production SLB9678 firmware is expected
  to support pure ML-DSA, which the registered algorithms already cover, and
  then the patch can go.

Apply it to wolfSSL master and build with the define:

```
cd wolfssl
git apply /path/to/wolfssl-hash-mldsa-tls.patch
./autogen.sh
./configure --prefix=$HOME/wolfssl-pqtls \
            --enable-cryptocb --enable-keygen --enable-experimental \
            --enable-mldsa --enable-mlkem --enable-tls-mlkem-standalone \
            --enable-certgen --enable-certreq --enable-certext \
            --disable-sys-ca-certs --enable-harden \
            CFLAGS="-DWOLFSSL_HASH_MLDSA_TLS"
make && make install
```

On Windows under MSYS2 add `LIBS="-lws2_32"`.

## wolfssl-mldsa-weak-prehash.patch

Makes `WOLFSSL_MLDSA_ALLOW_WEAK_PREHASH` mean something in wolfSSL.

FIPS 204 section 5.4 requires a HashML-DSA pre-hash to carry at least as much
collision strength as the parameter set: SHA-384 or better for ML-DSA-65,
SHA-512 for 87. wolfSSL enforces that in `mldsa_check_hash_strength()`. The
SLB9678 running firmware 45.91 signs SHA-256 and refuses SHA-384 and SHA-512
with `TPM_RC_SIZE`, so with parameter set 87 the part and the rule cannot both
be satisfied, and an unpatched wolfSSL returns `BAD_FUNC_ARG` rather than
verifying a signature the TPM just produced.

wolfTPM names the compatibility option in pull request 627 and gates its own
firmware TPM on it, but wolfSSL has no such option, so this patch adds it.
With `WOLFSSL_MLDSA_ALLOW_WEAK_PREHASH` defined the strength comparison is
skipped and any hash the function recognises is accepted; an unknown hash is
still refused. A build with it is not FIPS 204 compliant, so it is off by
default. The same change is proposed in
[wolfSSL pull request 11736](https://github.com/wolfSSL/wolfssl/pull/11736);
the patch is here so the demo builds against a wolfSSL without it.

One patch covers both directions. `mldsa_check_hash_strength()` has exactly two
callers, `mldsa_sign_ctx_hash_with_seed()` and `mldsa_verify_ctx_hash()`, so
signing and verification are both affected and no separate sign-side change is
needed.

Needed only on a wolfSSL new enough to have the check. The revision `BUILD.md`
pins predates it, so there the patch neither applies nor is needed.

```
cd wolfssl
git apply /path/to/wolfssl-mldsa-weak-prehash.patch
```

Then build with `-DWOLFSSL_MLDSA_ALLOW_WEAK_PREHASH`, which
`user_settings_tpm.h` already defines for the firmware.

Like the other patch, this one goes away once firmware supports a pre-hash
strong enough for the parameter set, or pure ML-DSA.

`host/README.md` covers the rest of the PQ TLS tab's setup.
