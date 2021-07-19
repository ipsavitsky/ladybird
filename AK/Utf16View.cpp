/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <AK/StringView.h>
#include <AK/Utf16View.h>
#include <AK/Utf8View.h>

namespace AK {

static constexpr u16 high_surrogate_min = 0xd800;
static constexpr u16 high_surrogate_max = 0xdbff;
static constexpr u16 low_surrogate_min = 0xdc00;
static constexpr u16 low_surrogate_max = 0xdfff;
static constexpr u32 replacement_code_point = 0xfffd;
static constexpr u32 first_supplementary_plane_code_point = 0x10000;

Vector<u16> utf8_to_utf16(StringView const& utf8_view)
{
    return utf8_to_utf16(Utf8View { utf8_view });
}

Vector<u16> utf8_to_utf16(Utf8View const& utf8_view)
{
    Vector<u16> utf16_data;

    for (auto code_point : utf8_view) {
        if (code_point < first_supplementary_plane_code_point) {
            utf16_data.append(static_cast<u16>(code_point));
        } else {
            code_point -= first_supplementary_plane_code_point;
            utf16_data.append(static_cast<u16>(high_surrogate_min | (code_point >> 10)));
            utf16_data.append(static_cast<u16>(low_surrogate_min | (code_point & 0x3ff)));
        }
    }

    return utf16_data;
}

bool Utf16View::is_high_surrogate(u16 code_unit)
{
    return (code_unit >= high_surrogate_min) && (code_unit <= high_surrogate_max);
}

bool Utf16View::is_low_surrogate(u16 code_unit)
{
    return (code_unit >= low_surrogate_min) && (code_unit <= low_surrogate_max);
}

u32 Utf16View::decode_surrogate_pair(u16 high_surrogate, u16 low_surrogate)
{
    VERIFY(is_high_surrogate(high_surrogate));
    VERIFY(is_low_surrogate(low_surrogate));

    return ((high_surrogate - high_surrogate_min) << 10) + (low_surrogate - low_surrogate_min) + first_supplementary_plane_code_point;
}

String Utf16View::to_utf8(AllowInvalidCodeUnits allow_invalid_code_units) const
{
    StringBuilder builder;

    if (allow_invalid_code_units == AllowInvalidCodeUnits::Yes) {
        for (auto const* ptr = begin_ptr(); ptr < end_ptr(); ++ptr) {
            if (is_high_surrogate(*ptr)) {
                auto const* next = ptr + 1;

                if ((next < end_ptr()) && is_low_surrogate(*next)) {
                    auto code_point = decode_surrogate_pair(*ptr, *next);
                    builder.append_code_point(code_point);
                    ++ptr;
                    continue;
                }
            }

            builder.append_code_point(static_cast<u32>(*ptr));
        }
    } else {
        for (auto code_point : *this)
            builder.append_code_point(code_point);
    }

    return builder.build();
}

size_t Utf16View::length_in_code_points() const
{
    if (!m_length_in_code_points.has_value())
        m_length_in_code_points = calculate_length_in_code_points();
    return *m_length_in_code_points;
}

u16 Utf16View::code_unit_at(size_t index) const
{
    VERIFY(index < length_in_code_units());
    return m_code_units[index];
}

Utf16View Utf16View::substring_view(size_t code_unit_offset, size_t code_unit_length) const
{
    VERIFY(!Checked<size_t>::addition_would_overflow(code_unit_offset, code_unit_length));
    VERIFY(code_unit_offset + code_unit_length <= length_in_code_units());

    return Utf16View { m_code_units.slice(code_unit_offset, code_unit_length) };
}

bool Utf16View::validate(size_t& valid_code_units) const
{
    valid_code_units = 0;

    for (auto const* ptr = begin_ptr(); ptr < end_ptr(); ++ptr) {
        if (is_high_surrogate(*ptr)) {
            if ((++ptr >= end_ptr()) || !is_low_surrogate(*ptr))
                return false;
            ++valid_code_units;
        } else if (is_low_surrogate(*ptr)) {
            return false;
        }

        ++valid_code_units;
    }

    return true;
}

size_t Utf16View::calculate_length_in_code_points() const
{
    size_t code_points = 0;
    for ([[maybe_unused]] auto code_point : *this)
        ++code_points;
    return code_points;
}

bool Utf16View::operator==(Utf16View const& other) const
{
    if (length_in_code_units() == 0)
        return other.length_in_code_units() == 0;
    if (length_in_code_units() != other.length_in_code_units())
        return false;

    for (size_t i = 0; i < length_in_code_units(); ++i) {
        if (m_code_units[i] != other.m_code_units[i])
            return false;
    }

    return true;
}

Utf16CodePointIterator& Utf16CodePointIterator::operator++()
{
    size_t code_units = length_in_code_units();

    if (code_units > m_remaining_code_units) {
        // If there aren't enough code units remaining, skip to the end.
        m_ptr += m_remaining_code_units;
        m_remaining_code_units = 0;
    } else {
        m_ptr += code_units;
        m_remaining_code_units -= code_units;
    }

    return *this;
}

u32 Utf16CodePointIterator::operator*() const
{
    VERIFY(m_remaining_code_units > 0);

    if (Utf16View::is_high_surrogate(*m_ptr)) {
        if ((m_remaining_code_units > 1) && Utf16View::is_low_surrogate(*(m_ptr + 1)))
            return Utf16View::decode_surrogate_pair(*m_ptr, *(m_ptr + 1));
        return replacement_code_point;
    } else if (Utf16View::is_low_surrogate(*m_ptr)) {
        return replacement_code_point;
    }

    return static_cast<u32>(*m_ptr);
}

size_t Utf16CodePointIterator::length_in_code_units() const
{
    VERIFY(m_remaining_code_units > 0);

    if (Utf16View::is_high_surrogate(*m_ptr)) {
        if ((m_remaining_code_units > 1) && Utf16View::is_low_surrogate(*(m_ptr + 1)))
            return 2;
    }

    // If this return is reached, either the encoded code point is a valid single code unit, or that
    // code point is invalid (e.g. began with a low surrogate, or a low surrogate did not follow a
    // high surrogate). In the latter case, a single replacement code unit will be used.
    return 1;
}

}
