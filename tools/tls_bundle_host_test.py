#!/usr/bin/env python3
"""中文：执行 SDK 补丁的真实片段，真实证书日期与未知根必须保持失败。 / English: Execute the SDK patch fragment; real dates and unknown roots must retain failure flags."""
from pathlib import Path
import subprocess,tempfile
from prepare_tls_bundle import BLOCK,prepare
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY 1
#define MBEDTLS_X509_BADCERT_EXPIRED 1
#define MBEDTLS_X509_BADCERT_FUTURE 2
#define MBEDTLS_X509_BADCERT_NOT_TRUSTED 4
#define MBEDTLS_X509_BADCERT_CN_MISMATCH 8
#define NULL ((void*)0)
typedef struct {unsigned char *p;unsigned len;} buf_t;
typedef struct {buf_t raw,subject_raw;struct {int year;}valid_from,valid_to;} crt_t;
typedef void *cert_t;
static void *s_crt_bundle=(void*)1;
static bool known;
static cert_t esp_crt_find_cert(void *p,unsigned n){(void)p;(void)n;return known?(void*)2:NULL;}
static uint32_t flags_for(crt_t *child,uint32_t value){uint32_t *flags=&value;
'''+BLOCK+r'''
return value;}
int main(void){
 crt_t synthetic={0},real={.raw={(void*)1,1},.valid_from={2020},.valid_to={2030}};
 known=true;assert(flags_for(&synthetic,1|2|4|8)==(4|8));
 assert(flags_for(&real,1|2|4|8)==(1|2|4|8));
 known=false;assert(flags_for(&synthetic,1|2|4)==(1|2|4));
 s_crt_bundle=NULL;known=true;assert(flags_for(&synthetic,1|4)==(1|4));
}
'''
with tempfile.TemporaryDirectory() as folder:
 root=Path(folder);src=root/'test.c';src.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test')],check=True)
 p=root/'components/mbedtls/esp_crt_bundle/esp_crt_bundle.c';p.parent.mkdir(parents=True)
 p.write_text('int esp_crt_verify_callback(void) {\n    const mbedtls_x509_crt* const child = crt;\n    uint32_t flags_filtered = *flags & ~(MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE);\n}\n')
 prepare(root);first=p.read_bytes();prepare(root);assert p.read_bytes()==first
 assert b'child->raw.p == NULL' in first
 assert b'flags_filtered = *flags & ~(MBEDTLS_X509_BADCERT_BAD_MD);' in first
print('PASS: synthetic bundle roots only; real expiry/future/hostname errors retained; unknown roots rejected; SDK patch idempotent')
