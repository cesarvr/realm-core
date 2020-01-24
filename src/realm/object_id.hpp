/*************************************************************************
 *
 * Copyright 2019 Realm Inc.
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
 *
 **************************************************************************/

#ifndef REALM_OBJECT_ID_HPP
#define REALM_OBJECT_ID_HPP

#include <cstdint>
#include <cstring>
#include <realm/timestamp.hpp>

namespace realm {

class ObjectId {
public:
    /**
     * Constructs an ObjectId with all bytes 0x00.
     */
    ObjectId() noexcept;
    ObjectId(null) noexcept : ObjectId()
    {
    }

    /**
     * Constructs an ObjectId from 24 hex characters.
     */
    ObjectId(const char* init);

    ObjectId(Timestamp d, int machine_id = 0, int process_id = 0);

    /**
     * Generates a new ObjectId using the algorithm to attempt to avoid collisions.
     */
    static ObjectId gen();

    bool operator==(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) == 0;
    }
    bool operator!=(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) != 0;
    }
    bool operator>(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) > 0;
    }
    bool operator<(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) < 0;
    }
    bool operator>=(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) >= 0;
    }
    bool operator<=(const ObjectId& other) const
    {
        return memcmp(m_bytes, other.m_bytes, sizeof(m_bytes)) <= 0;
    }

    Timestamp get_timestamp() const;
    std::string to_string() const;

private:
    uint8_t m_bytes[12];
};

inline std::ostream& operator<<(std::ostream& ostr, const ObjectId& id)
{
    ostr << id.to_string();
    return ostr;
}

} // namespace realm

#endif /* REALM_OBJECT_ID_HPP */
