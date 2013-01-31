/*************************************************************************
 *
 * TIGHTDB CONFIDENTIAL
 * __________________
 *
 *  [2011] - [2012] TightDB Inc
 *  All Rights Reserved.
 *
 * NOTICE:  All information contained herein is, and remains
 * the property of TightDB Incorporated and its suppliers,
 * if any.  The intellectual and technical concepts contained
 * herein are proprietary to TightDB Incorporated
 * and its suppliers and may be covered by U.S. and Foreign Patents,
 * patents in process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material
 * is strictly forbidden unless prior written permission is obtained
 * from TightDB Incorporated.
 *
 **************************************************************************/

namespace tightdb {

inline ColumnMixed::ColumnMixed(): m_data(0)
{
    Create(Allocator::get_default(), 0, 0);
}

inline ColumnMixed::ColumnMixed(Allocator& alloc, const Table* table, std::size_t column_ndx):
    m_data(0)
{
    Create(alloc, table, column_ndx);
}

inline ColumnMixed::ColumnMixed(Allocator& alloc, const Table* table, std::size_t column_ndx,
                                ArrayParent* parent, std::size_t ndx_in_parent, std::size_t ref):
    m_data(0)
{
    Create(alloc, table, column_ndx, parent, ndx_in_parent, ref);
}

inline size_t ColumnMixed::get_subtable_size(size_t row_idx) const TIGHTDB_NOEXCEPT
{
    // FIXME: If the table object is cached, it is possible to get the
    // size from it. Maybe it is faster in general to check for the
    // the presence of the cached object and use it when available.
    TIGHTDB_ASSERT(row_idx < m_types->Size());
    if (m_types->Get(row_idx) != COLUMN_TYPE_TABLE) return 0;
    const size_t top_ref = m_refs->GetAsRef(row_idx);
    const size_t columns_ref = Array(top_ref, 0, 0, m_refs->GetAllocator()).GetAsRef(1);
    const Array columns(columns_ref, 0, 0, m_refs->GetAllocator());
    if (columns.is_empty()) return 0;
    const size_t first_col_ref = columns.GetAsRef(0);
    return get_size_from_ref(first_col_ref, m_refs->GetAllocator());
}

inline Table* ColumnMixed::get_subtable_ptr(size_t row_idx) const
{
    TIGHTDB_ASSERT(row_idx < m_types->Size());
    if (m_types->Get(row_idx) != COLUMN_TYPE_TABLE) 
        return 0;
    return m_refs->get_subtable_ptr(row_idx);
}

inline void ColumnMixed::invalidate_subtables()
{
    m_refs->invalidate_subtables();
}

inline void ColumnMixed::invalidate_subtables_virtual()
{
    invalidate_subtables();
}


//
// Getters
//

#define TIGHTDB_BIT63 0x8000000000000000    

inline int64_t ColumnMixed::get_value(size_t ndx) const
{
    TIGHTDB_ASSERT(ndx < m_types->Size());

    // Shift the unsigned value right - ensuring 0 gets in from left.
    // Shifting signed integers right doesn't ensure 0's.
    const uint64_t value = static_cast<uint64_t>(m_refs->Get(ndx)) >> 1;
    return static_cast<int64_t>(value);
}

inline int64_t ColumnMixed::get_int(size_t ndx) const
{
    // Get first 63 bits of the integer value
    int64_t value = get_value(ndx);

    // restore 'sign'-bit from the column-type
    const MixedColType coltype = static_cast<MixedColType>(m_types->Get(ndx));
    if (coltype == MIXED_COL_INT_NEG)
        value |= TIGHTDB_BIT63; // set sign bit (63)
    else {
        TIGHTDB_ASSERT(coltype == MIXED_COL_INT);
    }
    return value;
}

inline bool ColumnMixed::get_bool(size_t ndx) const
{
    TIGHTDB_ASSERT(m_types->Get(ndx) == MIXED_COL_BOOL);

    return (get_value(ndx) != 0);
}

inline time_t ColumnMixed::get_date(size_t ndx) const
{
    TIGHTDB_ASSERT(m_types->Get(ndx) == MIXED_COL_DATE);

    return static_cast<time_t>(get_value(ndx));
}

inline float ColumnMixed::get_float(size_t ndx) const
{
    TIGHTDB_STATIC_ASSERT(std::numeric_limits<float>::is_iec559, "'float' is not IEEE");
    TIGHTDB_STATIC_ASSERT((sizeof(float) * CHAR_BIT == 32), "Assume 32 bit float.");
    TIGHTDB_ASSERT(m_types->Get(ndx) == MIXED_COL_FLOAT);

    return TypePunning<float>( get_value(ndx) );
}

inline double ColumnMixed::get_double(size_t ndx) const
{
    TIGHTDB_STATIC_ASSERT(std::numeric_limits<double>::is_iec559, "'double' is not IEEE");
    TIGHTDB_STATIC_ASSERT((sizeof(double) * CHAR_BIT == 64), "Assume 64 bit double.");

    int64_t intval = get_value(ndx);

    // restore 'sign'-bit from the column-type
    const MixedColType coltype = static_cast<MixedColType>(m_types->Get(ndx));
    if (coltype == MIXED_COL_DOUBLE_NEG)
        intval |= TIGHTDB_BIT63; // set sign bit (63)
    else {
        TIGHTDB_ASSERT(coltype == MIXED_COL_DOUBLE);
    }
    return TypePunning<double>( intval );
}

inline const char* ColumnMixed::get_string(size_t ndx) const
{
    TIGHTDB_ASSERT(ndx < m_types->Size());
    TIGHTDB_ASSERT(m_types->Get(ndx) == MIXED_COL_STRING);
    TIGHTDB_ASSERT(m_data);

    const size_t ref = m_refs->GetAsRef(ndx) >> 1;
    const char* value = static_cast<const char*>(m_data->GetData(ref));
    return value;
}

inline BinaryData ColumnMixed::get_binary(size_t ndx) const
{
    TIGHTDB_ASSERT(ndx < m_types->Size());
    TIGHTDB_ASSERT(m_types->Get(ndx) == MIXED_COL_BINARY);
    TIGHTDB_ASSERT(m_data);

    const size_t ref = m_refs->GetAsRef(ndx) >> 1;
    return m_data->Get(ref);
}

//
// Setters
//

// Set a int64 value. 
// Store 63 bit of the value in m_refs. Store sign bit in m_types.

inline void ColumnMixed::set_int64(size_t ndx, int64_t value, MixedColType pos_type, MixedColType neg_type)
{
    TIGHTDB_ASSERT(ndx < m_types->Size());

    // If sign-bit is set in value, 'store' it in the column-type
    const MixedColType coltype = ((value & TIGHTDB_BIT63) == 0) ? pos_type : neg_type;
    
    // Remove refs or binary data (sets type to double)
    clear_value(ndx, coltype);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    value = (value << 1) + 1;
    m_refs->Set(ndx, value);
}

inline void ColumnMixed::set_int(size_t ndx, int64_t value)
{
    set_int64(ndx, value, MIXED_COL_INT, MIXED_COL_INT_NEG);
}

inline void ColumnMixed::set_double(size_t ndx, double value)
{
    const int64_t val64 = TypePunning<int64_t>( value );
    set_int64(ndx, val64, MIXED_COL_DOUBLE, MIXED_COL_DOUBLE_NEG);
}

inline void ColumnMixed::set_value(size_t ndx, int64_t value, MixedColType coltype)
{
    TIGHTDB_ASSERT(ndx < m_types->Size());

    // Remove refs or binary data (sets type to float)
    clear_value(ndx, coltype);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t v = (value << 1) + 1;
    m_refs->Set(ndx, v);
}

inline void ColumnMixed::set_float(size_t ndx, float value)
{
    const void* vptr = reinterpret_cast<const void*>(&value);
    const int32_t val32 = * reinterpret_cast<const int32_t*>(vptr);
    set_value(ndx, static_cast<const int64_t>(val32), MIXED_COL_FLOAT);
}

inline void ColumnMixed::set_bool(size_t ndx, bool value)
{
    set_value(ndx, (value ? 1 : 0), MIXED_COL_BOOL);
}

inline void ColumnMixed::set_date(size_t ndx, time_t value)
{
    set_value(ndx, static_cast<const int64_t>(value), MIXED_COL_DATE);
}

// FIXME: Check that callers test the return value
inline bool ColumnMixed::set_subtable(size_t ndx)
{
    TIGHTDB_ASSERT(ndx < m_types->Size());
    const size_t ref = Table::create_empty_table(m_array->GetAllocator());
    if (!ref) 
        return false;
    // FIXME: Could the following operations also fail on allocation?
    clear_value(ndx, MIXED_COL_TABLE); // Remove any previous refs or binary data
    m_refs->Set(ndx, ref);
    return true;
}

//
// Inserts
//

// Insert a int64 value. 
// Store 63 bit of the value in m_refs. Store sign bit in m_types.

inline void ColumnMixed::insert_int64(size_t ndx, int64_t value, MixedColType pos_type, MixedColType neg_type)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());

    // 'store' the sign-bit in the integer-type
    if ((value & TIGHTDB_BIT63) == 0)
        m_types->Insert(ndx, pos_type);
    else
        m_types->Insert(ndx, neg_type);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    value = (value << 1) + 1;
    m_refs->Insert(ndx, value);
}

