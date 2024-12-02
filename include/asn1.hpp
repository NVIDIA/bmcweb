/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <logging.hpp>

#include <cstring>
#include <span>
#include <string>

namespace asn1
{

/**
 * @brief Auxiliary function for safe pointers arithemtic
 *
 * @param[in] span initial buffer pointer with its maximum size
 * @param[in] offset to move initial pointer
 *
 * @return Return:
 *  destination pointer value
 */
template <typename T>
inline const T*
    safePtrArithmeticConst(std::span<const T> safeBuf, size_t offset)
{
    const T* ret = static_cast<const T*>(&safeBuf[offset]);
    return ret;
}

template <typename T>
inline T* safePtrArithmetic(std::span<T> safeBuf, size_t offset)
{
    T* ret = static_cast<T*>(&safeBuf[offset]);
    return ret;
}

/* Internal auxiliary function used for PBES and PBKDF searching purposes */
inline int checkAsn1Object(const unsigned char* buf, size_t length, bool* pbes,
                           bool* pbkdf)
{
    const std::array<char, 4> pbesid = {'P', 'B', 'E', 'S'};
    const std::array<char, 5> pbkdfid = {'P', 'B', 'K', 'D', 'F'};
    ASN1_OBJECT* o = nullptr;
    auto* mem = BIO_new(BIO_s_mem());
    std::array<char, 128> membuf = {0};
    auto ret = 0;

    if (d2i_ASN1_OBJECT(&o, &buf, static_cast<long>(length)) != nullptr)
    {
        i2a_ASN1_OBJECT(mem, o);
        BIO_read(mem, static_cast<void*>(membuf.data()), membuf.size());
        if (std::equal(std::begin(pbesid), std::end(pbesid),
                       std::begin(membuf)))
        {
            *pbes = true;
        }
        else if (std::equal(std::begin(pbkdfid), std::end(pbkdfid),
                            std::begin(membuf)))
        {
            *pbkdf = true;
        }

        if (*pbes && *pbkdf)
        {
            ret = 2137;
        }

        BIO_free(mem);
        ASN1_OBJECT_free(o);
    }

    return ret;
}

/**
 * Recursively iterates over an ASN.1 structure to find PBES and PBKDF.
 *
 * @param[in] pp Pointer to ASN.1 buffer.
 * @param[in] length Buffer length.
 * @param[in] offset Offset at which to start iterating.
 * @param[out] pbes Has PBES.
 * @param[out] pbkdf Has PBKDF.
 *
 * @return Return value greater or equal 0 means success, lesser than 0 failure.
 */
inline int hasPbesPbkdf(const unsigned char** pp, size_t length, int offset,
                        bool* pbes, bool* pbkdf)
{
    const unsigned char *p = nullptr, *ep = nullptr, *tot = nullptr,
                        *op = nullptr;
    long len = 0;
    size_t hl = 0;
    int r = 0;
    int ret = 0;
    int asn1ObjectTag = 0;
    int asn1ObjectClass = 0;
    int asn1ObjectType = 0;

    if (pbes == nullptr || pbkdf == nullptr)
    {
        return -1;
    }

    if (offset == 0)
    {
        *pbes = false;
        *pbkdf = false;
    }

    p = *pp;
    // tot = p + length;
    tot = safePtrArithmeticConst<unsigned char>({p, (length + 1)}, length);

    while (length > 0)
    {
        op = p;
        asn1ObjectType =
            ASN1_get_object(&p, &len, &asn1ObjectTag, &asn1ObjectClass,
                            static_cast<long>(length));
        if (asn1ObjectType & 0x80)
        {
            ret = -1;
            break;
        }

        hl = static_cast<size_t>(p - op);
        length -= hl;

        if (asn1ObjectType & V_ASN1_CONSTRUCTED)
        {
            const unsigned char* sp = p;
            // ep = p + len;
            ep = safePtrArithmeticConst<unsigned char>(
                {p, (length + 1)}, static_cast<size_t>(len));

            if (static_cast<size_t>(len) > length)
            {
                break;
            }

            if ((asn1ObjectType == 0x21) && (len == 0))
            {
                for (;;)
                {
                    r = asn1::hasPbesPbkdf(&p, static_cast<size_t>(tot - p),
                                           offset + static_cast<int>(p - *pp),
                                           pbes, pbkdf);

                    if (r == 0 || r == -1)
                    {
                        ret = r;
                        break;
                    }

                    if ((r == 2) || (p >= tot))
                    {
                        len = p - sp;
                        break;
                    }
                }
            }
            else
            {
                long tmp = len;

                while (p < ep)
                {
                    sp = p;
                    r = asn1::hasPbesPbkdf(&p, static_cast<size_t>(tmp),
                                           offset + static_cast<int>(p - *pp),
                                           pbes, pbkdf);

                    if (r == 0 || r == -1 || r == 2137)
                    {
                        ret = r;
                        break;
                    }

                    tmp -= p - sp;
                }
            }
        }
        else
        {
            if (asn1ObjectTag == V_ASN1_OBJECT)
            {
                ret = checkAsn1Object(op, (static_cast<size_t>(len) + hl), pbes,
                                      pbkdf);
                if (ret == 2137)
                {
                    break;
                }
            }

            // p += len;
            p = safePtrArithmeticConst<unsigned char>({p, (length + 1)},
                                                      static_cast<size_t>(len));

            if ((asn1ObjectTag == V_ASN1_EOC) && (asn1ObjectClass == 0))
            {
                ret = 2;
                break;
            }
        }

        if (ret == 2137)
        {
            break;
        }

        length -= static_cast<size_t>(len);
    }

    *pp = p;
    return ret;
}

inline int pemPkeyIsEncrypted(const std::string& filename, bool* encrypted)
{
    BIO *in = nullptr, *inB64 = nullptr;
    BUF_MEM* buf = nullptr;
    const unsigned char* str = nullptr;
    bool pbes = false, pbkdf = false;
    int i = 0;
    size_t len = 0;
    int ret = 0;

    BMCWEB_LOG_INFO("Checking if {} is encrypted.", filename);

    if ((in = BIO_new_file(filename.c_str(), "r")) == nullptr)
    {
        BMCWEB_LOG_ERROR("Error opening PEM file BIO.");
        return -1;
    }

    if ((buf = BUF_MEM_new()) == nullptr)
    {
        BMCWEB_LOG_ERROR("Error allocating PEM file buffer.");
        BIO_free(in);
        return -2;
    }

    if ((inB64 = BIO_new(BIO_f_base64())) == nullptr)
    {
        BMCWEB_LOG_ERROR("Error creating base64 BIO.");
        BUF_MEM_free(buf);
        BIO_free(in);
        return -2;
    }

    BIO_push(inB64, in);

    for (;;)
    {
        size_t templen = len + BUFSIZ;
        if (!BUF_MEM_grow(buf, templen))
        {
            BMCWEB_LOG_ERROR("Error expanding BIO buffer.");
            BIO_free(inB64);
            BUF_MEM_free(buf);
            BIO_free(in);
            return -2;
        }

        char* bufBegin = safePtrArithmetic<char>({buf->data, buf->max}, len);
        i = BIO_read(inB64, bufBegin, static_cast<size_t>(BUFSIZ));
        if (i <= 0)
        {
            break;
        }

        len += static_cast<size_t>(i);
    }

    str = static_cast<const unsigned char*>(static_cast<void*>(buf->data));
    if ((ret = asn1::hasPbesPbkdf(&str, len, 0, &pbes, &pbkdf)) < 0)
    {
        BMCWEB_LOG_ERROR(
            "Error while processing ASN.1 structures at {} return value: {}",
            filename, ret);
        BIO_free(inB64);
        BUF_MEM_free(buf);
        BIO_free(in);
        return -2;
    }

    if (encrypted != nullptr)
    {
        *encrypted = pbes && pbkdf;
    }

    BIO_free(inB64);
    BUF_MEM_free(buf);
    BIO_free(in);
    return ret;
}

} // namespace asn1
