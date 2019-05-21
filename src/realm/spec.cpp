/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
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

#include <realm/impl/destroy_guard.hpp>
#include <realm/spec.hpp>
#include <realm/replication.hpp>
#include <realm/util/to_string.hpp>
#include <realm/group.hpp>
using namespace realm;

Spec::~Spec() noexcept
{
}

void Spec::detach() noexcept
{
    m_top.detach();
}

bool Spec::init(ref_type ref) noexcept
{
    MemRef mem(ref, get_alloc());
    init(mem);
    return true;
}

void Spec::init(MemRef mem) noexcept
{
    m_top.init_from_mem(mem);
    size_t top_size = m_top.size();
    REALM_ASSERT(top_size > 2 && top_size <= 6);

    m_types.init_from_ref(m_top.get_as_ref(0));
    m_types.set_parent(&m_top, 0);
    m_names.init_from_ref(m_top.get_as_ref(1));
    m_names.set_parent(&m_top, 1);
    m_attr.init_from_ref(m_top.get_as_ref(2));
    m_attr.set_parent(&m_top, 2);

    // Reset optional subarrays in the case of moving
    // from initialized children to uninitialized
    m_subspecs.detach();
    m_enumkeys.detach();

    // Subspecs array is only there and valid when there are subtables
    // if there are enumkey, but no subtables yet it will be a zero-ref
    if (has_subspec()) {
        ref_type ref = m_top.get_as_ref(3);
        m_subspecs.init_from_ref(ref);
        m_subspecs.set_parent(&m_top, 3);
    }

    // Enumkeys array is only there when there are StringEnum columns
    if ((m_top.size() > 4) && (m_top.get_as_ref(4) != 0)) {
        m_enumkeys.init_from_ref(m_top.get_as_ref(4));
        m_enumkeys.set_parent(&m_top, 4);
    }

    while (m_top.size() < 6) {
        m_top.add(0);
    }

    m_keys.set_parent(&m_top, 5);
    if (m_top.get_as_ref(5) == 0) {
        // This is an upgrade - create column key array
        MemRef mem_ref = Array::create_empty_array(Array::type_Normal, false, m_top.get_alloc()); // Throws
        m_keys.init_from_mem(mem_ref);
        m_keys.update_parent();
        size_t num_cols = m_types.size();
        for (size_t i = 0; i < num_cols; i++) {
            m_keys.add(i);
        }
    }
    else {
        m_keys.init_from_parent();
    }


    update_internals();
}

void Spec::update_internals() noexcept
{
    m_has_strong_link_columns = false;
    m_num_public_columns = 0;
    size_t n = m_types.size();
    for (size_t i = 0; i < n; ++i) {
        if (ColumnAttr(m_attr.get(i)) & col_attr_StrongLinks) {
            m_has_strong_link_columns = true;
        }
        if (m_types.get(i) == col_type_BackLink) {
            // Now we have no more public columns
            return;
        }
        m_num_public_columns++;
    }
}

bool Spec::update_from_parent(size_t old_baseline) noexcept
{
    if (!m_top.update_from_parent(old_baseline))
        return false;

    m_types.update_from_parent(old_baseline);
    m_names.update_from_parent(old_baseline);
    m_attr.update_from_parent(old_baseline);

    if (m_top.get_as_ref(3) != 0) {
        m_subspecs.update_from_parent(old_baseline);
    }
    else {
        m_subspecs.detach();
    }

    if (m_top.get_as_ref(4) != 0) {
        m_enumkeys.update_from_parent(old_baseline);
    }
    else {
        m_enumkeys.detach();
    }

    m_keys.update_from_parent(old_baseline);

    update_internals();

    return true;
}


