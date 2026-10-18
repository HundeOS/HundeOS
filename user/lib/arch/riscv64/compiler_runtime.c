#include "compiler_runtime.h"

#include <stdbool.h>
#include <stdint.h>

#define DOUBLE_SIGN_MASK (UINT64_C(1) << 63)
#define DOUBLE_EXPONENT_MASK (UINT64_C(0x7ff) << 52)
#define DOUBLE_FRACTION_MASK ((UINT64_C(1) << 52) - 1)
#define DOUBLE_IMPLICIT_BIT (UINT64_C(1) << 52)
#define DOUBLE_QUIET_NAN (DOUBLE_EXPONENT_MASK | (UINT64_C(1) << 51))
#define DOUBLE_INFINITY DOUBLE_EXPONENT_MASK
#define DOUBLE_EXPONENT_SHIFT 52
#define DOUBLE_EXPONENT_BIAS 1023
#define DOUBLE_MAX_EXPONENT 0x7ff
#define DOUBLE_ROUND_BITS 3
#define DOUBLE_EXTENDED_IMPLICIT_BIT (DOUBLE_IMPLICIT_BIT << DOUBLE_ROUND_BITS)

typedef union DoubleRepresentation
{
    double value;
    uint64_t bits;
} DoubleRepresentation;

typedef struct DoubleParts
{
    bool sign;
    int exponent;
    uint64_t significand;
} DoubleParts;

static DoubleRepresentation DoubleRepresentation__new(double value)
{
    return (DoubleRepresentation){.value = value};
}

static DoubleRepresentation DoubleRepresentation__new_bits(uint64_t bits)
{
    return (DoubleRepresentation){.bits = bits};
}

static bool DoubleRepresentation__is_nan(DoubleRepresentation self)
{
    return (self.bits & DOUBLE_EXPONENT_MASK) == DOUBLE_EXPONENT_MASK && (self.bits & DOUBLE_FRACTION_MASK) != 0;
}

static bool DoubleRepresentation__is_infinite(DoubleRepresentation self)
{
    return (self.bits & ~DOUBLE_SIGN_MASK) == DOUBLE_INFINITY;
}

static bool DoubleRepresentation__is_zero(DoubleRepresentation self)
{
    return (self.bits & ~DOUBLE_SIGN_MASK) == 0;
}

static DoubleParts DoubleParts__new(bool sign, int exponent, uint64_t significand)
{
    return (DoubleParts){.sign = sign, .exponent = exponent, .significand = significand};
}

static DoubleParts DoubleRepresentation__parts(DoubleRepresentation self)
{
    int exponent = (int)((self.bits & DOUBLE_EXPONENT_MASK) >> DOUBLE_EXPONENT_SHIFT);
    uint64_t significand = self.bits & DOUBLE_FRACTION_MASK;
    if (exponent == 0)
    {
        exponent = 1;
    }
    else
    {
        significand |= DOUBLE_IMPLICIT_BIT;
    }
    return DoubleParts__new((self.bits & DOUBLE_SIGN_MASK) != 0, exponent, significand);
}

static uint64_t shift_right_with_sticky(uint64_t value, unsigned int distance)
{
    if (distance == 0)
    {
        return value;
    }
    if (distance >= 64)
    {
        return value != 0;
    }
    uint64_t discarded_mask = (UINT64_C(1) << distance) - 1;
    return (value >> distance) | ((value & discarded_mask) != 0);
}

