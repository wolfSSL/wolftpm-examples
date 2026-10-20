/* board_tls.c
 *
 * A TLS 1.3 handshake whose server identity lives in the TPM fitted to the
 * board: ML-KEM key agreement, an ML-DSA-87 certificate, and a
 * CertificateVerify signature produced by the part.
 *
 * The TLS stack runs here on the host. Only the one operation that needs the
 * private key is delegated, through a wolfCrypt crypto callback that asks the
 * board over the demo's existing serial console. This program never opens that
 * port: the Python server owns the single serial handle for its whole life,
 * because Windows releases one well after close() returns. So the request goes
 * out on stdout and the answer comes back on stdin.
 *
 *   SIGNREQ <hex digest>      ->  stdout
 *   SIGNRSP <hex signature>   <-  stdin
 *   SIGNERR <reason>          <-  stdin
 *
 * Needs a wolfSSL built with -DWOLFSSL_HASH_MLDSA_TLS. TLS 1.3 defines no
 * code point for the pre-hash form of ML-DSA, and the part signs only the
 * pre-hash form, so the handshake uses a private use code point from the
 * RFC 8446 range. It is not interoperable with anything else, and production
 * firmware supporting pure ML-DSA would remove the need for it.
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfTPM.
 *
 * wolfTPM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTPM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/wc_mldsa.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/cryptocb.h>
#include <wolfssl/wolfcrypt/random.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <direct.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SOCK_T SOCKET
    #define SOCK_BAD INVALID_SOCKET
    #define CLOSESOCK closesocket
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCK_T int
    #define SOCK_BAD (-1)
    #define CLOSESOCK close
#endif

#define BOARD_DEV_ID        0x42424f44  /* any value the callback owns */
#define CA_CERT_FILE        "certs/board-ca-cert.der"
#define LEAF_CERT_FILE      "certs/board-leaf-cert.der"
#define MAX_CERT            8192
#define MAX_SIG             8192
#define MLDSA_DIGEST_SZ     32          /* the only size the part signs */

static int signCount;

/* ------------------------------------------------------------------ hex */

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Both nibbles are checked rather than handed to sscanf("%2x"), whose field
 * width is a maximum and not a minimum: "ag" converts as 0x0a and reports
 * success, so a corrupted digest would be signed instead of refused. */