MemRef Spec::create_empty_spec(Allocator& alloc)
{
    // The 'spec_set' contains the specification (types and names) of
    // all columns and sub-tables
    Array spec_set(alloc);
    _impl::DeepArrayDestroyGuard dg(&spec_set);
    spec_set.create(Array::type_HasRefs); // Throws

    _impl::DeepArrayRefDestroyGuard dg_2(alloc);
    {
        // One type for each column
        bool context_flag = false;
        MemRef mem = Array::create_empty_array(Array::type_Normal, context_flag, alloc); // Throws
        dg_2.reset(mem.get_ref());
        int_fast64_t v(from_ref(mem.get_ref()));
        spec_set.add(v); // Throws
        dg_2.release();
    }
    {
        size_t size = 0;
        // One name for each column
        MemRef mem = ArrayStringShort::create_array(size, alloc); // Throws
        dg_2.reset(mem.get_ref());
        int_fast64_t v = from_ref(mem.get_ref());
        spec_set.add(v); // Throws
        dg_2.release();
    }
    {
        // One attrib set for each column
        bool context_flag = false;
        MemRef mem = Array::create_empty_array(Array::type_Normal, context_flag, alloc); // Throws
        dg_2.reset(mem.get_ref());
        int_fast64_t v = from_ref(mem.get_ref());
        spec_set.add(v); // Throws
        dg_2.release();
    }
    spec_set.add(0); // Subspecs array
    spec_set.add(0); // Enumkeys array
    {
        // One key for each column
        bool context_flag = false;
        MemRef mem = Array::create_empty_array(Array::type_Normal, context_flag, alloc); // Throws
        dg_2.reset(mem.get_ref());
        int_fast64_t v = from_ref(mem.get_ref());
        spec_set.add(v); // Throws
        dg_2.release();
    }

    dg.release();
    return spec_set.get_mem();
}

ColKey Spec::generate_converted_colkey(size_t column_ndx)
{
    auto attr = get_column_attr(column_ndx);
    // index and uniqueness are not passed on to the key, so clear them
    attr.reset(col_attr_Indexed);
    attr.reset(col_attr_Unique);
    auto type = get_column_type(column_ndx);
    // columns get the same leaf index as in the spec during conversion.
    ColKey tmp(ColKey::Idx{static_cast<unsigned>(column_ndx)}, type, attr, 0);
    // simply reuse the lower bits of the tmp ColKey (+1) as tag for the real one
    return ColKey(ColKey::Idx{static_cast<unsigned>(column_ndx)}, type, attr, unsigned(tmp.value) + 1);
}

bool Spec::convert_column(size_t column_ndx)
{
    bool changes = false;
    if (column_ndx < m_num_public_columns) {
        StringData name = m_names.get(column_ndx);
        if (name.size() == 0) {
            auto new_name = std::string("col_") + util::to_string(column_ndx);
            m_names.set(column_ndx, new_name);
            changes = true;
        }
        else if (m_names.find_first(name) != column_ndx) {
            auto new_name = std::string(name.data()) + '_' + util::to_string(column_ndx);
            m_names.set(column_ndx, new_name);
            changes = true;
        }
    }
    ColumnType type = ColumnType(m_types.get(column_ndx));
    switch (type) {
        case col_type_OldTable: {
            Spec sub_spec(get_alloc());
            size_t subspec_ndx = get_subspec_ndx(column_ndx);
            ref_type ref = to_ref(m_subspecs.get(subspec_ndx)); // Throws
            sub_spec.init(ref);
            m_types.set(column_ndx, sub_spec.get_column_type(0));
            m_attr.set(column_ndx, m_attr.get(column_ndx) | col_attr_List);
            sub_spec.destroy();
            m_subspecs.erase(subspec_ndx);
            changes = true;
            break;
        }
        case col_type_LinkList:
            m_attr.set(column_ndx, m_attr.get(column_ndx) | col_attr_List);
            changes = true;
            break;
        default:
            break;
    }
    return changes;
}

bool Spec::convert_column_key(size_t column_ndx, Group* group)
{
    bool changes = false;
    ColKey col_key = generate_converted_colkey(column_ndx);
    ColKey existing_key = ColKey{m_keys.get(column_ndx)};
    if (col_key.value != existing_key.value) {
        m_keys.set(column_ndx, col_key.value);
        changes = true;
    }
    static_cast<void>(group);
    /*
      FIXME: This part of the conversion should be managed by Table, since link info
      has been moved there
      */
    auto type = col_key.get_type();
    if (type == col_type_BackLink) {
        ColKey stored_oc = get_origin_column_key(column_ndx);
        Spec& origin_spec = _impl::TableFriend::get_spec(*group->get_table(get_opposite_link_table_key(column_ndx)));
        // old type column keys had the spec index where we'll have the leaf index, so use get_index() on the key
        size_t old_oc_index = stored_oc.get_index().val;
        if (old_oc_index < origin_spec.get_public_column_count()) {
            ColKey generated_oc = origin_spec.generate_converted_colkey(old_oc_index);
            if (generated_oc != stored_oc) {
                set_backlink_origin_column(column_ndx, generated_oc);
                changes = true;
            }
        }
    }
    return changes;
}