static double pack_rounded(bool sign, int exponent, uint64_t extended_significand)
{
    if (extended_significand == 0)
    {
        return DoubleRepresentation__new_bits(sign ? DOUBLE_SIGN_MASK : 0).value;
    }
    if (exponent == 1 && extended_significand < DOUBLE_EXTENDED_IMPLICIT_BIT)
    {
        exponent = 0;
    }
    else if (exponent <= 0)
    {
        extended_significand = shift_right_with_sticky(extended_significand, (unsigned int)(1 - exponent));
        exponent = 0;
    }

    uint64_t significand = extended_significand >> DOUBLE_ROUND_BITS;
    uint64_t round_bits = extended_significand & ((UINT64_C(1) << DOUBLE_ROUND_BITS) - 1);
    if (round_bits > 4 || (round_bits == 4 && (significand & 1) != 0))
    {
        significand++;
    }
    if (significand >= (DOUBLE_IMPLICIT_BIT << 1))
    {
        significand >>= 1;
        exponent++;
    }
    if (exponent == 0 && significand >= DOUBLE_IMPLICIT_BIT)
    {
        exponent = 1;
    }
    if (exponent >= DOUBLE_MAX_EXPONENT)
    {
        return DoubleRepresentation__new_bits((sign ? DOUBLE_SIGN_MASK : 0) | DOUBLE_INFINITY).value;
    }

    uint64_t fraction = significand & DOUBLE_FRACTION_MASK;
    uint64_t bits = (sign ? DOUBLE_SIGN_MASK : 0) | ((uint64_t)exponent << DOUBLE_EXPONENT_SHIFT) | fraction;
    return DoubleRepresentation__new_bits(bits).value;
}

static double add_representations(DoubleRepresentation left, DoubleRepresentation right)
{
    if (DoubleRepresentation__is_nan(left) || DoubleRepresentation__is_nan(right))
    {
        return DoubleRepresentation__new_bits(DOUBLE_QUIET_NAN).value;
    }
    if (DoubleRepresentation__is_infinite(left))
    {
        if (DoubleRepresentation__is_infinite(right) && ((left.bits ^ right.bits) & DOUBLE_SIGN_MASK) != 0)
        {
            return DoubleRepresentation__new_bits(DOUBLE_QUIET_NAN).value;
        }
        return left.value;
    }
    if (DoubleRepresentation__is_infinite(right))
    {
        return right.value;
    }

    DoubleParts left_parts = DoubleRepresentation__parts(left);
    DoubleParts right_parts = DoubleRepresentation__parts(right);
    if (left_parts.exponent < right_parts.exponent ||
        (left_parts.exponent == right_parts.exponent && left_parts.significand < right_parts.significand))
    {
        DoubleParts temporary = left_parts;
        left_parts = right_parts;
        right_parts = temporary;
    }

    uint64_t left_significand = left_parts.significand << DOUBLE_ROUND_BITS;
    uint64_t right_significand = right_parts.significand << DOUBLE_ROUND_BITS;
    right_significand =
        shift_right_with_sticky(right_significand, (unsigned int)(left_parts.exponent - right_parts.exponent));

    int exponent = left_parts.exponent;
    uint64_t result_significand;
    if (left_parts.sign == right_parts.sign)
    {
        result_significand = left_significand + right_significand;
        if (result_significand >= (DOUBLE_EXTENDED_IMPLICIT_BIT << 1))
        {
            result_significand = shift_right_with_sticky(result_significand, 1);
            exponent++;
        }
    }
    else
    {
        result_significand = left_significand - right_significand;
        if (result_significand == 0)
        {
            return 0.0;
        }
        while (exponent > 1 && result_significand < DOUBLE_EXTENDED_IMPLICIT_BIT)
        {
            result_significand <<= 1;
            exponent--;
        }
    }
    return pack_rounded(left_parts.sign, exponent, result_significand);
}

static DoubleParts normalized_parts(DoubleRepresentation value)
{
    DoubleParts parts = DoubleRepresentation__parts(value);
    while (parts.significand < DOUBLE_IMPLICIT_BIT)
    {
        parts.significand <<= 1;
        parts.exponent--;
    }
    return parts;
}

static uint64_t multiply_significands(uint64_t left, uint64_t right, unsigned int shift)
{
    __uint128_t product = (__uint128_t)left * (__uint128_t)right;
    __uint128_t discarded_mask = (((__uint128_t)1) << shift) - 1;
    return (uint64_t)(product >> shift) | ((product & discarded_mask) != 0);
}

