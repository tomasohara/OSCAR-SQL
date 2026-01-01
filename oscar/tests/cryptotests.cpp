/* Cryptographic Abstraction Unit Tests
 *
 * Copyright (c) 2021-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "cryptotests.h"
#include "SleepLib/crypto.h"

//#define BENCHMARK_CRYPTO 1

void CryptoTests::testAES256()
{
    // From FIPS-197 C.3
    QByteArray expected_plaintext = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    QByteArray key = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    QByteArray ciphertext = QByteArray::fromHex("8ea2b7ca516745bfeafc49904b496089");

    QByteArray plaintext;
    CryptoResult result = decrypt_aes256(key, ciphertext, plaintext);
    Q_ASSERT(result == OK);
    Q_ASSERT(plaintext == expected_plaintext);
}


// From https://luca-giuzzi.unibs.it/corsi/Support/papers-cryptography/gcm-spec.pdf
typedef struct AES256GCMVector_t
{
    const char* key;
    const char* p;
    const char* iv;
    const char* c;
    const char* tag;
} AES256GCMVector_t;
static const int s_AES256GCMVectorCount = 3;
static const AES256GCMVector_t s_AES256GCMVectors[s_AES256GCMVectorCount] = {
    // Test Case 13
    {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "",
        "000000000000000000000000",
        "",
        "530f8afbc74536b9a963b4f1c4cb738b"
    },
    // Test Case 14
    {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "00000000000000000000000000000000",
        "000000000000000000000000",
        "cea7403d4d606b6e074ec5d3baf39d18",
        "d0d1c8a799996bf0265b98b5d48ab919"
    },
    // Test Case 15
    {
        "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
        "cafebabefacedbaddecaf888",
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad",
        "b094dac5d93471bdec1a502270e3cc6c"
    }
};

void CryptoTests::testAES256GCM()
{
    QByteArray empty;
    for (int i = 0; i < s_AES256GCMVectorCount; i++) {
        const AES256GCMVector_t* v = &s_AES256GCMVectors[i];
        QByteArray key = QByteArray::fromHex(v->key);
        QByteArray expected_plaintext = QByteArray::fromHex(v->p);
        QByteArray iv = QByteArray::fromHex(v->iv);
        QByteArray ciphertext = QByteArray::fromHex(v->c);
        QByteArray tag = QByteArray::fromHex(v->tag);

        QByteArray plaintext;
        CryptoResult result = decrypt_aes256_gcm(key, iv, ciphertext, tag, plaintext);
        Q_ASSERT(result == OK);
        Q_ASSERT(plaintext == expected_plaintext);
        
        tag = QByteArray::fromHex(s_AES256GCMVectors[(i+1) % s_AES256GCMVectorCount].tag);
        result = decrypt_aes256_gcm(key, iv, ciphertext, tag, plaintext);
        Q_ASSERT(result == InvalidTag);
        Q_ASSERT(plaintext == empty);
    }
}

void CryptoTests::testAES256GCMencrypt()
{
    QByteArray empty;
    for (int i = 0; i < s_AES256GCMVectorCount; i++) {
        const AES256GCMVector_t* v = &s_AES256GCMVectors[i];
        QByteArray key = QByteArray::fromHex(v->key);
        QByteArray plaintext = QByteArray::fromHex(v->p);
        QByteArray iv = QByteArray::fromHex(v->iv);
        QByteArray expected_ciphertext = QByteArray::fromHex(v->c);
        QByteArray expected_tag = QByteArray::fromHex(v->tag);

        QByteArray ciphertext;
        QByteArray tag;
        CryptoResult result = encrypt_aes256_gcm(key, iv, plaintext, ciphertext, tag);
        Q_ASSERT(result == OK);
        Q_ASSERT(ciphertext == expected_ciphertext);
        Q_ASSERT(tag == expected_tag);
    }
}

void CryptoTests::testPBKDF2_SHA256()
{
    // From RFC 7914 section 11
    QByteArray passphrase("passwd");
    QByteArray salt("salt");
    int iterations = 1;
    QByteArray expected_key = QByteArray::fromHex(
        "55 ac 04 6e 56 e3 08 9f ec 16 91 c2 25 44 b6 05"
        "f9 41 85 21 6d de 04 65 e6 8b 9d 57 c2 0d ac bc"
        "49 ca 9c cc f1 79 b6 45 99 16 64 b3 9d 77 ef 31"
        "7c 71 b8 45 b1 e3 0b d5 09 11 20 41 d3 a1 97 83");
    
    QByteArray derived_key(expected_key.size(), 0);
    CryptoResult result = pbkdf2_sha256(passphrase, salt, iterations, derived_key);
    Q_ASSERT(result == OK);
    Q_ASSERT(derived_key == expected_key);
}


void CryptoTests::testPRS1Benchmarks()
{
#if BENCHMARK_CRYPTO
    static const int AES_ITERATIONS = 500;
    static const int PBKDF2_ITERATIONS = 100;
    
    QTime time;
    qDebug() << "Timing AESGCM...";
    time.start();
    for (int i = 0; i < AES_ITERATIONS; i++) {
        // On average, a full directory of 500 PRS1 files is about 10-20MB, so use 32kB/file as representative.
        QByteArray ciphertext(32768, 0);
        QByteArray key = QByteArray::fromHex("0000000000000000000000000000000000000000000000000000000000000000");
        QByteArray iv = QByteArray::fromHex("000000000000000000000000");
        QByteArray tag = QByteArray::fromHex("51dedf58b6a5299bff9d06e041efe725");

        QByteArray plaintext;
        CryptoResult result = decrypt_aes256_gcm(key, iv, ciphertext, tag, plaintext);
        Q_ASSERT(result == OK);
    }
    int elapsed = time.restart();
    qDebug() << "AESGCM x" << AES_ITERATIONS << "=" << elapsed << "ms," << ((float)elapsed / AES_ITERATIONS) << "ms/file";

    qDebug() << "Timing PBKDF2...";
    time.restart();
    for (int i = 0; i < PBKDF2_ITERATIONS; i++) {
        QByteArray passphrase("passwd");
        QByteArray salt("salt");
        QByteArray derived_key(32, 0);
        CryptoResult result = pbkdf2_sha256(passphrase, salt, 10000, derived_key);
        Q_ASSERT(result == OK);
    }
    elapsed = time.restart();
    qDebug() << "PBKDF2 x" << PBKDF2_ITERATIONS << "=" << elapsed << "ms," << ((float)elapsed / PBKDF2_ITERATIONS) << "ms/file";
#endif
}