inline void ColumnMixed::insert_int(size_t ndx, int64_t value)
{
    insert_int64(ndx, value, MIXED_COL_INT, MIXED_COL_INT_NEG);
}

inline void ColumnMixed::insert_double(size_t ndx, double value)
{
    int64_t val64 = TypePunning<int64_t>( value );
    insert_int64(ndx, val64, MIXED_COL_DOUBLE, MIXED_COL_DOUBLE_NEG);
}

inline void ColumnMixed::insert_float(size_t ndx, float value)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());

    // Convert to int32_t first, to ensure we only access 32 bits from the float.
    const int32_t val32 = TypePunning<int32_t>( value );    

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t val64 = (static_cast<int64_t>(val32) << 1) + 1;
    m_refs->Insert(ndx, val64);
    m_types->Insert(ndx, MIXED_COL_FLOAT);
}

inline void ColumnMixed::insert_bool(size_t ndx, bool value)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t v = ((value ? 1 : 0) << 1) + 1;

    m_types->Insert(ndx, MIXED_COL_BOOL);
    m_refs->Insert(ndx, v);
}

inline void ColumnMixed::insert_date(size_t ndx, time_t value)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t v = (value << 1) + 1;

    m_types->Insert(ndx, MIXED_COL_DATE);
    m_refs->Insert(ndx, v);
}

inline void ColumnMixed::insert_string(size_t ndx, const char* value)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());
    InitDataColumn();

    const size_t len = strlen(value)+1;
    const size_t ref = m_data->Size();
    m_data->add(value, len);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t v = (ref << 1) + 1;

    m_types->Insert(ndx, MIXED_COL_STRING);
    m_refs->Insert(ndx, v);
}

inline void ColumnMixed::insert_binary(size_t ndx, const char* value, size_t len)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());
    InitDataColumn();

    const size_t ref = m_data->Size();
    m_data->add(value, len);

    // Shift value one bit and set lowest bit to indicate that this is not a ref
    const int64_t v = (ref << 1) + 1;

    m_types->Insert(ndx, MIXED_COL_BINARY);
    m_refs->Insert(ndx, v);
}

// FIXME: Check that callers test the return value
inline bool ColumnMixed::insert_subtable(size_t ndx)
{
    TIGHTDB_ASSERT(ndx <= m_types->Size());
    const size_t ref = Table::create_empty_table(m_array->GetAllocator());
    if (!ref) return false;
    // FIXME: These inserts can also fail on allocation
    m_types->Insert(ndx, MIXED_COL_TABLE);
    m_refs->Insert(ndx, ref);
    return true;
}

} // namespace tightdb