static double unsigned_integer_to_double(uint64_t magnitude, bool sign)
{
    if (magnitude == 0)
    {
        return 0.0;
    }

    unsigned int highest_bit = 0;
    for (uint64_t remaining = magnitude; remaining > 1; remaining >>= 1)
    {
        highest_bit++;
    }

    uint64_t significand;
    if (highest_bit <= DOUBLE_EXPONENT_SHIFT)
    {
        significand = magnitude << (DOUBLE_EXPONENT_SHIFT - highest_bit);
    }
    else
    {
        unsigned int shift = highest_bit - DOUBLE_EXPONENT_SHIFT;
        significand = magnitude >> shift;
        uint64_t remainder_mask = (UINT64_C(1) << shift) - 1;
        uint64_t remainder = magnitude & remainder_mask;
        uint64_t halfway = UINT64_C(1) << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (significand & 1) != 0))
        {
            significand++;
        }
        if (significand == (DOUBLE_IMPLICIT_BIT << 1))
        {
            significand >>= 1;
            highest_bit++;
        }
    }

    uint64_t bits = (sign ? DOUBLE_SIGN_MASK : 0) |
                    ((uint64_t)(highest_bit + DOUBLE_EXPONENT_BIAS) << DOUBLE_EXPONENT_SHIFT) |
                    (significand & DOUBLE_FRACTION_MASK);
    return DoubleRepresentation__new_bits(bits).value;
}

static int compare_representations(DoubleRepresentation left, DoubleRepresentation right, int unordered_result)
{
    if (DoubleRepresentation__is_nan(left) || DoubleRepresentation__is_nan(right))
    {
        return unordered_result;
    }
    if (DoubleRepresentation__is_zero(left) && DoubleRepresentation__is_zero(right))
    {
        return 0;
    }
    bool left_sign = (left.bits & DOUBLE_SIGN_MASK) != 0;
    bool right_sign = (right.bits & DOUBLE_SIGN_MASK) != 0;
    if (left_sign != right_sign)
    {
        return left_sign ? -1 : 1;
    }
    if (left.bits == right.bits)
    {
        return 0;
    }
    if (left_sign)
    {
        return left.bits > right.bits ? -1 : 1;
    }
    return left.bits < right.bits ? -1 : 1;
}

double __adddf3(double left, double right)
{
    return add_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right));
}

double __subdf3(double left, double right)
{
    DoubleRepresentation right_representation = DoubleRepresentation__new(right);
    right_representation.bits ^= DOUBLE_SIGN_MASK;
    return add_representations(DoubleRepresentation__new(left), right_representation);
}

double __muldf3(double left, double right)
{
    DoubleRepresentation left_representation = DoubleRepresentation__new(left);
    DoubleRepresentation right_representation = DoubleRepresentation__new(right);
    bool sign = ((left_representation.bits ^ right_representation.bits) & DOUBLE_SIGN_MASK) != 0;
    if (DoubleRepresentation__is_nan(left_representation) || DoubleRepresentation__is_nan(right_representation) ||
        (DoubleRepresentation__is_infinite(left_representation) &&
         DoubleRepresentation__is_zero(right_representation)) ||
        (DoubleRepresentation__is_zero(left_representation) && DoubleRepresentation__is_infinite(right_representation)))
    {
        return DoubleRepresentation__new_bits(DOUBLE_QUIET_NAN).value;
    }
    if (DoubleRepresentation__is_infinite(left_representation) ||
        DoubleRepresentation__is_infinite(right_representation))
    {
        return DoubleRepresentation__new_bits((sign ? DOUBLE_SIGN_MASK : 0) | DOUBLE_INFINITY).value;
    }
    if (DoubleRepresentation__is_zero(left_representation) || DoubleRepresentation__is_zero(right_representation))
    {
        return DoubleRepresentation__new_bits(sign ? DOUBLE_SIGN_MASK : 0).value;
    }

    DoubleParts left_parts = normalized_parts(left_representation);
    DoubleParts right_parts = normalized_parts(right_representation);
    __uint128_t product = (__uint128_t)left_parts.significand * (__uint128_t)right_parts.significand;
    bool upper_bit = (product & (((__uint128_t)1) << 105)) != 0;
    unsigned int shift = upper_bit ? 50 : 49;
    int exponent = left_parts.exponent + right_parts.exponent - DOUBLE_EXPONENT_BIAS + (upper_bit ? 1 : 0);
    uint64_t extended_significand = multiply_significands(left_parts.significand, right_parts.significand, shift);
    return pack_rounded(sign, exponent, extended_significand);
}