static int unhex(const char* in, unsigned char* out, int maxOut)
{
    int n = (int)strlen(in);
    int i;

    if ((n & 1) != 0 || n / 2 > maxOut)
        return -1;
    for (i = 0; i < n; i += 2) {
        int hi = nibble(in[i]);
        int lo = nibble(in[i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    return n / 2;
}

static void puthex(const unsigned char* b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        printf("%02x", b[i]);
}

/* ------------------------------------------------- the delegated signer */

/* Ask whoever is driving this process to have the board sign the digest.
 * Returns the signature length, or negative. */
static int ask_board(const unsigned char* digest, int digestSz,
    unsigned char* sig, int sigMax)
{
    static char line[MAX_SIG * 2 + 64];

    if (digestSz != MLDSA_DIGEST_SZ) {
        /* The part answers TPM_RC_SIZE for anything else, and wolfTPM pads a
         * short digest silently, which would sign the wrong value. */
        fprintf(stderr, "board_tls: digest is %d bytes, expected %d\n",
                digestSz, MLDSA_DIGEST_SZ);
        return -1;
    }

    printf("SIGNREQ ");
    puthex(digest, digestSz);
    printf("\n");
    fflush(stdout);

    if (fgets(line, (int)sizeof(line), stdin) == NULL) {
        fprintf(stderr, "board_tls: no answer to SIGNREQ\n");
        return -1;
    }
    line[strcspn(line, "\r\n")] = '\0';

    if (strncmp(line, "SIGNRSP ", 8) == 0) {
        int n = unhex(line + 8, sig, sigMax);
        if (n <= 0) {
            fprintf(stderr, "board_tls: malformed SIGNRSP\n");
            return -1;
        }
        signCount++;
        return n;
    }
    if (strncmp(line, "SIGNERR", 7) == 0) {
        fprintf(stderr, "board_tls: board reported %s\n", line);
        return -1;
    }
    fprintf(stderr, "board_tls: unexpected reply '%s'\n", line);
    return -1;
}

/* Only the ML-DSA sign is ours. Everything else goes back to software, which
 * is what keeps the key exchange and all the hashing on the host. */
static int board_crypto_cb(int devId, wc_CryptoInfo* info, void* ctx)
{
    (void)devId;
    (void)ctx;

    if (info->algo_type == WC_ALGO_TYPE_PK &&
            info->pk.type == WC_PK_TYPE_PQC_SIG_SIGN) {
        int n;

        /* wolfSSL hands over the pre-hash digest and names the hash it used.
         * The part applies the FIPS 204 pre-hash domain separation itself, so
         * the two have to agree; SHA-256 is the only one it implements. */
        if (info->pk.pqc_sign.preHashType != WC_HASH_TYPE_SHA256) {
            fprintf(stderr, "board_tls: pre-hash %u not supported by the part\n",
                    (unsigned)info->pk.pqc_sign.preHashType);
            return CRYPTOCB_UNAVAILABLE;
        }
        n = ask_board(info->pk.pqc_sign.in, (int)info->pk.pqc_sign.inlen,
                info->pk.pqc_sign.out, (int)*info->pk.pqc_sign.outlen);
        if (n < 0)
            return WC_HW_E;
        *info->pk.pqc_sign.outlen = (word32)n;
        return 0;
    }

    return CRYPTOCB_UNAVAILABLE;
}

/* ------------------------------------------------------------ cert files */

static int read_file(const char* path, unsigned char* buf, int maxSz)
{
    FILE* f = fopen(path, "rb");
    int n;

    if (f == NULL)
        return -1;
    n = (int)fread(buf, 1, (size_t)maxSz, f);
    fclose(f);
    return n;
}

/* The certificates go in a subdirectory, which will not exist on a machine
 * that has only ever unpacked the archive. */
static void ensure_certs_dir(void)
{
#ifdef _WIN32
    _mkdir("certs");
#else
    mkdir("certs", 0755);
#endif
}

static int write_file(const char* path, const unsigned char* buf, int sz)
{
    FILE* f = fopen(path, "wb");

    if (f == NULL) {
        fprintf(stderr, "board_tls: cannot write %s\n", path);
        return -1;
    }
    /* A short write, or a flush that fails at close, leaves a truncated
     * certificate the caller would otherwise announce as written. */
    if (fwrite(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "board_tls: short write on %s\n", path);
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "board_tls: cannot close %s\n", path);
        return -1;
    }
    return 0;
}

/* An ephemeral ML-DSA CA signs a leaf whose subject key is the board's. The
 * board signs nothing here, so the delegation stays confined to
 * CertificateVerify. The CA exists only for this run. */
static int gen_cert(const char* pubHex)
{
    static unsigned char boardPub[MAX_CERT];
    static unsigned char caDer[MAX_CERT];
    static unsigned char leafDer[MAX_CERT];
    WC_RNG rng;
    wc_MlDsaKey caKey;
    wc_MlDsaKey boardKey;
    Cert caCert, leaf;
    int boardPubSz, caSz, leafSz, ret;

    boardPubSz = unhex(pubHex, boardPub, (int)sizeof(boardPub));
    if (boardPubSz <= 0) {
        fprintf(stderr, "board_tls: bad -pub hex\n");
        return -1;
    }

    ensure_certs_dir();

    if (wc_InitRng(&rng) != 0)
        return -1;

    /* CA */
    if (wc_MlDsaKey_Init(&caKey, NULL, INVALID_DEVID) != 0) return -1;
    if (wc_MlDsaKey_SetParams(&caKey, WC_ML_DSA_87) != 0) return -1;
    if (wc_MlDsaKey_MakeKey(&caKey, &rng) != 0) return -1;

    if (wc_InitCert(&caCert) != 0) return -1;
    strncpy(caCert.subject.country, "US", CTC_NAME_SIZE);
    strncpy(caCert.subject.org, "wolfSSL", CTC_NAME_SIZE);
    strncpy(caCert.subject.commonName, "Board Demo CA", CTC_NAME_SIZE);
    caCert.isCA = 1;
    caCert.selfSigned = 1;
    caCert.sigType = CTC_ML_DSA_87;
    caCert.daysValid = 30;
    ret = wc_MakeCert_ex(&caCert, caDer, (word32)sizeof(caDer),
            ML_DSA_87_TYPE, &caKey, &rng);
    if (ret < 0) { fprintf(stderr, "MakeCert(ca) %d\n", ret); return -1; }
    caSz = wc_SignCert_ex(caCert.bodySz, caCert.sigType, caDer,
            (word32)sizeof(caDer), ML_DSA_87_TYPE, &caKey, &rng);
    if (caSz < 0) { fprintf(stderr, "SignCert(ca) %d\n", caSz); return -1; }

    /* Leaf: the board's public key as the subject, the CA as the issuer. */
    if (wc_MlDsaKey_Init(&boardKey, NULL, INVALID_DEVID) != 0) return -1;
    if (wc_MlDsaKey_SetParams(&boardKey, WC_ML_DSA_87) != 0) return -1;
    ret = wc_MlDsaKey_ImportPubRaw(&boardKey, boardPub, (word32)boardPubSz);
    if (ret != 0) { fprintf(stderr, "ImportPubRaw %d\n", ret); return -1; }

    if (wc_InitCert(&leaf) != 0) return -1;
    strncpy(leaf.subject.country, "US", CTC_NAME_SIZE);
    strncpy(leaf.subject.org, "wolfSSL", CTC_NAME_SIZE);
    strncpy(leaf.subject.commonName, "localhost", CTC_NAME_SIZE);
    leaf.isCA = 0;
    leaf.selfSigned = 0;
    leaf.sigType = CTC_ML_DSA_87;
    leaf.daysValid = 30;
    ret = wc_SetIssuerBuffer(&leaf, caDer, caSz);
    if (ret != 0) { fprintf(stderr, "SetIssuerBuffer %d\n", ret); return -1; }
    ret = wc_MakeCert_ex(&leaf, leafDer, (word32)sizeof(leafDer),
            ML_DSA_87_TYPE, &boardKey, &rng);
    if (ret < 0) { fprintf(stderr, "MakeCert(leaf) %d\n", ret); return -1; }
    leafSz = wc_SignCert_ex(leaf.bodySz, leaf.sigType, leafDer,
            (word32)sizeof(leafDer), ML_DSA_87_TYPE, &caKey, &rng);
    if (leafSz < 0) { fprintf(stderr, "SignCert(leaf) %d\n", leafSz); return -1; }

#ifdef WOLFSSL_CERT_GEN_CACHE
    wc_SetCert_Free(&leaf);
#endif
    wc_MlDsaKey_Free(&boardKey);
    wc_MlDsaKey_Free(&caKey);
    wc_FreeRng(&rng);

    if (write_file(CA_CERT_FILE, caDer, caSz) != 0) return -1;
    if (write_file(LEAF_CERT_FILE, leafDer, leafSz) != 0) return -1;

    /* Shape kept identical to gen_pqc_certs so the host's existing regex
     * needs no change. */
    printf("Wrote %s (%d bytes) and %s (%d bytes)\n",
           CA_CERT_FILE, caSz, LEAF_CERT_FILE, leafSz);
    fflush(stdout);
    return 0;
}

/* ---------------------------------------------------------------- sockets */

static SOCK_T listen_on(int port)
{
    SOCK_T s;
    struct sockaddr_in a;
    int on = 1;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD)
        return SOCK_BAD;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(s, 1) != 0) {
        CLOSESOCK(s);
        return SOCK_BAD;
    }
    return s;
}

