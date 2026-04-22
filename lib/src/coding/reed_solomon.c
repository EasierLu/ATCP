#include "reed_solomon.h"
#include "gf256.h"
#include <string.h>

atcp_status_t atcp_rs_init(atcp_rs_t *rs, int nsym)
{
    if (!rs || nsym < 1 || nsym > 254)
        return ATCP_ERR_INVALID_PARAM;

    gf256_init();

    rs->nsym = nsym;

    /* 生成多项式 g(x) = Π(x - α^i), i=0..nsym-1 */
    /* 从 g(x) = (x - α^0) = {1, α^0} 开始 */
    rs->gen_poly[0] = 1;
    rs->gen_poly[1] = gf256_pow(2, 0); /* α^0 = 1 */
    rs->gen_poly_len = 2;

    uint8_t tmp[256];
    int tmp_len;

    for (int i = 1; i < nsym; i++) {
        uint8_t factor[2] = { 1, gf256_pow(2, i) }; /* (x - α^i) = (x + α^i) in GF(2^8) */
        gf256_poly_mul(rs->gen_poly, rs->gen_poly_len, factor, 2, tmp, &tmp_len);
        memcpy(rs->gen_poly, tmp, (size_t)tmp_len);
        rs->gen_poly_len = tmp_len;
    }

    return ATCP_OK;
}

atcp_status_t atcp_rs_encode(const atcp_rs_t *rs, const uint8_t *data, int data_len,
                         uint8_t *output)
{
    if (!rs || !data || !output)
        return ATCP_ERR_INVALID_PARAM;

    int max_data = 255 - rs->nsym;
    if (data_len < 1 || data_len > max_data - 1)
        return ATCP_ERR_INVALID_PARAM;

    /* 构造消息多项式: [length_byte | data | trailing_zeros | nsym个0用于除法] */
    uint8_t msg[512];
    int msg_len = 255; /* max_data + nsym */
    memset(msg, 0, sizeof(msg));

    /* 第0字节存储原始数据长度，随后是数据，其余为0填充 */
    msg[0] = (uint8_t)data_len;
    memcpy(msg + 1, data, (size_t)data_len);
    /* msg[1+data_len .. max_data-1] = 0 (尾部填充) */
    /* msg[max_data .. 254] = 0 (nsym个0用于除法) */

    /* 多项式除法求余数 */
    uint8_t remainder[256];
    int rem_len;
    gf256_poly_divmod(msg, msg_len, rs->gen_poly, rs->gen_poly_len,
                      remainder, &rem_len);

    /* 输出 = 数据区(含长度前缀) + 校验码 */
    memcpy(output, msg, (size_t)max_data);
    memcpy(output + max_data, remainder, (size_t)rs->nsym);

    return ATCP_OK;
}

/* ---------- 解码辅助函数 ---------- */

/* 计算 syndromes: S_i = r(α^i), i=0..nsym-1 */
static void calc_syndromes(const uint8_t *codeword, int nsym, uint8_t *synd)
{
    for (int i = 0; i < nsym; i++) {
        synd[i] = gf256_poly_eval(codeword, 255, gf256_pow(2, i));
    }
}

