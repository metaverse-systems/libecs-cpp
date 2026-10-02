#pragma once
#include <cstdint>
#include <string>

namespace ecs
{
    /**
     * A 128-bit random identifier in the standard version 4 text form
     * (for example "550e8400-e29b-41d4-a716-446655440000").
     *
     * The layout is the same in every build: two 64-bit halves, 16 bytes in total.
     */
    class Uuid
    {
      public:
        /**
         * Generates a new random identifier.
         *
         * Safe to call from any number of threads at once. Each thread has its own
         * independently seeded generator, so no lock is taken. The identifiers are
         * unique in practice but are not cryptographically unpredictable; do not
         * use them as secrets.
         */
        Uuid();

        /**
         * Parses the canonical 36-character text form, in either letter case and
         * of any version. The text is not trimmed.
         *
         * @param id the text to parse
         * @throws std::runtime_error if the text is empty, is not 36 characters
         *         long, or has a character that does not belong at its position
         */
        Uuid(const std::string &id);

        /**
         * Returns the canonical lowercase 36-character text form.
         */
        std::string Get() const;

      private:
        std::uint64_t high = 0;
        std::uint64_t low = 0;
    };
}
