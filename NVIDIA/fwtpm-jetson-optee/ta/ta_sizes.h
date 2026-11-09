/* Measured on this board, bounded from both sides.
 *
 *   heap  (TA_DATA_SIZE):  96 KB passes, 88 KB fails. At 64 KB native_test
 *                          still passes and only ML-DSA fails, so ML-DSA is
 *                          the binding constraint. The acceptance test signs
 *                          with ML-DSA-65; ML-DSA-87 needs more.
 *   stack (TA_STACK_SIZE):  8 KB passes, 4 KB fails. WOLFTPM_SMALL_STACK and
 *                          WOLFSSL_SMALL_STACK keep the large temporaries on
 *                          the heap, which is what makes that possible.
 *
 * FWTPM_CTX is a 161 KB static and lives in .bss, not in this heap.
 * The TA load ceiling on this board is between 47 and 48 MB, so these
 * defaults leave roughly 5x headroom on the heap and 4x on the stack. */
#define TA_STACK_SIZE (32 * 1024)
#define TA_DATA_SIZE  (512 * 1024)