/* Berlekamp-Massey 算法求错误定位多项式 σ(x) */
static int berlekamp_massey(const uint8_t *synd, int nsym,
                            uint8_t *sigma, int *sigma_len)
{
    uint8_t C[256], B[256], T[256];
    memset(C, 0, sizeof(C));
    memset(B, 0, sizeof(B));
    C[0] = 1;
    B[0] = 1;
    int L = 0, m = 1, b = 1;
    int C_len = 1, B_len = 1;

    for (int n = 0; n < nsym; n++) {
        /* 计算差异 d */
        uint8_t d = synd[n];
        for (int i = 1; i <= L; i++) {
            d ^= gf256_mul(C[i], synd[n - i]);
        }

        if (d == 0) {
            m++;
        } else if (2 * L <= n) {
            /* 保存 T = C */
            int T_len = C_len;
            memcpy(T, C, (size_t)C_len);

            /* C = C - (d/b) * x^m * B */
            uint8_t coeff = gf256_div(d, (uint8_t)b);
            for (int i = 0; i < B_len; i++) {
                int idx = i + m;
                if (idx >= 256) break;
                if (idx >= C_len) {
                    memset(C + C_len, 0, (size_t)(idx + 1 - C_len));
                    C_len = idx + 1;
                }
                C[idx] ^= gf256_mul(coeff, B[i]);
            }

            L = n + 1 - L;
            memcpy(B, T, (size_t)T_len);
            B_len = T_len;
            b = d;
            m = 1;
        } else {
            /* C = C - (d/b) * x^m * B */
            uint8_t coeff = gf256_div(d, (uint8_t)b);
            for (int i = 0; i < B_len; i++) {
                int idx = i + m;
                if (idx >= 256) break;
                if (idx >= C_len) {
                    memset(C + C_len, 0, (size_t)(idx + 1 - C_len));
                    C_len = idx + 1;
                }
                C[idx] ^= gf256_mul(coeff, B[i]);
            }
            m++;
        }
    }

    memcpy(sigma, C, (size_t)C_len);
    *sigma_len = C_len;
    return L; /* 错误数量 */
}

/* Chien搜索: 找σ(x)的根，返回错误位置 */
static int chien_search(const uint8_t *sigma, int sigma_len,
                        int *err_pos, int max_errors)
{
    int count = 0;
    for (int i = 0; i < 255; i++) {
        /* 测试 α^(-i) = α^(255-i) */
        uint8_t x = gf256_pow(2, 255 - i);
        uint8_t val = gf256_poly_eval(sigma, sigma_len, x);
        if (val == 0) {
            err_pos[count++] = i;
            if (count >= max_errors) break;
        }
    }
    return count;
}

/* Forney算法计算错误值 */
static uint8_t forney_error_value(const uint8_t *synd, int nsym,
                                  const uint8_t *sigma, int sigma_len,
                                  int err_pos)
{
    /* 由于 gf256_poly_eval 将 sigma[0] 视为最高次项（但BM中是常数项），
     * Chien搜索实际解的是 x^L * σ(1/x) = 0，根为 X_k = α^{-err_pos}。
     * Forney公式: e_k = X_k * Ω(X_k^{-1}) / σ'(X_k^{-1})
     * 其中 X_k = α^{-err_pos}, X_k^{-1} = α^{err_pos} */

    uint8_t Xi_inv = gf256_pow(2, err_pos);  /* α^{err_pos} = X_k^{-1} (求值点) */
    uint8_t Xi = gf256_inv(Xi_inv);           /* α^{-err_pos} = X_k (错误定位子) */

    /* 计算 Ω(X_k^{-1}): Ω(x) = S(x)*σ(x) mod x^nsym
     * 其中 S(x) = synd[0] + synd[1]x + ... + synd[nsym-1]x^{nsym-1}
     * sigma存储: sigma[0]=σ_0=1, sigma[1]=σ_1, ... */
    uint8_t omega_val = 0;
    {
        uint8_t xpow = 1; /* Xi_inv^0 */
        for (int k = 0; k < nsym; k++) {
            uint8_t coeff = 0;
            for (int j = 0; j <= k && j < sigma_len; j++) {
                coeff ^= gf256_mul(synd[k - j], sigma[j]);
            }
            omega_val ^= gf256_mul(coeff, xpow);
            xpow = gf256_mul(xpow, Xi_inv);
        }
    }

    /* 计算 σ'(X_k^{-1}) — 形式导数，只有奇数次项
     * σ'(x) = σ_1 + σ_3*x^2 + σ_5*x^4 + ... */
    uint8_t sigma_deriv = 0;
    {
        uint8_t xpow = 1; /* Xi_inv^0 */
        for (int i = 1; i < sigma_len; i += 2) {
            sigma_deriv ^= gf256_mul(sigma[i], xpow);
            xpow = gf256_mul(xpow, gf256_mul(Xi_inv, Xi_inv));
        }
    }

    if (sigma_deriv == 0) return 0;

    /* e_k = X_k * Ω(X_k^{-1}) / σ'(X_k^{-1}) */
    return gf256_mul(Xi, gf256_div(omega_val, sigma_deriv));
}

