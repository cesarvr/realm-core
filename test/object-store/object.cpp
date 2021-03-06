////////////////////////////////////////////////////////////////////////////
//
// Copyright 2017 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include <catch2/catch.hpp>

#include "util/event_loop.hpp"
#include "util/index_helpers.hpp"
#include "util/test_file.hpp"

#include <realm/object-store/feature_checks.hpp>
#include <realm/object-store/collection_notifications.hpp>
#include <realm/object-store/object_accessor.hpp>
#include <realm/object-store/property.hpp>
#include <realm/object-store/schema.hpp>
#include <realm/object-store/object.hpp>

#include <realm/object-store/impl/realm_coordinator.hpp>
#include <realm/object-store/impl/object_accessor_impl.hpp>

#include <realm/group.hpp>
#include <realm/util/any.hpp>

#include <cstdint>

using namespace realm;
using util::any_cast;

namespace {
using AnyDict = std::map<std::string, util::Any>;
using AnyVec = std::vector<util::Any>;
template <class T>
std::vector<T> get_vector(std::initializer_list<T> list)
{
    return std::vector<T>(list);
}
} // namespace

struct TestContext : CppContext {
    std::map<std::string, AnyDict> defaults;

    using CppContext::CppContext;
    TestContext(TestContext& parent, realm::Obj& obj, realm::Property const& prop)
        : CppContext(parent, obj, prop)
        , defaults(parent.defaults)
    {
    }

    util::Optional<util::Any> default_value_for_property(ObjectSchema const& object, Property const& prop)
    {
        auto obj_it = defaults.find(object.name);
        if (obj_it == defaults.end())
            return util::none;
        auto prop_it = obj_it->second.find(prop.name);
        if (prop_it == obj_it->second.end())
            return util::none;
        return prop_it->second;
    }

    void will_change(Object const&, Property const&) {}
    void did_change() {}
    std::string print(util::Any)
    {
        return "not implemented";
    }
    bool allow_missing(util::Any)
    {
        return false;
    }
};

