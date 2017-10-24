#ifndef VTZERO_GEOMETRY_HPP
#define VTZERO_GEOMETRY_HPP

/*****************************************************************************

vtzero - Minimalistic vector tile decoder and encoder in C++.

This file is from https://github.com/mapbox/vtzero where you can find more
documentation.

*****************************************************************************/

/**
 * @file geometry.hpp
 *
 * @brief Contains classes and functions related to geometry handling.
 */

#include "exception.hpp"
#include "types.hpp"

#include <protozero/pbf_reader.hpp>

#include <cstdint>
#include <iosfwd>

namespace vtzero {

    /// A simple point class
    struct point {

        /// X coordinate
        int32_t x = 0;

        /// Y coordinate
        int32_t y = 0;

        /// Default construct to 0 coordinates
        constexpr point() noexcept = default;

        /// Constructor
        constexpr point(int32_t x_, int32_t y_) noexcept :
            x(x_),
            y(y_) {
        }

    }; // struct point

    /// Overload of the << operator for points
    template <typename TChar, typename TTraits>
    std::basic_ostream<TChar, TTraits>& operator<<(std::basic_ostream<TChar, TTraits>& out, point p) {
        return out << '(' << p.x << ',' << p.y << ')';
    }

    /// Helper function to create a point from any type that has members x and y
    template <typename T>
    point create_point(T p) noexcept {
        return {p.x, p.y};
    }

    /// Points are equal if their coordinates are
    inline constexpr bool operator==(const point a, const point b) noexcept {
        return a.x == b.x && a.y == b.y;
    }

    /// Points are not equal if their coordinates aren't
    inline constexpr bool operator!=(const point a, const point b) noexcept {
        return !(a==b);
    }

    namespace detail {

        inline constexpr uint32_t command_integer(uint32_t id, uint32_t count) noexcept {
            return (id & 0x7) | (count << 3);
        }

        inline constexpr uint32_t command_move_to(uint32_t count = 0) noexcept {
            return command_integer(1, count);
        }

        inline constexpr uint32_t command_line_to(uint32_t count = 0) noexcept {
            return command_integer(2, count);
        }

        inline constexpr uint32_t command_close_path(uint32_t count = 0) noexcept {
            return command_integer(7, count);
        }

        inline constexpr uint32_t get_command_id(uint32_t command_integer) noexcept {
            return command_integer & 0x7;
        }

        inline constexpr uint32_t get_command_count(uint32_t command_integer) noexcept {
            return command_integer >> 3;
        }

        inline constexpr int64_t det(const point a, const point b) noexcept {
            return static_cast<int64_t>(a.x) * static_cast<int64_t>(b.y) -
                   static_cast<int64_t>(b.x) * static_cast<int64_t>(a.y);
        }

        /**
         * Decode a geometry as specified in spec 4.3 from a sequence of 32 bit
         * unsigned integers. This templated base class can be instantiated
         * with a different iterator type for testing than for normal use.
         */
        template <typename T>
        class basic_geometry_decoder {

            using iterator_type = T;

            iterator_type m_it;
            iterator_type m_end;

            point m_cursor{0, 0};
            uint32_t m_command_id = 0;
            uint32_t m_count = 0;
            bool m_strict = true;

        public:

            explicit basic_geometry_decoder(T begin, T end, bool strict = true) :
                m_it(begin),
                m_end(end),
                m_strict(strict) {
            }

            uint32_t count() const noexcept {
                return m_count;
            }

            bool done() const noexcept {
                return m_it == m_end;
            }

            bool next_command(const uint32_t expected_command) {
                vtzero_assert(m_count == 0);

                if (m_it == m_end) {
                    return false;
                }

                m_command_id = detail::get_command_id(*m_it);
                if (m_command_id == command_close_path()) {
                    // spec 4.3.3.3 "A ClosePath command MUST have a command count of 1"
                    if (detail::get_command_count(*m_it) != 1) {
                        throw geometry_exception{"ClosePath command count is not 1"};
                    }
                } else {
                    m_count = detail::get_command_count(*m_it);
                }

                if (m_command_id != expected_command) {
                    throw geometry_exception{std::string{"expected command "} +
                                             std::to_string(expected_command) +
                                             " but got " +
                                             std::to_string(m_command_id)};
                }

                ++m_it;

                return true;
            }

            point next_point() {
                vtzero_assert(m_count > 0);

                if (m_it == m_end || std::next(m_it) == m_end) {
                    throw geometry_exception{"too few points in geometry"};
                }

                // spec 4.3.2 "A ParameterInteger is zigzag encoded"
                const int32_t x = protozero::decode_zigzag32(*m_it++);
                const int32_t y = protozero::decode_zigzag32(*m_it++);

                // spec 4.3.3.2 "For any pair of (dX, dY) the dX and dY MUST NOT both be 0."
                if (m_strict && m_command_id == command_line_to() && x == 0 && y == 0) {
                    throw geometry_exception{"found consecutive equal points (spec 4.3.3.2) (strict mode)"};
                }

                m_cursor.x += x;
                m_cursor.y += y;

                --m_count;

                return m_cursor;
            }

        }; // class basic_geometry_decoder