void Spec::insert_column(size_t column_ndx, ColKey col_key, ColumnType type, StringData name, int attr)
{
    REALM_ASSERT(column_ndx <= m_types.size());

    if (REALM_UNLIKELY(name.size() > Table::max_column_name_length)) {
        throw LogicError(LogicError::column_name_too_long);
    }
    if (get_column_index(name) != npos) {
        throw LogicError(LogicError::column_name_in_use);
    }

    if (type != col_type_BackLink) {
        m_names.insert(column_ndx, name); // Throws
        m_num_public_columns++;
    }

    m_types.insert(column_ndx, type);     // Throws
    // FIXME: So far, attributes are never reported to the replication system
    m_attr.insert(column_ndx, attr); // Throws
    m_keys.insert(column_ndx, col_key.value);

    bool is_subspec_type = type == col_type_Link || type == col_type_LinkList || type == col_type_BackLink;
    if (is_subspec_type) {
        Allocator& alloc = m_top.get_alloc();
        // `m_subspecs` array is only present when the spec contains a subtable column
        REALM_ASSERT_EX(m_subspecs.is_attached() == has_subspec(), m_subspecs.is_attached(), m_top.size());
        if (!m_subspecs.is_attached()) {
            bool context_flag = false;
            MemRef subspecs_mem = Array::create_empty_array(Array::type_HasRefs, context_flag, alloc); // Throws
            _impl::DeepArrayRefDestroyGuard dg(subspecs_mem.get_ref(), alloc);
            if (m_top.size() == 3) {
                int_fast64_t v(from_ref(subspecs_mem.get_ref()));
                m_top.add(v); // Throws
            }
            else {
                int_fast64_t v(from_ref(subspecs_mem.get_ref()));
                m_top.set(3, v); // Throws
            }
            m_subspecs.init_from_ref(subspecs_mem.get_ref());
            m_subspecs.set_parent(&m_top, 3);
            dg.release();
        }

        if (type == col_type_Link || type == col_type_LinkList) {
            // Store group-level table index of target table. When we set the
            // target it will be as a tagged integer (low bit set) Since we
            // don't know it yet we just store zero (null ref).
            size_t subspec_ndx = get_subspec_ndx(column_ndx);
            m_subspecs.insert(subspec_ndx, 0); // Throws
        }
        else if (type == col_type_BackLink) {
            // Store group-level table index of origin table and index of origin
            // column. When we set the target it will be as a tagged integer
            // (low bit set) Since we don't know it yet we just store zero (null
            // ref).
            size_t subspec_ndx = get_subspec_ndx(column_ndx);
            m_subspecs.insert(subspec_ndx, 0); // Throws
            m_subspecs.insert(subspec_ndx, 1); // Throws
        }
    }

    if (m_enumkeys.is_attached() && type != col_type_BackLink) {
        m_enumkeys.insert(column_ndx, 0);
    }

    update_internals();
}

void Spec::erase_column(size_t column_ndx)
{
    REALM_ASSERT(column_ndx < m_types.size());

    // If the column is a subtable column, we have to delete
    // the subspec(s) as well
    ColumnType type = ColumnType(m_types.get(column_ndx));
    if (Table::is_link_type(type)) {
        size_t subspec_ndx = get_subspec_ndx(column_ndx);
        m_subspecs.erase(subspec_ndx); // origin table index  : Throws
    }
    else if (type == col_type_BackLink) {
        size_t subspec_ndx = get_subspec_ndx(column_ndx);
        m_subspecs.erase(subspec_ndx); // origin table index  : Throws
        m_subspecs.erase(subspec_ndx); // origin column index : Throws
    }
    else if (is_string_enum_type(column_ndx)) {
        // Enum columns do also have a separate key list
        ref_type keys_ref = m_enumkeys.get_as_ref(column_ndx);
        Array::destroy_deep(keys_ref, m_top.get_alloc());
        m_enumkeys.set(column_ndx, 0);
    }

    // Remove this column from the enum keys lookup and clean it up if it's now empty
    if (m_enumkeys.is_attached() && type != col_type_BackLink) {
        m_enumkeys.erase(column_ndx); // Throws
        bool all_empty = true;
        for (size_t i = 0; i < m_enumkeys.size(); i++) {
            if (m_enumkeys.get(i) != 0) {
                all_empty = false;
                break;
            }
        }
        if (all_empty) {
            m_enumkeys.destroy_deep();
            m_top.set(4, 0);
        }
    }

    // Delete the actual name and type entries
    if (type != col_type_BackLink) {
        m_num_public_columns--;
        m_names.erase(column_ndx); // Throws
    }
    m_types.erase(column_ndx);     // Throws
    m_attr.erase(column_ndx);      // Throws
    m_keys.erase(column_ndx);

    update_internals();
}