static SOCK_T connect_to(int port)
{
    SOCK_T s;
    struct sockaddr_in a;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_BAD)
        return SOCK_BAD;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)port);
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
        CLOSESOCK(s);
        return SOCK_BAD;
    }
    return s;
}

/* ------------------------------------------------------------------ modes */

static int run_server(int port)
{
    static const unsigned char keyId[] = { 'b','o','a','r','d','m','l','d','s','a' };
    static unsigned char leafDer[MAX_CERT];
    int leafSz;
    WOLFSSL_CTX* ctx = NULL;
    WOLFSSL* ssl = NULL;
    SOCK_T lfd = SOCK_BAD, cfd = SOCK_BAD;
    int rc = -1;

    leafSz = read_file(LEAF_CERT_FILE, leafDer, (int)sizeof(leafDer));
    if (leafSz <= 0) {
        fprintf(stderr, "board_tls: no %s, run -gencert first\n",
                LEAF_CERT_FILE);
        return -1;
    }

    if (wc_CryptoCb_RegisterDevice(BOARD_DEV_ID, board_crypto_cb, NULL) != 0) {
        fprintf(stderr, "board_tls: RegisterDevice failed\n");
        return -1;
    }

    ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
    if (ctx == NULL)
        goto done;
    wolfSSL_CTX_SetDevId(ctx, BOARD_DEV_ID);

    if (wolfSSL_CTX_use_certificate_buffer(ctx, leafDer, leafSz,
            WOLFSSL_FILETYPE_ASN1) != WOLFSSL_SUCCESS) {
        fprintf(stderr, "board_tls: use_certificate_buffer failed\n");
        goto done;
    }
    /* The private key never leaves the part; wolfSSL holds only a reference
     * and reaches it through the crypto callback. */
    if (wolfSSL_CTX_use_PrivateKey_Id(ctx, keyId, sizeof(keyId),
            BOARD_DEV_ID) != WOLFSSL_SUCCESS) {
        fprintf(stderr, "board_tls: use_PrivateKey_Id failed\n");
        goto done;
    }

    lfd = listen_on(port);
    if (lfd == SOCK_BAD) {
        fprintf(stderr, "board_tls: cannot listen on %d\n", port);
        goto done;
    }
    fprintf(stderr, "board_tls: listening on %d\n", port);

    cfd = accept(lfd, NULL, NULL);
    if (cfd == SOCK_BAD)
        goto done;

    ssl = wolfSSL_new(ctx);
    if (ssl == NULL)
        goto done;
    wolfSSL_set_fd(ssl, (int)cfd);

    if (wolfSSL_accept(ssl) != WOLFSSL_SUCCESS) {
        int err = wolfSSL_get_error(ssl, -1);
        char buf[80];
        fprintf(stderr, "board_tls: accept failed %d: %s\n", err,
                wolfSSL_ERR_error_string((unsigned long)err, buf));
        goto done;
    }

    /* Both lines are what the host parses: the first names the negotiated
     * connection, the second proves the board did the signing rather than
     * something on this machine. */
    printf("Handshake: %s, group %s (ML-DSA identity signed on TPM)\n",
           wolfSSL_get_cipher(ssl), wolfSSL_get_curve_name(ssl));
    printf("Delegated signatures: %d\n", signCount);
    fflush(stdout);
    rc = 0;

done:
    if (ssl != NULL) wolfSSL_free(ssl);
    if (cfd != SOCK_BAD) CLOSESOCK(cfd);
    if (lfd != SOCK_BAD) CLOSESOCK(lfd);
    if (ctx != NULL) wolfSSL_CTX_free(ctx);
    return rc;
}