TEST_CASE("object") {
    using namespace std::string_literals;
    _impl::RealmCoordinator::assert_no_open_realms();

    InMemoryTestFile config;
    config.cache = false;
    config.automatic_change_notifications = false;
    config.schema = Schema{
        {"table",
         {
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"value 1", PropertyType::Int},
             {"value 2", PropertyType::Int},
         }},
        {"all types",
         {
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"bool", PropertyType::Bool},
             {"int", PropertyType::Int},
             {"float", PropertyType::Float},
             {"double", PropertyType::Double},
             {"string", PropertyType::String},
             {"data", PropertyType::Data},
             {"date", PropertyType::Date},
             {"object id", PropertyType::ObjectId},
             {"decimal", PropertyType::Decimal},
             {"uuid", PropertyType::UUID},
             {"mixed", PropertyType::Mixed | PropertyType::Nullable},
             {"object", PropertyType::Object | PropertyType::Nullable, "link target"},

             {"bool array", PropertyType::Array | PropertyType::Bool},
             {"int array", PropertyType::Array | PropertyType::Int},
             {"float array", PropertyType::Array | PropertyType::Float},
             {"double array", PropertyType::Array | PropertyType::Double},
             {"string array", PropertyType::Array | PropertyType::String},
             {"data array", PropertyType::Array | PropertyType::Data},
             {"date array", PropertyType::Array | PropertyType::Date},
             {"object array", PropertyType::Array | PropertyType::Object, "array target"},
             {"object id array", PropertyType::Array | PropertyType::ObjectId},
             {"uuid array", PropertyType::Array | PropertyType::UUID},
             {"decimal array", PropertyType::Array | PropertyType::Decimal},
             {"mixed array", PropertyType::Array | PropertyType::Mixed | PropertyType::Nullable},

             {"dictionary", PropertyType::Dictionary | PropertyType::String},
         }},
        {"all optional types",
         {
             {"_id", PropertyType::Int | PropertyType::Nullable, Property::IsPrimary{true}},
             {"bool", PropertyType::Bool | PropertyType::Nullable},
             {"int", PropertyType::Int | PropertyType::Nullable},
             {"float", PropertyType::Float | PropertyType::Nullable},
             {"double", PropertyType::Double | PropertyType::Nullable},
             {"string", PropertyType::String | PropertyType::Nullable},
             {"data", PropertyType::Data | PropertyType::Nullable},
             {"date", PropertyType::Date | PropertyType::Nullable},
             {"object id", PropertyType::ObjectId | PropertyType::Nullable},
             {"decimal", PropertyType::Decimal | PropertyType::Nullable},
             {"uuid", PropertyType::UUID | PropertyType::Nullable},

             {"bool array", PropertyType::Array | PropertyType::Bool | PropertyType::Nullable},
             {"int array", PropertyType::Array | PropertyType::Int | PropertyType::Nullable},
             {"float array", PropertyType::Array | PropertyType::Float | PropertyType::Nullable},
             {"double array", PropertyType::Array | PropertyType::Double | PropertyType::Nullable},
             {"string array", PropertyType::Array | PropertyType::String | PropertyType::Nullable},
             {"data array", PropertyType::Array | PropertyType::Data | PropertyType::Nullable},
             {"date array", PropertyType::Array | PropertyType::Date | PropertyType::Nullable},
             {"object id array", PropertyType::Array | PropertyType::ObjectId | PropertyType::Nullable},
             {"decimal array", PropertyType::Array | PropertyType::Decimal | PropertyType::Nullable},
             {"uuid array", PropertyType::Array | PropertyType::UUID | PropertyType::Nullable},
         }},
        {"link target",
         {
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"value", PropertyType::Int},
         },
         {
             {"origin", PropertyType::LinkingObjects | PropertyType::Array, "all types", "object"},
         }},
        {"array target",
         {
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"value", PropertyType::Int},
         }},
        {"pk after list",
         {
             {"array 1", PropertyType::Array | PropertyType::Object, "array target"},
             {"int 1", PropertyType::Int},
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"int 2", PropertyType::Int},
             {"array 2", PropertyType::Array | PropertyType::Object, "array target"},
         }},
        {"nullable int pk",
         {
             {"_id", PropertyType::Int | PropertyType::Nullable, Property::IsPrimary{true}},
         }},
        {"nullable string pk",
         {
             {"_id", PropertyType::String | PropertyType::Nullable, Property::IsPrimary{true}},
         }},
        {"nullable object id pk",
         {
             {"_id", PropertyType::ObjectId | PropertyType::Nullable, Property::IsPrimary{true}},
         }},
        {"nullable uuid pk",
         {
             {"_id", PropertyType::UUID | PropertyType::Nullable, Property::IsPrimary{true}},
         }},
        {"person",
         {
             {"_id", PropertyType::String, Property::IsPrimary{true}},
             {"age", PropertyType::Int},
             {"scores", PropertyType::Array | PropertyType::Int},
             {"assistant", PropertyType::Object | PropertyType::Nullable, "person"},
             {"team", PropertyType::Array | PropertyType::Object, "person"},
         }},
    };
    config.schema_version = 0;
    auto r = Realm::get_shared_realm(config);
    auto& coordinator = *_impl::RealmCoordinator::get_coordinator(config.path);

    SECTION("add_notification_callback()") {
        auto table = r->read_group().get_table("class_table");
        auto col_keys = table->get_column_keys();
        std::vector<int64_t> pks = {3, 4, 7, 9, 10, 21, 24, 34, 42, 50};
        r->begin_transaction();
        for (int i = 0; i < 10; ++i)
            table->create_object_with_primary_key(pks[i]).set("value 1", i).set("value 2", i);
        r->commit_transaction();

        auto r2 = coordinator.get_realm();

        CollectionChangeSet change;
        auto obj = *table->begin();
        Object object(r, obj);

        auto write = [&](auto&& f) {
            r->begin_transaction();
            f();
            r->commit_transaction();

            advance_and_notify(*r);
        };

        auto require_change = [&] {
            auto token = object.add_notification_callback([&](CollectionChangeSet c, std::exception_ptr) {
                change = c;
            });
            advance_and_notify(*r);
            return token;
        };

        auto require_no_change = [&] {
            bool first = true;
            auto token = object.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
                REQUIRE(first);
                first = false;
            });
            advance_and_notify(*r);
            return token;
        };

        SECTION("deleting the object sends a change notification") {
            auto token = require_change();
            write([&] {
                obj.remove();
            });
            REQUIRE_INDICES(change.deletions, 0);
        }

        SECTION("clearing the table sends a change notification") {
            auto token = require_change();
            write([&] {
                table->clear();
            });
            REQUIRE_INDICES(change.deletions, 0);
        }

        SECTION("clearing the table sends a change notification to the last object") {
            obj = table->get_object(table->size() - 1);
            object = Object(r, obj);

            auto token = require_change();
            write([&] {
                table->clear();
            });
            REQUIRE_INDICES(change.deletions, 0);
        }

        SECTION("modifying the object sends a change notification") {
            auto token = require_change();

            write([&] {
                obj.set(col_keys[0], 10);
            });
            REQUIRE_INDICES(change.modifications, 0);
            REQUIRE(change.columns.size() == 1);
            REQUIRE_INDICES(change.columns[col_keys[0].value], 0);

            write([&] {
                obj.set(col_keys[1], 10);
            });
            REQUIRE_INDICES(change.modifications, 0);
            REQUIRE(change.columns.size() == 1);
            REQUIRE_INDICES(change.columns[col_keys[1].value], 0);
        }

        SECTION("modifying a different object") {
            auto token = require_no_change();
            write([&] {
                table->get_object(1).set(col_keys[0], 10);
            });
        }

        SECTION("multiple write transactions") {
            auto token = require_change();

            auto r2row = r2->read_group().get_table("class_table")->get_object(0);
            r2->begin_transaction();
            r2row.set(col_keys[0], 1);
            r2->commit_transaction();
            r2->begin_transaction();
            r2row.set(col_keys[1], 2);
            r2->commit_transaction();

            advance_and_notify(*r);
            REQUIRE(change.columns.size() == 2);
            REQUIRE_INDICES(change.columns[col_keys[0].value], 0);
            REQUIRE_INDICES(change.columns[col_keys[1].value], 0);
        }

        SECTION("skipping a notification") {
            auto token = require_no_change();
            write([&] {
                obj.set(col_keys[0], 1);
                token.suppress_next();
            });
        }

        SECTION("skipping only effects the current transaction even if no notification would occur anyway") {
            auto token = require_change();

            // would not produce a notification even if it wasn't skipped because no changes were made
            write([&] {
                token.suppress_next();
            });
            REQUIRE(change.empty());

            // should now produce a notification
            write([&] {
                obj.set(col_keys[0], 1);
            });
            REQUIRE_INDICES(change.modifications, 0);
        }

        SECTION("add notification callback, remove it, then add another notification callback") {
            {
                auto token = object.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
                    FAIL("This should never happen");
                });
            }
            auto token = require_change();
            write([&] {
                obj.remove();
            });
            REQUIRE_INDICES(change.deletions, 0);
        }

        SECTION("observing deleted object throws") {
            write([&] {
                obj.remove();
            });
            REQUIRE_THROWS(require_change());
        }
    }

    TestContext d(r);
    auto create = [&](util::Any&& value, CreatePolicy policy = CreatePolicy::ForceCreate) {
        r->begin_transaction();
        auto obj = Object::create(d, r, *r->schema().find("all types"), value, policy);
        r->commit_transaction();
        return obj;
    };
    auto create_sub = [&](util::Any&& value, CreatePolicy policy = CreatePolicy::ForceCreate) {
        r->begin_transaction();
        auto obj = Object::create(d, r, *r->schema().find("link target"), value, policy);
        r->commit_transaction();
        return obj;
    };
    auto create_company = [&](util::Any&& value, CreatePolicy policy = CreatePolicy::ForceCreate) {
        r->begin_transaction();
        Object obj = Object::create(d, r, *r->schema().find("person"), value, policy);
        r->commit_transaction();
        return obj;
    };

    SECTION("create object") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-abba-baba-caca-4136c566a962")},
            {"mixed", "mixed"s},

            {"bool array", AnyVec{true, false}},
            {"int array", AnyVec{INT64_C(5), INT64_C(6)}},
            {"float array", AnyVec{1.1f, 2.2f}},
            {"double array", AnyVec{3.3, 4.4}},
            {"string array", AnyVec{"a"s, "b"s, "c"s}},
            {"data array", AnyVec{"d"s, "e"s, "f"s}},
            {"date array", AnyVec{Timestamp(10, 20), Timestamp(30, 40)}},
            {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}}}},
            {"object id array", AnyVec{ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"), ObjectId("BBBBBBBBBBBBBBBBBBBBBBBB")}},
            {"decimal array", AnyVec{Decimal128("1.23e45"), Decimal128("6.78e9")}},
            {"uuid array", AnyVec{UUID(), UUID("3b241101-e2bb-4255-8caf-4136c566a962")}},
            {"mixed array",
             AnyVec{25, "b"s, 1.45, util::none, Timestamp(30, 40), Decimal128("1.23e45"),
                    ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"), UUID("3b241101-e2bb-4255-8caf-4136c566a962")}},
            {"dictionary", AnyDict{{"key", "value"s}}},
        });

        Obj row = obj.obj();
        auto link_target = *r->read_group().get_table("class_link target")->begin();
        TableRef table = row.get_table();
        auto target_table = link_target.get_table();
        auto array_target_table = r->read_group().get_table("class_array target");
        REQUIRE(row.get<Int>(table->get_column_key("_id")) == 1);
        REQUIRE(row.get<Bool>(table->get_column_key("bool")) == true);
        REQUIRE(row.get<Int>(table->get_column_key("int")) == 5);
        REQUIRE(row.get<float>(table->get_column_key("float")) == 2.2f);
        REQUIRE(row.get<double>(table->get_column_key("double")) == 3.3);
        REQUIRE(row.get<String>(table->get_column_key("string")) == "hello");
        REQUIRE(row.get<Binary>(table->get_column_key("data")) == BinaryData("olleh", 5));
        REQUIRE(row.get<Timestamp>(table->get_column_key("date")) == Timestamp(10, 20));
        REQUIRE(row.get<ObjKey>(table->get_column_key("object")) == link_target.get_key());
        REQUIRE(row.get<ObjectId>(table->get_column_key("object id")) == ObjectId("000000000000000000000001"));
        REQUIRE(row.get<Decimal128>(table->get_column_key("decimal")) == Decimal128("1.23e45"));
        REQUIRE(row.get<UUID>(table->get_column_key("uuid")) == UUID("3b241101-abba-baba-caca-4136c566a962"));
        REQUIRE(row.get<Mixed>(table->get_column_key("mixed")) == Mixed("mixed"));

        REQUIRE(link_target.get<Int>(target_table->get_column_key("value")) == 10);

        auto check_array = [&](ColKey col, auto... values) {
            auto vec = get_vector({values...});
            using U = typename decltype(vec)::value_type;
            auto list = row.get_list<U>(col);
            size_t i = 0;
            for (auto value : vec) {
                CAPTURE(i);
                REQUIRE(i < list.size());
                REQUIRE(value == list.get(i));
                ++i;
            }
        };
        check_array(table->get_column_key("bool array"), true, false);
        check_array(table->get_column_key("int array"), INT64_C(5), INT64_C(6));
        check_array(table->get_column_key("float array"), 1.1f, 2.2f);
        check_array(table->get_column_key("double array"), 3.3, 4.4);
        check_array(table->get_column_key("string array"), StringData("a"), StringData("b"), StringData("c"));
        check_array(table->get_column_key("data array"), BinaryData("d", 1), BinaryData("e", 1), BinaryData("f", 1));
        check_array(table->get_column_key("date array"), Timestamp(10, 20), Timestamp(30, 40));
        check_array(table->get_column_key("object id array"), ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"),
                    ObjectId("BBBBBBBBBBBBBBBBBBBBBBBB"));
        check_array(table->get_column_key("decimal array"), Decimal128("1.23e45"), Decimal128("6.78e9"));
        check_array(table->get_column_key("uuid array"), UUID(), UUID("3b241101-e2bb-4255-8caf-4136c566a962"));
        {
            auto list = row.get_list<Mixed>(table->get_column_key("mixed array"));
            REQUIRE(list.size() == 8);
            REQUIRE(list.get(0).get_int() == 25);
            REQUIRE(list.get(1).get_string() == "b");
            REQUIRE(list.get(2).get_double() == 1.45);
            REQUIRE(list.get(3).is_null());
            REQUIRE(list.get(4).get_timestamp() == Timestamp(30, 40));
            REQUIRE(list.get(5).get_decimal() == Decimal128("1.23e45"));
            REQUIRE(list.get(6).get_object_id() == ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"));
            REQUIRE(list.get(7).get_uuid() == UUID("3b241101-e2bb-4255-8caf-4136c566a962"));
        }

        REQUIRE(row.get_dictionary(table->get_column_key("dictionary")).get("key") == Mixed("value"));

        auto list = row.get_linklist_ptr(table->get_column_key("object array"));
        REQUIRE(list->size() == 1);
        REQUIRE(list->get_object(0).get<Int>(array_target_table->get_column_key("value")) == 20);
    }

    SECTION("create uses defaults for missing values") {
        d.defaults["all types"] = {
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-1111-2222-3333-4136c566a962")},

            {"bool array", AnyVec{true, false}},
            {"int array", AnyVec{INT64_C(5), INT64_C(6)}},
            {"float array", AnyVec{1.1f, 2.2f}},
            {"double array", AnyVec{3.3, 4.4}},
            {"string array", AnyVec{"a"s, "b"s, "c"s}},
            {"data array", AnyVec{"d"s, "e"s, "f"s}},
            {"date array", AnyVec{}},
            {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}}}},
            {"object id array", AnyVec{ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"), ObjectId("BBBBBBBBBBBBBBBBBBBBBBBB")}},
            {"decimal array", AnyVec{Decimal128("1.23e45"), Decimal128("6.78e9")}},
            {"uuid array", AnyVec{UUID(), UUID("3b241101-e2bb-4255-8caf-4136c566a962")}},
            {"dictionary", AnyDict{{"name", "John Doe"s}}},
        };

        Object obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"float", 6.6f},
        });

        Obj row = obj.obj();
        TableRef table = row.get_table();
        REQUIRE(row.get<Int>(table->get_column_key("_id")) == 1);
        REQUIRE(row.get<Bool>(table->get_column_key("bool")) == true);
        REQUIRE(row.get<Int>(table->get_column_key("int")) == 5);
        REQUIRE(row.get<float>(table->get_column_key("float")) == 6.6f);
        REQUIRE(row.get<double>(table->get_column_key("double")) == 3.3);
        REQUIRE(row.get<String>(table->get_column_key("string")) == "hello");
        REQUIRE(row.get<Binary>(table->get_column_key("data")) == BinaryData("olleh", 5));
        REQUIRE(row.get<Timestamp>(table->get_column_key("date")) == Timestamp(10, 20));
        REQUIRE(row.get<ObjectId>(table->get_column_key("object id")) == ObjectId("000000000000000000000001"));
        REQUIRE(row.get<Decimal128>(table->get_column_key("decimal")) == Decimal128("1.23e45"));
        REQUIRE(row.get<UUID>(table->get_column_key("uuid")) == UUID("3b241101-1111-2222-3333-4136c566a962"));
        REQUIRE(row.get_dictionary(table->get_column_key("dictionary")).get("name") == Mixed("John Doe"));

        REQUIRE(row.get_listbase_ptr(table->get_column_key("bool array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("int array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("float array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("double array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("string array"))->size() == 3);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("data array"))->size() == 3);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("date array"))->size() == 0);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("object array"))->size() == 1);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("object id array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("decimal array"))->size() == 2);
        REQUIRE(row.get_listbase_ptr(table->get_column_key("uuid array"))->size() == 2);
    }

    SECTION("create can use defaults for primary key") {
        d.defaults["all types"] = {
            {"_id", INT64_C(10)},
        };
        auto obj = create(AnyDict{
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-0000-0000-0000-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},
        });

        auto row = obj.obj();
        REQUIRE(row.get<Int>(row.get_table()->get_column_key("_id")) == 10);
    }

    SECTION("create does not complain about missing values for nullable fields") {
        r->begin_transaction();
        realm::Object obj;
        REQUIRE_NOTHROW(obj = Object::create(d, r, *r->schema().find("all optional types"), util::Any(AnyDict{})));
        r->commit_transaction();

        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "_id").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "bool").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "int").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "float").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "double").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "string").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "data").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "date").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "object id").has_value());
        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "uuid").has_value());

        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "int array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "float array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "double array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "string array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "data array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "date array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object id array")).size() == 0);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "uuid array")).size() == 0);
    }

    SECTION("create throws for missing values if there is no default") {
        REQUIRE_THROWS(create(AnyDict{
            {"_id", INT64_C(1)},
            {"float", 6.6f},
        }));
    }

    SECTION("create always sets the PK first") {
        AnyDict value{
            {"array 1", AnyVector{AnyDict{{"_id", INT64_C(1)}, {"value", INT64_C(1)}}}},
            {"array 2", AnyVector{AnyDict{{"_id", INT64_C(2)}, {"value", INT64_C(2)}}}},
            {"int 1", INT64_C(0)},
            {"int 2", INT64_C(0)},
            {"_id", INT64_C(7)},
        };
        // Core will throw if the list is populated before the PK is set
        r->begin_transaction();
        REQUIRE_NOTHROW(Object::create(d, r, *r->schema().find("pk after list"), util::Any(value)));
    }

    SECTION("create with update") {
        CollectionChangeSet change;
        bool callback_called;
        Object obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-9999-9999-9999-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},

            {"bool array", AnyVec{true, false}},
            {"int array", AnyVec{INT64_C(5), INT64_C(6)}},
            {"float array", AnyVec{1.1f, 2.2f}},
            {"double array", AnyVec{3.3, 4.4}},
            {"string array", AnyVec{"a"s, "b"s, "c"s}},
            {"data array", AnyVec{"d"s, "e"s, "f"s}},
            {"date array", AnyVec{}},
            {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}}}},
            {"object id array", AnyVec{ObjectId("AAAAAAAAAAAAAAAAAAAAAAAA"), ObjectId("BBBBBBBBBBBBBBBBBBBBBBBB")}},
            {"decimal array", AnyVec{Decimal128("1.23e45"), Decimal128("6.78e9")}},
            {"uuid array", AnyVec{UUID(), UUID("3b241101-1234-5678-9012-4136c566a962")}}});

        auto token = obj.add_notification_callback([&](CollectionChangeSet c, std::exception_ptr) {
            change = c;
            callback_called = true;
        });
        advance_and_notify(*r);

        create(
            AnyDict{
                {"_id", INT64_C(1)},
                {"int", INT64_C(6)},
                {"string", "a"s},
            },
            CreatePolicy::UpdateAll);

        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(callback_called);
        REQUIRE_INDICES(change.modifications, 0);

        auto row = obj.obj();
        auto table = row.get_table();
        REQUIRE(row.get<Int>(table->get_column_key("_id")) == 1);
        REQUIRE(row.get<Bool>(table->get_column_key("bool")) == true);
        REQUIRE(row.get<Int>(table->get_column_key("int")) == 6);
        REQUIRE(row.get<float>(table->get_column_key("float")) == 2.2f);
        REQUIRE(row.get<double>(table->get_column_key("double")) == 3.3);
        REQUIRE(row.get<String>(table->get_column_key("string")) == "a");
        REQUIRE(row.get<Binary>(table->get_column_key("data")) == BinaryData("olleh", 5));
        REQUIRE(row.get<Timestamp>(table->get_column_key("date")) == Timestamp(10, 20));
        REQUIRE(row.get<ObjectId>(table->get_column_key("object id")) == ObjectId("000000000000000000000001"));
        REQUIRE(row.get<Decimal128>(table->get_column_key("decimal")) == Decimal128("1.23e45"));
        REQUIRE(row.get<UUID>(table->get_column_key("uuid")) == UUID("3b241101-9999-9999-9999-4136c566a962"));
    }

    SECTION("create with update - only with diffs") {
        CollectionChangeSet change;
        bool callback_called;
        AnyDict adam{
            {"_id", "pk0"s},
            {"name", "Adam"s},
            {"age", INT64_C(32)},
            {"scores", AnyVec{INT64_C(1), INT64_C(2)}},
        };
        AnyDict brian{
            {"_id", "pk1"s},
            {"name", "Brian"s},
            {"age", INT64_C(33)},
        };
        AnyDict charley{{"_id", "pk2"s}, {"name", "Charley"s}, {"age", INT64_C(34)}, {"team", AnyVec{adam, brian}}};
        AnyDict donald{
            {"_id", "pk3"s},
            {"name", "Donald"s},
            {"age", INT64_C(35)},
        };
        AnyDict eddie{{"_id", "pk4"s},
                      {"name", "Eddie"s},
                      {"age", INT64_C(36)},
                      {"assistant", donald},
                      {"team", AnyVec{donald, charley}}};
        Object obj = create_company(eddie, CreatePolicy::UpdateAll);

        auto table = r->read_group().get_table("class_person");
        REQUIRE(table->size() == 5);
        Results result(r, table);
        result = result.sort({{"_id", false}});
        auto token = result.add_notification_callback([&](CollectionChangeSet c, std::exception_ptr) {
            change = c;
            callback_called = true;
        });
        advance_and_notify(*r);

        // First update unconditionally
        create_company(eddie, CreatePolicy::UpdateAll);

        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(callback_called);
        REQUIRE_INDICES(change.modifications, 0, 1, 2, 3, 4);

        // Now, only update where differences (there should not be any diffs - so no update)
        create_company(eddie, CreatePolicy::UpdateModified);

        REQUIRE(table->size() == 5);
        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(!callback_called);

        // Now, only update sub-object)
        donald["scores"] = AnyVec{INT64_C(3), INT64_C(4), INT64_C(5)};
        // Insert the new donald
        eddie["assistant"] = donald;
        create_company(eddie, CreatePolicy::UpdateModified);

        REQUIRE(table->size() == 5);
        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(callback_called);
        REQUIRE_INDICES(change.modifications, 1);

        // Shorten list
        donald["scores"] = AnyVec{INT64_C(3), INT64_C(4)};
        eddie["assistant"] = donald;
        create_company(eddie, CreatePolicy::UpdateModified);

        REQUIRE(table->size() == 5);
        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(callback_called);
        REQUIRE_INDICES(change.modifications, 1);
    }

    SECTION("create with update - identical sub-object") {
        Object sub_obj = create_sub(AnyDict{{"value", INT64_C(10)}, {"_id", INT64_C(10)}});
        Object obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", sub_obj},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-9999-9999-9999-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},
        });

        auto obj_table = r->read_group().get_table("class_all types");
        Results result(r, obj_table);
        bool callback_called;
        bool results_callback_called;
        bool sub_callback_called;
        auto token1 = obj.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            callback_called = true;
        });
        auto token2 = result.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            results_callback_called = true;
        });
        auto token3 = sub_obj.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            sub_callback_called = true;
        });
        advance_and_notify(*r);

        auto table = r->read_group().get_table("class_link target");
        REQUIRE(table->size() == 1);

        create(
            AnyDict{
                {"_id", INT64_C(1)},
                {"bool", true},
                {"int", INT64_C(5)},
                {"float", 2.2f},
                {"double", 3.3},
                {"string", "hello"s},
                {"data", "olleh"s},
                {"date", Timestamp(10, 20)},
                {"object id", ObjectId("000000000000000000000001")},
                {"decimal", Decimal128("1.23e45")},
                {"uuid", UUID("3b241101-9999-9999-9999-4136c566a962")},
                {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            },
            CreatePolicy::UpdateModified);

        REQUIRE(table->size() == 1);
        callback_called = false;
        results_callback_called = false;
        sub_callback_called = false;
        advance_and_notify(*r);
        REQUIRE(!callback_called);
        REQUIRE(!results_callback_called);
        REQUIRE(!sub_callback_called);

        // Now change sub object
        create(
            AnyDict{
                {"_id", INT64_C(1)},
                {"bool", true},
                {"int", INT64_C(5)},
                {"float", 2.2f},
                {"double", 3.3},
                {"string", "hello"s},
                {"data", "olleh"s},
                {"date", Timestamp(10, 20)},
                {"object id", ObjectId("000000000000000000000001")},
                {"decimal", Decimal128("1.23e45")},
                {"uuid", UUID("3b241101-9999-9999-9999-4136c566a962")},
                {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(11)}}},
            },
            CreatePolicy::UpdateModified);

        callback_called = false;
        results_callback_called = false;
        sub_callback_called = false;
        advance_and_notify(*r);
        REQUIRE(!callback_called);
        REQUIRE(results_callback_called);
        REQUIRE(sub_callback_called);
    }

    SECTION("create with update - identical array of sub-objects") {
        bool callback_called;
        auto dict = AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}},
                                    AnyDict{{"_id", INT64_C(21)}, {"value", INT64_C(21)}}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},
        };
        Object obj = create(dict);

        auto obj_table = r->read_group().get_table("class_all types");
        Results result(r, obj_table);
        auto token1 = result.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            callback_called = true;
        });
        advance_and_notify(*r);

        create(dict, CreatePolicy::UpdateModified);

        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(!callback_called);

        // Now change list
        dict["object array"] = AnyVec{AnyDict{{"_id", INT64_C(23)}, {"value", INT64_C(23)}}};
        create(dict, CreatePolicy::UpdateModified);

        callback_called = false;
        advance_and_notify(*r);
        REQUIRE(callback_called);
    }

    for (auto policy : {CreatePolicy::UpdateAll, CreatePolicy::UpdateModified}) {
        SECTION("set existing fields to null with update "s + (policy.diff ? "(diffed)" : "(all)")) {
            AnyDict initial_values{
                {"_id", INT64_C(1)},
                {"bool", true},
                {"int", INT64_C(5)},
                {"float", 2.2f},
                {"double", 3.3},
                {"string", "hello"s},
                {"data", "olleh"s},
                {"date", Timestamp(10, 20)},
                {"object id", ObjectId("000000000000000000000001")},
                {"decimal", Decimal128("1.23e45")},
                {"uuid", UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")},

                {"bool array", AnyVec{true, false}},
                {"int array", AnyVec{INT64_C(5), INT64_C(6)}},
                {"float array", AnyVec{1.1f, 2.2f}},
                {"double array", AnyVec{3.3, 4.4}},
                {"string array", AnyVec{"a"s, "b"s, "c"s}},
                {"data array", AnyVec{"d"s, "e"s, "f"s}},
                {"date array", AnyVec{}},
                {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}}}},
                {"object id array", AnyVec{ObjectId("000000000000000000000001")}},
                {"decimal array", AnyVec{Decimal128("1.23e45")}},
                {"uuid array", AnyVec{UUID("3b241101-1111-bbbb-cccc-4136c566a962")}},
            };
            r->begin_transaction();
            auto obj = Object::create(d, r, *r->schema().find("all optional types"), util::Any(initial_values));

            // Missing fields in dictionary do not update anything
            Object::create(d, r, *r->schema().find("all optional types"), util::Any(AnyDict{{"_id", INT64_C(1)}}),
                           policy);

            REQUIRE(any_cast<bool>(obj.get_property_value<util::Any>(d, "bool")) == true);
            REQUIRE(any_cast<int64_t>(obj.get_property_value<util::Any>(d, "int")) == 5);
            REQUIRE(any_cast<float>(obj.get_property_value<util::Any>(d, "float")) == 2.2f);
            REQUIRE(any_cast<double>(obj.get_property_value<util::Any>(d, "double")) == 3.3);
            REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(d, "string")) == "hello");
            REQUIRE(any_cast<Timestamp>(obj.get_property_value<util::Any>(d, "date")) == Timestamp(10, 20));
            REQUIRE(any_cast<util::Optional<ObjectId>>(obj.get_property_value<util::Any>(d, "object id")).value() ==
                    ObjectId("000000000000000000000001"));
            REQUIRE(any_cast<Decimal128>(obj.get_property_value<util::Any>(d, "decimal")) == Decimal128("1.23e45"));
            REQUIRE(any_cast<util::Optional<UUID>>(obj.get_property_value<util::Any>(d, "uuid")) ==
                    UUID("3b241101-aaaa-bbbb-cccc-4136c566a962"));

            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).get<util::Optional<bool>>(0) ==
                true);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "int array")).get<util::Optional<int64_t>>(0) ==
                5);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "float array")).get<util::Optional<float>>(0) ==
                1.1f);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "double array"))
                        .get<util::Optional<double>>(0) == 3.3);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "string array")).get<StringData>(0) == "a");
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "date array")).size() == 0);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object id array"))
                        .get<util::Optional<ObjectId>>(0) == ObjectId("000000000000000000000001"));
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "decimal array")).get<Decimal128>(0) ==
                    Decimal128("1.23e45"));
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "uuid array")).get<util::Optional<UUID>>(0) ==
                UUID("3b241101-1111-bbbb-cccc-4136c566a962"));

            // Set all properties to null
            AnyDict null_values{
                {"_id", INT64_C(1)},
                {"bool", util::Any()},
                {"int", util::Any()},
                {"float", util::Any()},
                {"double", util::Any()},
                {"string", util::Any()},
                {"data", util::Any()},
                {"date", util::Any()},
                {"object id", util::Any()},
                {"decimal", util::Any()},
                {"uuid", util::Any()},

                {"bool array", AnyVec{util::Any()}},
                {"int array", AnyVec{util::Any()}},
                {"float array", AnyVec{util::Any()}},
                {"double array", AnyVec{util::Any()}},
                {"string array", AnyVec{util::Any()}},
                {"data array", AnyVec{util::Any()}},
                {"date array", AnyVec{Timestamp()}},
                {"object id array", AnyVec{util::Any()}},
                {"decimal array", AnyVec{Decimal128(realm::null())}},
                {"uuid array", AnyVec{util::Any()}},
            };
            Object::create(d, r, *r->schema().find("all optional types"), util::Any(null_values), policy);

            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "bool").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "int").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "float").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "double").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "string").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "data").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "date").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "object id").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "decimal").has_value());
            REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "uuid").has_value());

            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).get<util::Optional<bool>>(0) ==
                util::none);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "int array")).get<util::Optional<int64_t>>(0) ==
                util::none);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "float array")).get<util::Optional<float>>(0) ==
                util::none);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "double array"))
                        .get<util::Optional<double>>(0) == util::none);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "string array")).get<StringData>(0) ==
                    StringData());
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "data array")).get<BinaryData>(0) ==
                    BinaryData());
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "date array")).get<Timestamp>(0) ==
                    Timestamp());
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object id array"))
                        .get<util::Optional<ObjectId>>(0) == util::none);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "decimal array")).get<Decimal>(0) ==
                    Decimal128(realm::null()));
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "uuid array")).get<util::Optional<UUID>>(0) ==
                util::none);

            // Set all properties back to non-null
            Object::create(d, r, *r->schema().find("all optional types"), util::Any(initial_values), policy);
            REQUIRE(any_cast<bool>(obj.get_property_value<util::Any>(d, "bool")) == true);
            REQUIRE(any_cast<int64_t>(obj.get_property_value<util::Any>(d, "int")) == 5);
            REQUIRE(any_cast<float>(obj.get_property_value<util::Any>(d, "float")) == 2.2f);
            REQUIRE(any_cast<double>(obj.get_property_value<util::Any>(d, "double")) == 3.3);
            REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(d, "string")) == "hello");
            REQUIRE(any_cast<Timestamp>(obj.get_property_value<util::Any>(d, "date")) == Timestamp(10, 20));
            REQUIRE(any_cast<util::Optional<ObjectId>>(obj.get_property_value<util::Any>(d, "object id")).value() ==
                    ObjectId("000000000000000000000001"));
            REQUIRE(any_cast<Decimal128>(obj.get_property_value<util::Any>(d, "decimal")) == Decimal128("1.23e45"));
            REQUIRE(any_cast<util::Optional<UUID>>(obj.get_property_value<util::Any>(d, "uuid")) ==
                    UUID("3b241101-aaaa-bbbb-cccc-4136c566a962"));

            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).get<util::Optional<bool>>(0) ==
                true);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "int array")).get<util::Optional<int64_t>>(0) ==
                5);
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "float array")).get<util::Optional<float>>(0) ==
                1.1f);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "double array"))
                        .get<util::Optional<double>>(0) == 3.3);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "string array")).get<StringData>(0) == "a");
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "date array")).size() == 0);
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object id array"))
                        .get<util::Optional<ObjectId>>(0) == ObjectId("000000000000000000000001"));
            REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "decimal array")).get<Decimal128>(0) ==
                    Decimal128("1.23e45"));
            REQUIRE(
                any_cast<List&&>(obj.get_property_value<util::Any>(d, "uuid array")).get<util::Optional<UUID>>(0) ==
                UUID("3b241101-1111-bbbb-cccc-4136c566a962"));
        }
    }

    SECTION("create throws for duplicate pk if update is not specified") {
        create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},
        });
        REQUIRE_THROWS(create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"_id", INT64_C(10)}, {"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}}},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},
        }));
    }

    SECTION("create with explicit null pk does not fall back to default") {
        d.defaults["nullable int pk"] = {
            {"_id", INT64_C(10)},
        };
        d.defaults["nullable string pk"] = {
            {"_id", "value"s},
        };
        auto create = [&](util::Any&& value, StringData type) {
            r->begin_transaction();
            auto obj = Object::create(d, r, *r->schema().find(type), value);
            r->commit_transaction();
            return obj;
        };

        auto obj = create(AnyDict{{"_id", d.null_value()}}, "nullable int pk");
        auto col_pk_int = r->read_group().get_table("class_nullable int pk")->get_column_key("_id");
        auto col_pk_str = r->read_group().get_table("class_nullable string pk")->get_column_key("_id");
        REQUIRE(obj.obj().is_null(col_pk_int));
        obj = create(AnyDict{{"_id", d.null_value()}}, "nullable string pk");
        REQUIRE(obj.obj().is_null(col_pk_str));

        obj = create(AnyDict{{}}, "nullable int pk");
        REQUIRE(obj.obj().get<util::Optional<Int>>(col_pk_int) == 10);
        obj = create(AnyDict{{}}, "nullable string pk");
        REQUIRE(obj.obj().get<String>(col_pk_str) == "value");
    }

    SECTION("create null and 0 primary keys for Int types") {
        auto create = [&](util::Any&& value, StringData type) {
            r->begin_transaction();
            auto obj = Object::create(d, r, *r->schema().find(type), value);
            r->commit_transaction();
            return obj;
        };
        create(AnyDict{{"_id", util::Any()}}, "all optional types");
        create(AnyDict{{"_id", INT64_C(0)}}, "all optional types");
        REQUIRE(Results(r, r->read_group().get_table("class_all optional types")).size() == 2);
    }

    SECTION("create null and default primary keys for ObjectId types") {
        auto create = [&](util::Any&& value, StringData type) {
            r->begin_transaction();
            auto obj = Object::create(d, r, *r->schema().find(type), value);
            r->commit_transaction();
            return obj;
        };
        create(AnyDict{{"_id", util::Any()}}, "nullable object id pk");
        create(AnyDict{{"_id", ObjectId::gen()}}, "nullable object id pk");
        REQUIRE(Results(r, r->read_group().get_table("class_nullable object id pk")).size() == 2);
    }

    SECTION("getters and setters") {
        r->begin_transaction();

        auto table = r->read_group().get_table("class_all types");
        table->create_object_with_primary_key(1);
        Object obj(r, *r->schema().find("all types"), *table->begin());

        auto link_table = r->read_group().get_table("class_link target");
        link_table->create_object_with_primary_key(0);
        Object linkobj(r, *r->schema().find("link target"), *link_table->begin());

        obj.set_property_value(d, "bool", util::Any(true));
        REQUIRE(any_cast<bool>(obj.get_property_value<util::Any>(d, "bool")) == true);

        obj.set_property_value(d, "int", util::Any(INT64_C(5)));
        REQUIRE(any_cast<int64_t>(obj.get_property_value<util::Any>(d, "int")) == 5);

        obj.set_property_value(d, "float", util::Any(1.23f));
        REQUIRE(any_cast<float>(obj.get_property_value<util::Any>(d, "float")) == 1.23f);

        obj.set_property_value(d, "double", util::Any(1.23));
        REQUIRE(any_cast<double>(obj.get_property_value<util::Any>(d, "double")) == 1.23);

        obj.set_property_value(d, "string", util::Any("abc"s));
        REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(d, "string")) == "abc");

        obj.set_property_value(d, "data", util::Any("abc"s));
        REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(d, "data")) == "abc");

        obj.set_property_value(d, "date", util::Any(Timestamp(1, 2)));
        REQUIRE(any_cast<Timestamp>(obj.get_property_value<util::Any>(d, "date")) == Timestamp(1, 2));

        obj.set_property_value(d, "object id", util::Any(ObjectId("111111111111111111111111")));
        REQUIRE(any_cast<ObjectId>(obj.get_property_value<util::Any>(d, "object id")) ==
                ObjectId("111111111111111111111111"));

        obj.set_property_value(d, "decimal", util::Any(Decimal128("42.4242e42")));
        REQUIRE(any_cast<Decimal128>(obj.get_property_value<util::Any>(d, "decimal")) == Decimal128("42.4242e42"));

        obj.set_property_value(d, "uuid", util::Any(UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")));
        REQUIRE(any_cast<UUID>(obj.get_property_value<util::Any>(d, "uuid")) ==
                UUID("3b241101-aaaa-bbbb-cccc-4136c566a962"));

        obj.set_property_value(d, "mixed", util::Any(25));
        REQUIRE(any_cast<int64_t>(obj.get_property_value<util::Any>(d, "mixed")) == 25);
        obj.set_property_value(d, "mixed", util::Any("Hello"s));
        REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(d, "mixed")) == "Hello");
        obj.set_property_value(d, "mixed", util::Any(1.23));
        REQUIRE(any_cast<double>(obj.get_property_value<util::Any>(d, "mixed")) == 1.23);
        obj.set_property_value(d, "mixed", util::Any(123.45f));
        REQUIRE(any_cast<float>(obj.get_property_value<util::Any>(d, "mixed")) == 123.45f);
        obj.set_property_value(d, "mixed", util::Any(Timestamp(30, 40)));
        REQUIRE(any_cast<Timestamp>(obj.get_property_value<util::Any>(d, "mixed")) == Timestamp(30, 40));
        obj.set_property_value(d, "mixed", util::Any(ObjectId("111111111111111111111111")));
        REQUIRE(any_cast<ObjectId>(obj.get_property_value<util::Any>(d, "mixed")) ==
                ObjectId("111111111111111111111111"));
        obj.set_property_value(d, "mixed", util::Any(Decimal128("42.4242e42")));
        REQUIRE(any_cast<Decimal128>(obj.get_property_value<util::Any>(d, "mixed")) == Decimal128("42.4242e42"));
        obj.set_property_value(d, "mixed", util::Any(UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")));
        REQUIRE(any_cast<UUID>(obj.get_property_value<util::Any>(d, "mixed")) ==
                UUID("3b241101-aaaa-bbbb-cccc-4136c566a962"));

        obj.set_property_value(d, "dictionary", util::Any(AnyDict({{"k1", "v1"s}, {"k2", "v2"s}})));
        auto dict = any_cast<AnyDict&&>(obj.get_property_value<util::Any>(d, "dictionary"));
        REQUIRE(util::any_cast<std::string>(dict["k1"]) == "v1");
        REQUIRE(util::any_cast<std::string>(dict["k2"]) == "v2");

        REQUIRE_FALSE(obj.get_property_value<util::Any>(d, "object").has_value());
        obj.set_property_value(d, "object", util::Any(linkobj));
        REQUIRE(any_cast<Object>(obj.get_property_value<util::Any>(d, "object")).obj().get_key() ==
                linkobj.obj().get_key());

        auto linking = any_cast<Results>(linkobj.get_property_value<util::Any>(d, "origin"));
        REQUIRE(linking.size() == 1);

        REQUIRE_THROWS(obj.set_property_value(d, "_id", util::Any(INT64_C(5))));
        REQUIRE_THROWS(obj.set_property_value(d, "not a property", util::Any(INT64_C(5))));

        r->commit_transaction();

        REQUIRE_THROWS(obj.get_property_value<util::Any>(d, "not a property"));
        REQUIRE_THROWS(obj.set_property_value(d, "int", util::Any(INT64_C(5))));
    }

    SECTION("list property self-assign is a no-op") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"bool", true},
            {"int", INT64_C(5)},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object id", ObjectId("000000000000000000000001")},
            {"decimal", Decimal128("1.23e45")},
            {"uuid", UUID("3b241101-aaaa-bbbb-cccc-4136c566a962")},
            {"dictionary", AnyDict{{"key", "value"s}}},

            {"bool array", AnyVec{true, false}},
            {"object array", AnyVec{AnyDict{{"_id", INT64_C(20)}, {"value", INT64_C(20)}}}},
        });

        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).size() == 2);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object array")).size() == 1);

        r->begin_transaction();
        obj.set_property_value(d, "bool array", obj.get_property_value<util::Any>(d, "bool array"));
        obj.set_property_value(d, "object array", obj.get_property_value<util::Any>(d, "object array"));
        r->commit_transaction();

        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "bool array")).size() == 2);
        REQUIRE(any_cast<List&&>(obj.get_property_value<util::Any>(d, "object array")).size() == 1);
    }