size_t Spec::get_subspec_ndx(size_t column_ndx) const noexcept
{
    REALM_ASSERT(column_ndx == get_column_count() || get_column_type(column_ndx) == col_type_Link ||
                 get_column_type(column_ndx) == col_type_LinkList ||
                 get_column_type(column_ndx) == col_type_BackLink ||
                 get_column_type(column_ndx) == col_type_OldTable);

    return get_subspec_ndx_after(column_ndx, column_ndx);
}


size_t Spec::get_subspec_ndx_after(size_t column_ndx, size_t skip_column_ndx) const noexcept
{
    REALM_ASSERT(column_ndx <= get_column_count());
    // The m_subspecs array only keep info for subtables so we need to
    // count up to it's position
    size_t subspec_ndx = 0;
    for (size_t i = 0; i != column_ndx; ++i) {
        if (i == skip_column_ndx) {
            continue;
        }

        ColumnType type = ColumnType(m_types.get(i));
        subspec_ndx += get_subspec_entries_for_col_type(type);
    }
    return subspec_ndx;
}


size_t Spec::get_subspec_entries_for_col_type(ColumnType type) const noexcept
{
    if (type == col_type_Link || type == col_type_LinkList) {
        return 1; // index of dest column
    }
    else if (type == col_type_BackLink) {
        return 2; // index of table and index of linked column
    }
    return 0; // no entries for other column types (subspec array is sparse)
}


void Spec::upgrade_string_to_enum(size_t column_ndx, ref_type keys_ref)
{
    REALM_ASSERT(get_column_type(column_ndx) == col_type_String);

    // Create the enumkeys list if needed
    if (!m_enumkeys.is_attached()) {
        m_enumkeys.create(Array::type_HasRefs, false, m_num_public_columns);
        m_top.set(4, m_enumkeys.get_ref());
        m_enumkeys.set_parent(&m_top, 4);
    }

    // Insert the new key list
    m_enumkeys.set(column_ndx, keys_ref);
}

bool Spec::is_string_enum_type(size_t column_ndx) const noexcept
{
    return m_enumkeys.is_attached() ? (m_enumkeys.get(column_ndx) != 0) : false;
}

ref_type Spec::get_enumkeys_ref(size_t column_ndx, ArrayParent*& keys_parent) noexcept
{
    // We also need to return parent info
    keys_parent = &m_enumkeys;

    return m_enumkeys.get_as_ref(column_ndx);
}

TableKey Spec::get_opposite_link_table_key(size_t column_ndx) const noexcept
{
    REALM_ASSERT(column_ndx < get_column_count());
    REALM_ASSERT(get_column_type(column_ndx) == col_type_Link || get_column_type(column_ndx) == col_type_LinkList ||
                 get_column_type(column_ndx) == col_type_BackLink);

    // Key of opposite table is stored as tagged int in the
    // subspecs array
    size_t subspec_ndx = get_subspec_ndx(column_ndx);
    int64_t tagged_value = m_subspecs.get(subspec_ndx);
    REALM_ASSERT(tagged_value != 0); // can't retrieve it if never set

    uint64_t table_ref = uint64_t(tagged_value) >> 1;

    REALM_ASSERT(!util::int_cast_has_overflow<size_t>(table_ref));
    return TableKey(table_ref);
}

void Spec::set_backlink_origin_column(size_t backlink_col_ndx, ColKey origin_col_key)
{
    REALM_ASSERT(backlink_col_ndx < get_column_count());
    REALM_ASSERT(get_column_type(backlink_col_ndx) == col_type_BackLink);

    // position of target table is stored as tagged int
    int64_t tagged_ndx = (origin_col_key.value << 1) + 1;

    size_t subspec_ndx = get_subspec_ndx(backlink_col_ndx);
    m_subspecs.set(subspec_ndx + 1, tagged_ndx); // Throws
}