static int run_client(int port, int group)
{
    static unsigned char caDer[MAX_CERT];
    int caSz;
    WOLFSSL_CTX* ctx = NULL;
    WOLFSSL* ssl = NULL;
    SOCK_T fd = SOCK_BAD;
    int rc = -1;

    caSz = read_file(CA_CERT_FILE, caDer, (int)sizeof(caDer));
    if (caSz <= 0) {
        fprintf(stderr, "board_tls: no %s, run -gencert first\n", CA_CERT_FILE);
        return -1;
    }

    ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
    if (ctx == NULL)
        goto done;
    if (wolfSSL_CTX_load_verify_buffer(ctx, caDer, caSz,
            WOLFSSL_FILETYPE_ASN1) != WOLFSSL_SUCCESS) {
        fprintf(stderr, "board_tls: load_verify_buffer failed\n");
        goto done;
    }

    fd = connect_to(port);
    if (fd == SOCK_BAD) {
        fprintf(stderr, "board_tls: cannot connect to %d\n", port);
        goto done;
    }
    ssl = wolfSSL_new(ctx);
    if (ssl == NULL)
        goto done;
    if (wolfSSL_UseKeyShare(ssl, (word16)group) != WOLFSSL_SUCCESS) {
        fprintf(stderr, "board_tls: UseKeyShare failed; is wolfSSL built with "
                "--enable-tls-mlkem-standalone?\n");
        goto done;
    }
    wolfSSL_set_fd(ssl, (int)fd);

    if (wolfSSL_connect(ssl) != WOLFSSL_SUCCESS) {
        int err = wolfSSL_get_error(ssl, -1);
        char buf[80];
        fprintf(stderr, "board_tls: connect failed %d: %s\n", err,
                wolfSSL_ERR_error_string((unsigned long)err, buf));
        goto done;
    }
    printf("Client: %s, group %s\n", wolfSSL_get_cipher(ssl),
           wolfSSL_get_curve_name(ssl));
    fflush(stdout);
    rc = 0;

done:
    if (ssl != NULL) wolfSSL_free(ssl);
    if (fd != SOCK_BAD) CLOSESOCK(fd);
    if (ctx != NULL) wolfSSL_CTX_free(ctx);
    return rc;
}