atcp_status_t atcp_rs_decode(const atcp_rs_t *rs, const uint8_t *codeword,
                         uint8_t *output, int *output_len)
{
    if (!rs || !codeword || !output || !output_len)
        return ATCP_ERR_INVALID_PARAM;

    int nsym = rs->nsym;
    int max_data = 255 - nsym;

    /* 计算 syndromes */
    uint8_t synd[256];
    calc_syndromes(codeword, nsym, synd);

    /* 检查是否全零（无错误） */
    int has_error = 0;
    for (int i = 0; i < nsym; i++) {
        if (synd[i] != 0) { has_error = 1; break; }
    }

    uint8_t corrected[255];
    memcpy(corrected, codeword, 255);

    if (has_error) {
        /* Berlekamp-Massey */
        uint8_t sigma[256];
        int sigma_len;
        int num_errors = berlekamp_massey(synd, nsym, sigma, &sigma_len);

        if (num_errors > nsym / 2)
            return ATCP_ERR_RS_DECODE_FAIL;

        /* Chien搜索 */
        int err_pos[256];
        int found = chien_search(sigma, sigma_len, err_pos, num_errors);

        if (found != num_errors)
            return ATCP_ERR_RS_DECODE_FAIL;

        /* Forney算法纠正错误 */
        for (int i = 0; i < found; i++) {
            uint8_t e = forney_error_value(synd, nsym, sigma, sigma_len, err_pos[i]);
            corrected[(254 + err_pos[i]) % 255] ^= e;
        }

        /* 验证纠正后的码字 */
        uint8_t check[256];
        calc_syndromes(corrected, nsym, check);
        for (int i = 0; i < nsym; i++) {
            if (check[i] != 0) return ATCP_ERR_RS_DECODE_FAIL;
        }
    }

    /* 提取数据部分：第0字节是长度前缀 */
    int original_len = corrected[0];
    if (original_len < 1 || original_len > max_data - 1)
        return ATCP_ERR_RS_DECODE_FAIL;

    memcpy(output, corrected + 1, (size_t)original_len);
    *output_len = original_len;

    return ATCP_OK;
}

atcp_status_t atcp_rs_encode_blocks(const atcp_rs_t *rs, const uint8_t *data, int data_len,
                                uint8_t *output, int *output_len)
{
    if (!rs || !data || !output || !output_len)
        return ATCP_ERR_INVALID_PARAM;

    int block_data = 255 - rs->nsym - 1; /* -1 for length prefix byte */
    int total_out = 0;
    int offset = 0;

    while (offset < data_len) {
        int chunk = data_len - offset;
        if (chunk > block_data) chunk = block_data;

        atcp_status_t st = atcp_rs_encode(rs, data + offset, chunk, output + total_out);
        if (st != ATCP_OK) return st;

        offset += chunk;
        total_out += 255;
    }

    *output_len = total_out;
    return ATCP_OK;
}

atcp_status_t atcp_rs_decode_blocks(const atcp_rs_t *rs, const uint8_t *coded, int coded_len,
                                uint8_t *output, int *output_len)
{
    if (!rs || !coded || !output || !output_len)
        return ATCP_ERR_INVALID_PARAM;

    if (coded_len % 255 != 0)
        return ATCP_ERR_INVALID_PARAM;

    int block_data = 255 - rs->nsym;
    int num_blocks = coded_len / 255;
    int total_out = 0;

    for (int i = 0; i < num_blocks; i++) {
        int dec_len;
        atcp_status_t st = atcp_rs_decode(rs, coded + i * 255, output + total_out, &dec_len);
        if (st != ATCP_OK) return st;
        total_out += dec_len;
    }

    *output_len = total_out;
    return ATCP_OK;
}