        class geometry_decoder : public basic_geometry_decoder<protozero::pbf_reader::const_uint32_iterator> {

        public:

            explicit geometry_decoder(const data_view data, bool strict = true) :
                basic_geometry_decoder({data.data(), data.data() + data.size()},
                                       {data.data() + data.size(), data.data() + data.size()},
                                       strict) {
            }

        }; // class geometry_decoder

    } // namespace detail

    template <typename TGeomHandler>
    void decode_point_geometry(const geometry geometry, bool strict, TGeomHandler&& geom_handler) {
        vtzero_assert(geometry.type() == GeomType::POINT);
        detail::geometry_decoder decoder{geometry.data(), strict};

        // spec 4.3.4.2 "MUST consist of a single MoveTo command"
        if (!decoder.next_command(detail::command_move_to())) {
            throw geometry_exception{"expected MoveTo command (spec 4.3.4.2)"};
        }

        // spec 4.3.4.2 "command count greater than 0"
        if (decoder.count() == 0) {
            throw geometry_exception{"MoveTo command count is zero (spec 4.3.4.2)"};
        }

        std::forward<TGeomHandler>(geom_handler).points_begin(decoder.count());
        while (decoder.count() > 0) {
            std::forward<TGeomHandler>(geom_handler).points_point(decoder.next_point());
        }

        // spec 4.3.4.2 "MUST consist of of a single ... command"
        if (!decoder.done()) {
            throw geometry_exception{"additional data after end of geometry (spec 4.3.4.2)"};
        }

        std::forward<TGeomHandler>(geom_handler).points_end();
    }

    template <typename TGeomHandler>
    void decode_linestring_geometry(const geometry geometry, bool strict, TGeomHandler&& geom_handler) {
        vtzero_assert(geometry.type() == GeomType::LINESTRING);
        detail::geometry_decoder decoder{geometry.data(), strict};

        // spec 4.3.4.3 "1. A MoveTo command"
        while (decoder.next_command(detail::command_move_to())) {
            // spec 4.3.4.3 "with a command count of 1"
            if (decoder.count() != 1) {
                throw geometry_exception{"MoveTo command count is not 1 (spec 4.3.4.3)"};
            }

            const auto first_point = decoder.next_point();

            // spec 4.3.4.3 "2. A LineTo command"
            if (!decoder.next_command(detail::command_line_to())) {
                throw geometry_exception{"expected LineTo command (spec 4.3.4.3)"};
            }

            // spec 4.3.4.3 "with a command count greater than 0"
            if (decoder.count() == 0) {
                throw geometry_exception{"LineTo command count is zero (spec 4.3.4.3)"};
            }

            std::forward<TGeomHandler>(geom_handler).linestring_begin(decoder.count() + 1);
            std::forward<TGeomHandler>(geom_handler).linestring_point(first_point);
            while (decoder.count() > 0) {
                std::forward<TGeomHandler>(geom_handler).linestring_point(decoder.next_point());
            }

            std::forward<TGeomHandler>(geom_handler).linestring_end();
        }
    }

    template <typename TGeomHandler>
    void decode_polygon_geometry(const geometry geometry, bool strict, TGeomHandler&& geom_handler) {
        vtzero_assert(geometry.type() == GeomType::POLYGON);
        detail::geometry_decoder decoder{geometry.data(), strict};

        // spec 4.3.4.4 "1. A MoveTo command"
        while (decoder.next_command(detail::command_move_to())) {
            // spec 4.3.4.4 "with a command count of 1"
            if (decoder.count() != 1) {
                throw geometry_exception{"MoveTo command count is not 1 (spec 4.3.4.4)"};
            }

            point start_point{decoder.next_point()};
            int64_t sum = 0;
            point last_point = start_point;

            // spec 4.3.4.4 "2. A LineTo command"
            if (!decoder.next_command(detail::command_line_to())) {
                throw geometry_exception{"expected LineTo command (spec 4.3.4.4)"};
            }

            // spec 4.3.4.4 "with a command count greater than 1"
            if (strict && decoder.count() <= 1) {
                throw geometry_exception{"LineTo command count is not greater than 1 (spec 4.3.4.4)"};
            }

            std::forward<TGeomHandler>(geom_handler).ring_begin(decoder.count() + 2);
            std::forward<TGeomHandler>(geom_handler).ring_point(start_point);

            while (decoder.count() > 0) {
                point p = decoder.next_point();
                sum += detail::det(last_point, p);
                last_point = p;
                std::forward<TGeomHandler>(geom_handler).ring_point(p);
            }

            // spec 4.3.4.4 "3. A ClosePath command"
            if (!decoder.next_command(detail::command_close_path())) {
                throw geometry_exception{"expected ClosePath command (4.3.4.4)"};
            }

            sum += detail::det(last_point, start_point);
            std::forward<TGeomHandler>(geom_handler).ring_point(start_point);

            std::forward<TGeomHandler>(geom_handler).ring_end(sum > 0);
        }
    }

} // namespace vtzero

#endif // VTZERO_GEOMETRY_HPP