int main(int argc, char** argv)
{
    int i, port = 11500, group = WOLFSSL_ML_KEM_768;
    int doGen = 0, doServer = 0, doClient = 0;
    const char* pubHex = NULL;
    int rc;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-gencert") == 0)             doGen = 1;
        else if (strcmp(argv[i], "-server") == 0)         doServer = 1;
        else if (strcmp(argv[i], "-client") == 0)         doClient = 1;
        else if (strncmp(argv[i], "-pub=", 5) == 0)       pubHex = argv[i] + 5;
        else if (strncmp(argv[i], "-p=", 3) == 0)         port = atoi(argv[i] + 3);
        else {
            fprintf(stderr,
                "usage: %s -gencert -pub=<hex> | -server [-p=N] | "
                "-client [-p=N]\n", argv[0]);
            return 2;
        }
    }

    wolfSSL_Init();

    if (doGen) {
        if (pubHex == NULL) {
            fprintf(stderr, "board_tls: -gencert needs -pub=<hex>\n");
            rc = 2;
        }
        else {
            rc = (gen_cert(pubHex) == 0) ? 0 : 1;
        }
    }
    else if (doServer) {
        rc = (run_server(port) == 0) ? 0 : 1;
    }
    else if (doClient) {
        rc = (run_client(port, group) == 0) ? 0 : 1;
    }
    else {
        fprintf(stderr, "board_tls: pick one of -gencert, -server, -client\n");
        rc = 2;
    }

    wolfSSL_Cleanup();
    return rc;
}
