# Source lists shared by the native dry run and the OP-TEE trusted application.
# Paths are relative to the wolfssl and wolftpm trees respectively.

# wolfCrypt surface the fwTPM command set reaches: RSA and ECC (with key
# generation), AES-CFB, HMAC/HKDF, SHA-1/2/3, ASN.1 encode and decode, and
# ML-DSA / ML-KEM.
WC_SRCS = \
	wolfcrypt/src/aes.c \
	wolfcrypt/src/asn.c \
	wolfcrypt/src/coding.c \
	wolfcrypt/src/cpuid.c \
	wolfcrypt/src/ecc.c \
	wolfcrypt/src/error.c \
	wolfcrypt/src/hash.c \
	wolfcrypt/src/hmac.c \
	wolfcrypt/src/kdf.c \
	wolfcrypt/src/logging.c \
	wolfcrypt/src/memory.c \
	wolfcrypt/src/random.c \
	wolfcrypt/src/rsa.c \
	wolfcrypt/src/sha.c \
	wolfcrypt/src/sha256.c \
	wolfcrypt/src/sha3.c \
	wolfcrypt/src/sha512.c \
	wolfcrypt/src/sp_int.c \
	wolfcrypt/src/wc_encrypt.c \
	wolfcrypt/src/wc_mldsa.c \
	wolfcrypt/src/wc_mlkem.c \
	wolfcrypt/src/wc_mlkem_poly.c \
	wolfcrypt/src/wc_port.c \
	wolfcrypt/src/wolfmath.c

# Trusted application only: the aarch64 assembly. The native dry run builds
# for x86, so it takes WC_SRCS alone. Each file self-gates on the macros
# user_settings.h defines only for __aarch64__, so none of this reaches a
# build for another architecture.
#
# sp_arm64.c is the single-precision RSA and ECC. The armv8-* files are the
# ARMv8 crypto extensions for AES and SHA-2, the ML-KEM NTT and the Keccak
# permutation, in their inline-assembly form (WOLFSSL_ARMASM_INLINE), which
# avoids handing .S files to the OP-TEE build.
#
# armv8-sha3-asm_c.c holds both BlockSha3_crypto, which needs FEAT_SHA3, and
# BlockSha3_base, which does not. Only the latter is built here, because
# user_settings.h leaves WOLFSSL_ARMASM_CRYPTO_SHA3 undefined, and it is what
# accelerates SHAKE and so ML-DSA and ML-KEM.
WC_SRCS_TA = \
	wolfcrypt/src/sp_arm64.c \
	wolfcrypt/src/port/arm/armv8-aes-asm_c.c \
	wolfcrypt/src/port/arm/armv8-sha256-asm_c.c \
	wolfcrypt/src/port/arm/armv8-sha512-asm_c.c \
	wolfcrypt/src/port/arm/armv8-sha3-asm_c.c \
	wolfcrypt/src/port/arm/armv8-mlkem-asm_c.c

# The fwTPM minus every transport. fwtpm_io.c, fwtpm_tis.c, fwtpm_tis_shm.c
# and fwtpm_main.c are deliberately absent: both server loops block, which a
# trusted application cannot do, and TA_InvokeCommandEntryPoint is the
# transport instead.
FWTPM_SRCS = \
	src/fwtpm/fwtpm.c \
	src/fwtpm/fwtpm_command.c \
	src/fwtpm/fwtpm_crypto.c \
	src/fwtpm/fwtpm_nv.c \
	src/tpm2_util.c \
	src/tpm2_packet.c \
	src/tpm2_crypto.c \
	src/tpm2_param_enc.c