double __divdf3(double left, double right)
{
    DoubleRepresentation left_representation = DoubleRepresentation__new(left);
    DoubleRepresentation right_representation = DoubleRepresentation__new(right);
    bool sign = ((left_representation.bits ^ right_representation.bits) & DOUBLE_SIGN_MASK) != 0;
    if (DoubleRepresentation__is_nan(left_representation) || DoubleRepresentation__is_nan(right_representation) ||
        (DoubleRepresentation__is_infinite(left_representation) &&
         DoubleRepresentation__is_infinite(right_representation)) ||
        (DoubleRepresentation__is_zero(left_representation) && DoubleRepresentation__is_zero(right_representation)))
    {
        return DoubleRepresentation__new_bits(DOUBLE_QUIET_NAN).value;
    }
    if (DoubleRepresentation__is_infinite(left_representation) || DoubleRepresentation__is_zero(right_representation))
    {
        return DoubleRepresentation__new_bits((sign ? DOUBLE_SIGN_MASK : 0) | DOUBLE_INFINITY).value;
    }
    if (DoubleRepresentation__is_zero(left_representation) || DoubleRepresentation__is_infinite(right_representation))
    {
        return DoubleRepresentation__new_bits(sign ? DOUBLE_SIGN_MASK : 0).value;
    }

    DoubleParts left_parts = normalized_parts(left_representation);
    DoubleParts right_parts = normalized_parts(right_representation);
    int exponent = left_parts.exponent - right_parts.exponent + DOUBLE_EXPONENT_BIAS;
    uint64_t remainder = left_parts.significand;
    if (remainder < right_parts.significand)
    {
        remainder <<= 1;
        exponent--;
    }

    uint64_t extended_significand = 0;
    for (int bit = 55; bit >= 0; bit--)
    {
        if (remainder >= right_parts.significand)
        {
            extended_significand |= UINT64_C(1) << bit;
            remainder -= right_parts.significand;
        }
        if (bit != 0)
        {
            remainder <<= 1;
        }
    }
    if (remainder != 0)
    {
        extended_significand |= 1;
    }
    return pack_rounded(sign, exponent, extended_significand);
}

double __floatsidf(int32_t value)
{
    return __floatdidf(value);
}

double __floatunsidf(uint32_t value)
{
    return __floatundidf(value);
}

double __floatdidf(int64_t value)
{
    bool sign = value < 0;
    uint64_t magnitude = sign ? (~(uint64_t)value) + 1 : (uint64_t)value;
    return unsigned_integer_to_double(magnitude, sign);
}

double __floatundidf(uint64_t value)
{
    return unsigned_integer_to_double(value, false);
}

int __eqdf2(double left, double right)
{
    return compare_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right), 1);
}

int __ltdf2(double left, double right)
{
    return compare_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right), 1);
}

int __ledf2(double left, double right)
{
    return compare_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right), 1);
}

int __gtdf2(double left, double right)
{
    return compare_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right), -1);
}

int __gedf2(double left, double right)
{
    return compare_representations(DoubleRepresentation__new(left), DoubleRepresentation__new(right), -1);
}

int __unorddf2(double left, double right)
{
    return DoubleRepresentation__is_nan(DoubleRepresentation__new(left)) ||
           DoubleRepresentation__is_nan(DoubleRepresentation__new(right));
}