#if REALM_ENABLE_SYNC
    if (!util::EventLoop::has_implementation())
        return;
    SECTION("defaults do not override values explicitly passed to create()") {
        TestSyncManager init_sync_manager({}, {false});
        auto& server = init_sync_manager.sync_server();
        SyncTestFile config1(init_sync_manager.app(), "shared");
        config1.schema = config.schema;
        SyncTestFile config2(init_sync_manager.app(), "shared");
        config2.schema = config.schema;

        AnyDict v1{
            {"_id", INT64_C(7)},
            {"array 1", AnyVector{AnyDict{{"_id", INT64_C(1)}, {"value", INT64_C(1)}}}},
            {"array 2", AnyVector{AnyDict{{"_id", INT64_C(2)}, {"value", INT64_C(2)}}}},
        };
        auto v2 = v1;
        v1["int 1"] = INT64_C(1);
        v2["int 2"] = INT64_C(2);
        v2["array 1"] = AnyVector{AnyDict{{"_id", INT64_C(3)}, {"value", INT64_C(1)}}};
        v2["array 2"] = AnyVector{AnyDict{{"_id", INT64_C(4)}, {"value", INT64_C(2)}}};

        auto r1 = Realm::get_shared_realm(config1);
        auto r2 = Realm::get_shared_realm(config2);

        TestContext c1(r1);
        TestContext c2(r2);

        c1.defaults["pk after list"] = {
            {"int 1", INT64_C(10)},
            {"int 2", INT64_C(10)},
        };
        c2.defaults = c1.defaults;

        r1->begin_transaction();
        r2->begin_transaction();
        auto object1 = Object::create(c1, r1, *r1->schema().find("pk after list"), util::Any(v1));
        auto object2 = Object::create(c2, r2, *r2->schema().find("pk after list"), util::Any(v2));
        r2->commit_transaction();
        r1->commit_transaction();

        server.start();
        util::EventLoop::main().run_until([&] {
            return r1->read_group().get_table("class_array target")->size() == 4;
        });

        Obj obj = object1.obj();
        REQUIRE(obj.get<Int>("_id") == 7); // pk
        REQUIRE(obj.get_linklist("array 1").size() == 2);
        REQUIRE(obj.get<Int>("int 1") == 1); // non-default from r1
        REQUIRE(obj.get<Int>("int 2") == 2); // non-default from r2
        REQUIRE(obj.get_linklist("array 2").size() == 2);
    }
