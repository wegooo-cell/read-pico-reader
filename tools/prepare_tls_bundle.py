#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2018-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""中文：为 IDF 6.1 交叉签名回调保留真实证书有效期检查。 / English: Keep real certificate validity checks in the IDF 6.1 cross-signed callback.

只处理由已信任证书包生成、没有原始 DER 和有效期的合成根；不放宽主机名、签名或真实证书有效期。
Only handle synthetic bundle roots without DER/validity; never relax hostname, signatures or real certificate dates.
"""
from pathlib import Path
import re
import sys

BLOCK = '#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY)\n    /* When cross-signed verification is enabled, the CA callback provides a\n     * synthetic bundle root containing only the subject name and public key.\n     * It has no meaningful validity window, so mbedtls may set EXPIRED/FUTURE\n     * on this generated cert. Clear those flags only for this synthetic bundle\n     * root so that cross-signed verification can continue.\n     *\n     * Real certificates must keep their time-based verification result and\n     * should not proceed to additional bundle signature checks once they are\n     * marked expired or not-yet-valid. */\n    const uint32_t time_flags = *flags &\n        (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);\n    if (time_flags && s_crt_bundle != NULL && child->raw.p == NULL &&\n        child->valid_from.year == 0 && child->valid_to.year == 0) {\n        cert_t cert = esp_crt_find_cert(child->subject_raw.p,\n                                        child->subject_raw.len);\n        if (cert != NULL) {\n            *flags &= ~(MBEDTLS_X509_BADCERT_EXPIRED |\n                         MBEDTLS_X509_BADCERT_FUTURE);\n        }\n    }\n#endif /* CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY */'

def prepare(idf: Path):
    path=idf/'components/mbedtls/esp_crt_bundle/esp_crt_bundle.c'
    source=path.read_text()
    begin=source.index('int esp_crt_verify_callback(')
    at=source.index('    const mbedtls_x509_crt* const child = crt;',begin)
    end=source.index('    uint32_t flags_filtered',at)
    if BLOCK not in source[at:end]:
        assert 'child->raw.p' not in source[at:end], 'Unknown SDK callback; review before building'
        source=source[:at]+source[at:end]+BLOCK+'\n\n'+source[end:]
    mask=re.search(r'uint32_t flags_filtered = \*flags & ~\((.*?)\);',source[begin:],re.S)
    assert mask
    terms={x.strip() for x in mask[1].split('|')}
    assert terms <= {'MBEDTLS_X509_BADCERT_BAD_MD','MBEDTLS_X509_BADCERT_EXPIRED','MBEDTLS_X509_BADCERT_FUTURE'}
    assert 'MBEDTLS_X509_BADCERT_BAD_MD' in terms
    start=begin+mask.start();stop=begin+mask.end()
    source=source[:start]+'uint32_t flags_filtered = *flags & ~(MBEDTLS_X509_BADCERT_BAD_MD);'+source[stop:]
    if source!=path.read_text():path.write_text(source)
    print('IDF cross-signed callback verified: synthetic bundle roots only; real validity retained')

if __name__=='__main__':prepare(Path(sys.argv[1]).resolve())
