/**
 * KTX2 Stub Functions
 *
 * This file provides stub implementations for KTX2 functions that are
 * referenced by texture.c but not needed when only KTX1 support is required.
 *
 * KTX2 has many additional dependencies (zstd, basis_universal, etc.) that
 * we want to avoid pulling in for simple texture loading use cases.
 */

#include <ktx.h>

/* Forward declarations to avoid including internal headers */
typedef struct ktxStream ktxStream;
typedef struct KTX_header2 KTX_header2;

KTX_error_code
ktxTexture2_constructFromStreamAndHeader(ktxTexture2* This,
                                         ktxStream* pStream,
                                         KTX_header2* pHeader,
                                         ktx_uint32_t createFlags) {
    (void)This;
    (void)pStream;
    (void)pHeader;
    (void)createFlags;
    /* KTX2 is not supported in this build */
    return KTX_UNSUPPORTED_TEXTURE_TYPE;
}