#endif
}

TEST_CASE("Embedded Object") {
    Schema schema{
        {"all types",
         {
             {"_id", PropertyType::Int, Property::IsPrimary{true}},
             {"object", PropertyType::Object | PropertyType::Nullable, "link target"},
             {"array", PropertyType::Object | PropertyType::Array, "array target"},
         }},
        {"all types no pk",
         {
             {"value", PropertyType::Int},
             {"object", PropertyType::Object | PropertyType::Nullable, "link target"},
             {"array", PropertyType::Object | PropertyType::Array, "array target"},
         }},
        {"link target",
         ObjectSchema::IsEmbedded{true},
         {
             {"value", PropertyType::Int},
         }},
        {"array target",
         ObjectSchema::IsEmbedded{true},
         {
             {"value", PropertyType::Int},
         }},
    };
    InMemoryTestFile config;
    config.automatic_change_notifications = false;
    config.schema_mode = SchemaMode::Automatic;
    config.schema = schema;

    auto realm = Realm::get_shared_realm(config);
    CppContext ctx(realm);

    auto create = [&](util::Any&& value, CreatePolicy policy = CreatePolicy::UpdateAll) {
        realm->begin_transaction();
        auto obj = Object::create(ctx, realm, *realm->schema().find("all types"), value, policy);
        realm->commit_transaction();
        return obj;
    };

    SECTION("Basic object creation") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"object", AnyDict{{"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}, AnyDict{{"value", INT64_C(30)}}}},
        });

        REQUIRE(obj.obj().get<int64_t>("_id") == 1);
        auto linked_obj = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object")).obj();
        REQUIRE(linked_obj.is_valid());
        REQUIRE(linked_obj.get<int64_t>("value") == 10);
        auto list = any_cast<List>(obj.get_property_value<util::Any>(ctx, "array"));
        REQUIRE(list.size() == 2);
        REQUIRE(list.get(0).get<int64_t>("value") == 20);
        REQUIRE(list.get(1).get<int64_t>("value") == 30);
    }

    SECTION("set_property_value() on link to embedded object") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"object", AnyDict{{"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}, AnyDict{{"value", INT64_C(30)}}}},
        });

        SECTION("throws when given a managed object") {
            realm->begin_transaction();
            REQUIRE_THROWS_WITH(
                obj.set_property_value(ctx, "object", obj.get_property_value<util::Any>(ctx, "object")),
                "Cannot set a link to an existing managed embedded object");
            realm->cancel_transaction();
        }

        SECTION("replaces object when given a dictionary and CreatePolicy::UpdateAll") {
            realm->begin_transaction();
            auto old_linked = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object"));
            obj.set_property_value(ctx, "object", util::Any(AnyDict{{"value", INT64_C(40)}}));
            auto new_linked = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object"));
            REQUIRE_FALSE(old_linked.is_valid());
            REQUIRE(new_linked.obj().get<int64_t>("value") == 40);
            realm->cancel_transaction();
        }

        SECTION("mutates existing object when given a dictionary and CreatePolicy::UpdateModified") {
            realm->begin_transaction();
            auto old_linked = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object"));
            obj.set_property_value(ctx, "object", util::Any(AnyDict{{"value", INT64_C(40)}}),
                                   CreatePolicy::UpdateModified);
            auto new_linked = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object"));
            REQUIRE(old_linked.is_valid());
            REQUIRE(old_linked.obj() == new_linked.obj());
            REQUIRE(new_linked.obj().get<int64_t>("value") == 40);
            realm->cancel_transaction();
        }

        SECTION("can set embedded link to null") {
            realm->begin_transaction();
            auto old_linked = any_cast<Object>(obj.get_property_value<util::Any>(ctx, "object"));
            obj.set_property_value(ctx, "object", util::Any());
            auto new_linked = obj.get_property_value<util::Any>(ctx, "object");
            REQUIRE_FALSE(old_linked.is_valid());
            REQUIRE_FALSE(new_linked.has_value());
            realm->cancel_transaction();
        }
    }

    SECTION("set_property_value() on list of embedded objects") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"array", AnyVector{AnyDict{{"value", INT64_C(1)}}, AnyDict{{"value", INT64_C(2)}}}},
        });
        List list(realm, obj.obj().get_linklist("array"));
        auto obj2 = create(AnyDict{
            {"_id", INT64_C(2)},
            {"array", AnyVector{AnyDict{{"value", INT64_C(1)}}, AnyDict{{"value", INT64_C(2)}}}},
        });
        List list2(realm, obj2.obj().get_linklist("array"));

        SECTION("throws when given a managed object") {
            realm->begin_transaction();
            REQUIRE_THROWS_WITH(obj.set_property_value(ctx, "array", util::Any{AnyVector{list2.get(0)}}),
                                "Cannot add an existing managed embedded object to a List.");
            realm->cancel_transaction();
        }

        SECTION("replaces objects when given a dictionary and CreatePolicy::UpdateAll") {
            realm->begin_transaction();
            auto old_obj_1 = list.get(0);
            auto old_obj_2 = list.get(1);
            obj.set_property_value(ctx, "array",
                                   util::Any(AnyVector{AnyDict{{"value", INT64_C(1)}}, AnyDict{{"value", INT64_C(2)}},
                                                       AnyDict{{"value", INT64_C(3)}}}),
                                   CreatePolicy::UpdateAll);
            REQUIRE(list.size() == 3);
            REQUIRE_FALSE(old_obj_1.is_valid());
            REQUIRE_FALSE(old_obj_2.is_valid());
            REQUIRE(list.get(0).get<int64_t>("value") == 1);
            REQUIRE(list.get(1).get<int64_t>("value") == 2);
            REQUIRE(list.get(2).get<int64_t>("value") == 3);
            realm->cancel_transaction();
        }

        SECTION("mutates existing objects when given a dictionary and CreatePolicy::UpdateModified") {
            realm->begin_transaction();
            auto old_obj_1 = list.get(0);
            auto old_obj_2 = list.get(1);
            obj.set_property_value(ctx, "array",
                                   util::Any(AnyVector{AnyDict{{"value", INT64_C(1)}}, AnyDict{{"value", INT64_C(2)}},
                                                       AnyDict{{"value", INT64_C(3)}}}),
                                   CreatePolicy::UpdateModified);
            REQUIRE(list.size() == 3);
            REQUIRE(old_obj_1.is_valid());
            REQUIRE(old_obj_2.is_valid());
            REQUIRE(old_obj_1.get<int64_t>("value") == 1);
            REQUIRE(old_obj_2.get<int64_t>("value") == 2);
            REQUIRE(list.get(2).get<int64_t>("value") == 3);
            realm->cancel_transaction();
        }

        SECTION("clears list when given null") {
            realm->begin_transaction();
            obj.set_property_value(ctx, "array", util::Any());
            REQUIRE(list.size() == 0);
            realm->cancel_transaction();
        }
    }

    SECTION("create with UpdateModified diffs child objects") {
        auto obj = create(AnyDict{
            {"_id", INT64_C(1)},
            {"object", AnyDict{{"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}, AnyDict{{"value", INT64_C(30)}}}},
        });

        auto array_table = realm->read_group().get_table("class_array target");
        Results result(realm, array_table);

        bool obj_callback_called = false;
        auto token = obj.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            obj_callback_called = true;
        });
        bool list_callback_called = false;
        auto token1 = result.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
            list_callback_called = true;
        });
        advance_and_notify(*realm);

        // Update with identical value
        create(
            AnyDict{
                {"_id", INT64_C(1)},
                {"object", AnyDict{{"value", INT64_C(10)}}},
            },
            CreatePolicy::UpdateModified);

        obj_callback_called = false;
        list_callback_called = false;
        advance_and_notify(*realm);
        REQUIRE(!obj_callback_called);
        REQUIRE(!list_callback_called);

        // Update with different values
        create(
            AnyDict{
                {"_id", INT64_C(1)},
                {"array", AnyVector{AnyDict{{"value", INT64_C(40)}}, AnyDict{{"value", INT64_C(50)}}}},
            },
            CreatePolicy::UpdateModified);

        obj_callback_called = false;
        list_callback_called = false;
        advance_and_notify(*realm);
        REQUIRE(!obj_callback_called);
        REQUIRE(list_callback_called);
    }

    SECTION("deleting parent object sends change notification") {
        auto parent = create(AnyDict{
            {"_id", INT64_C(1)},
            {"object", AnyDict{{"value", INT64_C(10)}}},
            {"array", AnyVector{AnyDict{{"value", INT64_C(20)}}, AnyDict{{"value", INT64_C(30)}}}},
        });

        CppContext ctx(realm);
        auto child = any_cast<Object>(parent.get_property_value<util::Any>(ctx, "object"));

        int calls = 0;
        auto token = child.add_notification_callback([&](CollectionChangeSet const& c, std::exception_ptr) {
            if (++calls == 2) {
                REQUIRE_INDICES(c.deletions, 0);
            }
        });
        advance_and_notify(*realm);
        REQUIRE(calls == 1);

        realm->begin_transaction();
        parent.obj().remove();
        realm->commit_transaction();
        advance_and_notify(*realm);
        REQUIRE(calls == 2);
    }
}