ColKey Spec::get_origin_column_key(size_t backlink_col_ndx) const noexcept
{
    REALM_ASSERT(backlink_col_ndx < get_column_count());
    REALM_ASSERT(get_column_type(backlink_col_ndx) == col_type_BackLink);

    // Origin column is stored as second tagged int in the subspecs array
    size_t subspec_ndx = get_subspec_ndx(backlink_col_ndx);
    int64_t tagged_value = m_subspecs.get(subspec_ndx + 1);
    REALM_ASSERT(tagged_value != 0); // can't retrieve it if never set
    auto retval = ColKey(uint64_t(tagged_value) >> 1);
    return retval;
}

DataType Spec::get_public_column_type(size_t ndx) const noexcept
{
    REALM_ASSERT(ndx < get_column_count());

    ColumnType type = get_column_type(ndx);

    return DataType(type);
}


bool Spec::operator==(const Spec& spec) const noexcept
{
    if (!m_attr.compare(spec.m_attr))
        return false;
    if (!m_names.compare_string(spec.m_names))
        return false;

    // check each column's type
    const size_t column_count = get_column_count();
    for (size_t col_ndx = 0; col_ndx < column_count; ++col_ndx) {
        ColumnType col_type = ColumnType(m_types.get(col_ndx));
        switch (col_type) {
            case col_type_Link:
            case col_type_LinkList: {
                // In addition to name and attributes, the link target table must also be compared
                REALM_ASSERT(false); // We can no longer compare specs - in fact we don't want to
                /*
                auto lhs_table_key = get_opposite_link_table_key(col_ndx);
                auto rhs_table_key = spec.get_opposite_link_table_key(col_ndx);
                if (lhs_table_key != rhs_table_key)
                    return false;
                */
                break;
            }
            case col_type_Int:
            case col_type_Bool:
            case col_type_Binary:
            case col_type_String:
            case col_type_OldStringEnum:
            case col_type_OldTable:
            case col_type_OldMixed:
            case col_type_OldDateTime:
            case col_type_Timestamp:
            case col_type_Float:
            case col_type_Double:
            case col_type_Reserved4:
            case col_type_BackLink:
                // All other column types are compared as before
                if (m_types.get(col_ndx) != spec.m_types.get(col_ndx))
                    return false;
                break;
        }
    }

    return true;
}


ColKey Spec::get_key(size_t column_ndx) const
{
    return ColKey(m_keys.get(column_ndx));
}


#ifdef REALM_DEBUG // LCOV_EXCL_START ignore debug functions

void Spec::verify() const
{
    REALM_ASSERT(m_names.size() == get_public_column_count());
    REALM_ASSERT(m_types.size() == get_column_count());
    REALM_ASSERT(m_attr.size() == get_column_count());

    REALM_ASSERT(m_types.get_ref() == m_top.get_as_ref(0));
    REALM_ASSERT(m_names.get_ref() == m_top.get_as_ref(1));
    REALM_ASSERT(m_attr.get_ref() == m_top.get_as_ref(2));
}


void Spec::to_dot(std::ostream& out, StringData title) const
{
    ref_type top_ref = m_top.get_ref();

    out << "subgraph cluster_specset" << top_ref << " {" << std::endl;
    out << " label = \"specset " << title << "\";" << std::endl;

    std::string types_name = "types (" + util::to_string(m_types.size()) + ")";
    std::string names_name = "names (" + util::to_string(m_names.size()) + ")";
    std::string attr_name = "attrs (" + util::to_string(m_attr.size()) + ")";

    m_top.to_dot(out);
    m_types.to_dot(out, types_name);
    m_names.to_dot(out, names_name);
    m_attr.to_dot(out, attr_name);

    size_t num_cols = m_types.size();
    bool have_subspecs = false;
    for (size_t i = 0; i < num_cols; ++i) {
        ColumnType type = ColumnType(m_types.get(i));
        if (type == col_type_Link || type == col_type_LinkList || type == col_type_BackLink) {
            have_subspecs = true;
            break;
        }
    }

    if (have_subspecs) {
        REALM_ASSERT(m_subspecs.is_attached());
        m_subspecs.to_dot(out, "subspecs");
    }

    out << "}" << std::endl;
}


#endif // LCOV_EXCL_STOP ignore debug functions
