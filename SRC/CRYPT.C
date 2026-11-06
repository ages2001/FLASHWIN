#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
/* crypt32 certificate-chain gap closures. A real certificate store / signature
   verification subsystem is out of scope for early NT; these return clean
   "unavailable / not verified" results so signed or certificate-bearing content
   is rejected gracefully instead of aborting the player. NON-FUNCTIONAL by
   design — no certificate is ever reported valid. No CRT. */

HCERTSTORE WINAPI fp_CertOpenStore(LPCSTR t,DWORD enc,HCRYPTPROV_LEGACY p,DWORD f,const void*pa){
    (void)t;(void)enc;(void)p;(void)f;(void)pa; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}
BOOL WINAPI fp_CertCloseStore(HCERTSTORE s,DWORD f){(void)s;(void)f;return TRUE;}
PCCERT_CONTEXT WINAPI fp_CertCreateCertificateContext(DWORD enc,const BYTE*b,DWORD n){
    (void)enc;(void)b;(void)n; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return 0;
}
PCCERT_CONTEXT WINAPI fp_CertDuplicateCertificateContext(PCCERT_CONTEXT c){ return c; }
BOOL WINAPI fp_CertFreeCertificateContext(PCCERT_CONTEXT c){(void)c;return TRUE;}
PCCERT_CONTEXT WINAPI fp_CertFindCertificateInStore(HCERTSTORE s,DWORD enc,DWORD ff,DWORD ft,
                                                    const void*fp,PCCERT_CONTEXT prev){
    (void)s;(void)enc;(void)ff;(void)ft;(void)fp;(void)prev; return 0;
}
PCCERT_CONTEXT WINAPI fp_CertEnumCertificatesInStore(HCERTSTORE s,PCCERT_CONTEXT prev){
    (void)s;(void)prev; return 0;
}
BOOL WINAPI fp_CertGetCertificateContextProperty(PCCERT_CONTEXT c,DWORD id,void*d,DWORD*n){
    (void)c;(void)id;(void)d; if(n)*n=0; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE;
}
BOOL WINAPI fp_CertCompareCertificate(DWORD enc,PCERT_INFO a,PCERT_INFO b){
    (void)enc;(void)a;(void)b; return FALSE;
}
BOOL WINAPI fp_CertVerifySubjectCertificateContext(PCCERT_CONTEXT s,PCCERT_CONTEXT issuer,DWORD*flags){
    (void)s;(void)issuer;
    /* Report every validity check as "could not verify". */
    if(flags) *flags = CERT_STORE_SIGNATURE_FLAG|CERT_STORE_TIME_VALIDITY_FLAG|CERT_STORE_REVOCATION_FLAG;
    return TRUE;
}
BOOL WINAPI fp_CryptVerifyMessageSignature(void*para,DWORD idx,const BYTE*b,DWORD n,
                                           BYTE*out,DWORD*outn,PCCERT_CONTEXT*signer){
    (void)para;(void)idx;(void)b;(void)n;(void)out; if(outn)*outn=0; if(signer)*signer=0;
    SetLastError((DWORD)NTE_BAD_SIGNATURE); return FALSE;
}
HCERTSTORE WINAPI fp_CryptGetMessageCertificates(DWORD enc,HCRYPTPROV_LEGACY p,DWORD f,const BYTE*b,DWORD n){
    (void)enc;(void)p;(void)f;(void)b;(void)n; return 0;
}
