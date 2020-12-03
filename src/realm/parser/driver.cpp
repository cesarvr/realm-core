#include "realm/parser/driver.hpp"
#include "realm/parser/keypath_mapping.hpp"
#include "realm/parser/query_parser.hpp"
#include "realm/sort_descriptor.hpp"
#include <realm/decimal128.hpp>
#include <realm/uuid.hpp>
#include "realm/util/base64.hpp"
#include "realm/parser/generated/query_flex.hpp"

using namespace realm;
using namespace std::string_literals;

// Whether to generate parser debug traces.
static bool trace_parsing = false;
// Whether to generate scanner debug traces.
static bool trace_scanning = false;

namespace {

StringData get_printable_table_name(StringData name)
{
    // the "class_" prefix is an implementation detail of the object store that shouldn't be exposed to users
    static const std::string prefix = "class_";
    if (name.size() > prefix.size() && strncmp(name.data(), prefix.data(), prefix.size()) == 0) {
        name = StringData(name.data() + prefix.size(), name.size() - prefix.size());
    }
    return name;
}

const char* agg_op_type_to_str(query_parser::AggrNode::Type type)
{
    switch (type) {
        case realm::query_parser::AggrNode::MAX:
            return ".@max";
        case realm::query_parser::AggrNode::MIN:
            return ".@min";
        case realm::query_parser::AggrNode::SUM:
            return ".@sum";
        case realm::query_parser::AggrNode::AVG:
            return ".@avg";
    }
    return "";
}

const char* expression_cmp_type_to_str(ExpressionComparisonType type)
{
    switch (type) {
        case ExpressionComparisonType::Any:
            return "ANY";
        case ExpressionComparisonType::All:
            return "ALL";
        case ExpressionComparisonType::None:
            return "NONE";
    }
    return "";
}

bool is_length_suffix(const std::string& s)
{
    return s.size() == 6 && (s[0] == 'l' || s[0] == 'L') && (s[1] == 'e' || s[1] == 'E') &&
           (s[2] == 'n' || s[2] == 'N') && (s[3] == 'g' || s[3] == 'G') && (s[4] == 't' || s[4] == 'T') &&
           (s[5] == 'h' || s[5] == 'H');
}

// Converts ascii c-locale uppercase characters to lower case,
// leaves other char values unchanged.
inline char toLowerAscii(char c)
{
    if (isascii(c) && isupper(c)) {
#if REALM_ANDROID
        return tolower(c); // _tolower is not supported on all ABI levels
#else
        return _tolower(c);
#endif
    }
    return c;
}

template <typename T>
inline bool try_parse_specials(std::string str, T& ret)
{
    if constexpr (realm::is_any<T, float, double>::value || std::numeric_limits<T>::is_iec559) {
        std::transform(str.begin(), str.end(), str.begin(), toLowerAscii);
        if (std::numeric_limits<T>::has_quiet_NaN && (str == "nan" || str == "+nan")) {
            ret = std::numeric_limits<T>::quiet_NaN();
            return true;
        }
        else if (std::numeric_limits<T>::has_quiet_NaN && (str == "-nan")) {
            ret = -std::numeric_limits<T>::quiet_NaN();
            return true;
        }
        else if (std::numeric_limits<T>::has_infinity &&
                 (str == "+infinity" || str == "infinity" || str == "+inf" || str == "inf")) {
            ret = std::numeric_limits<T>::infinity();
            return true;
        }
        else if (std::numeric_limits<T>::has_infinity && (str == "-infinity" || str == "-inf")) {
            ret = -std::numeric_limits<T>::infinity();
            return true;
        }
    }
    return false;
}

template <typename T>
inline const char* get_type_name()
{
    return "unknown";
}
template <>
inline const char* get_type_name<int64_t>()
{
    return "number";
}
template <>
inline const char* get_type_name<float>()
{
    return "floating point number";
}
template <>
inline const char* get_type_name<double>()
{
    return "floating point number";
}

template <typename T>
inline T string_to(const std::string& s)
{
    std::istringstream iss(s);
    iss.imbue(std::locale::classic());
    T value;
    iss >> value;
    if (iss.fail()) {
        if (!try_parse_specials(s, value)) {
            throw std::invalid_argument(util::format("Cannot convert '%1' to a %2", s, get_type_name<T>()));
        }
    }
    return value;
}

class MixedArguments : public query_parser::Arguments {
public:
    MixedArguments(const std::vector<Mixed>& args)
        : m_args(args)
    {
    }
    bool bool_for_argument(size_t n) final
    {
        return m_args.at(n).get<bool>();
    }
    long long long_for_argument(size_t n) final
    {
        return m_args.at(n).get<int64_t>();
    }
    float float_for_argument(size_t n) final
    {
        return m_args.at(n).get<float>();
    }
    double double_for_argument(size_t n) final
    {
        return m_args.at(n).get<double>();
    }
    StringData string_for_argument(size_t n) final
    {
        return m_args.at(n).get<StringData>();
    }
    BinaryData binary_for_argument(size_t n) final
    {
        return m_args.at(n).get<BinaryData>();
    }
    Timestamp timestamp_for_argument(size_t n) final
    {
        return m_args.at(n).get<Timestamp>();
    }
    ObjectId objectid_for_argument(size_t n) final
    {
        return m_args.at(n).get<ObjectId>();
    }
    UUID uuid_for_argument(size_t n) final
    {
        return m_args.at(n).get<UUID>();
    }
    Decimal128 decimal128_for_argument(size_t n) final
    {
        return m_args.at(n).get<Decimal128>();
    }
    ObjKey object_index_for_argument(size_t n) final
    {
        return m_args.at(n).get<ObjKey>();
    }
    bool is_argument_null(size_t n) final
    {
        return m_args.at(n).is_null();
    }
    DataType type_for_argument(size_t n)
    {
        return m_args.at(n).get_type();
    }

private:
    const std::vector<Mixed>& m_args;
};

} // namespace

namespace realm {

namespace query_parser {

NoArguments ParserDriver::s_default_args;
query_parser::KeyPathMapping ParserDriver::s_default_mapping;

Arguments::~Arguments() {}

Timestamp get_timestamp_if_valid(int64_t seconds, int32_t nanoseconds)
{
    const bool both_non_negative = seconds >= 0 && nanoseconds >= 0;
    const bool both_non_positive = seconds <= 0 && nanoseconds <= 0;
    if (both_non_negative || both_non_positive) {
        return Timestamp(seconds, nanoseconds);
    }
    throw std::runtime_error("Invalid timestamp format");
}

ParserNode::~ParserNode() {}

AtomPredNode::~AtomPredNode() {}

Query NotNode::visit(ParserDriver* drv)
{
    Query query = atom_pred->visit(drv);
    Query q = drv->m_base_table->where();
    q.Not();
    q.and_query(query);
    return {q};
}

Query ParensNode::visit(ParserDriver* drv)
{
    return pred->visit(drv);
}

Query OrNode::visit(ParserDriver* drv)
{
    if (and_preds.size() == 1) {
        return and_preds[0]->visit(drv);
    }

    Query q(drv->m_base_table);
    q.group();
    for (auto it : and_preds) {
        q.Or();
        q.and_query(it->visit(drv));
    }
    q.end_group();

    return q;
}

Query AndNode::visit(ParserDriver* drv)
{
    if (atom_preds.size() == 1) {
        return atom_preds[0]->visit(drv);
    }
    Query q(drv->m_base_table);
    for (auto it : atom_preds) {
        q.and_query(it->visit(drv));
    }
    return q;
}

Query EqualityNode::visit(ParserDriver* drv)
{
    auto [left, right] = drv->cmp(values);

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    if (left_type >= 0 && right_type >= 0 && !Mixed::data_types_are_comparable(left_type, right_type)) {
        throw std::invalid_argument(util::format("Unsupported comparison between type '%1' and type '%2'",
                                                 get_data_type_name(left_type), get_data_type_name(right_type)));
    }

    if (op == CompareNode::IN) {
        Subexpr* r = right.get();
        if (!r->has_multiple_values()) {
            throw std::invalid_argument("The keypath following 'IN' must contain a list");
        }
    }

    const ObjPropertyBase* prop = dynamic_cast<const ObjPropertyBase*>(left.get());
    if (right->has_constant_evaluation() && left_type == right_type) {
        Mixed val = right->get_mixed();
        if (prop && !prop->links_exist()) {
            auto col_key = prop->column_key();
            if (val.is_null()) {
                switch (op) {
                    case CompareNode::EQUAL:
                    case CompareNode::IN:
                        return drv->m_base_table->where().equal(col_key, realm::null());
                    case CompareNode::NOT_EQUAL:
                        return drv->m_base_table->where().not_equal(col_key, realm::null());
                }
            }
            switch (left->get_type()) {
                case type_Int:
                    return drv->simple_query(op, col_key, val.get_int());
                case type_Bool:
                    return drv->simple_query(op, col_key, val.get_bool());
                case type_String:
                    return drv->simple_query(op, col_key, val.get_string(), case_sensitive);
                    break;
                case type_Binary:
                    return drv->simple_query(op, col_key, val.get_binary(), case_sensitive);
                    break;
                case type_Timestamp:
                    return drv->simple_query(op, col_key, val.get<Timestamp>());
                case type_Float:
                    return drv->simple_query(op, col_key, val.get_float());
                    break;
                case type_Double:
                    return drv->simple_query(op, col_key, val.get_double());
                    break;
                case type_Decimal:
                    return drv->simple_query(op, col_key, val.get<Decimal128>());
                    break;
                case type_ObjectId:
                    break;
                case type_UUID:
                    return drv->simple_query(op, col_key, val.get<UUID>());
                    break;
                default:
                    break;
            }
        }
        else if (left_type == type_Link) {
            auto link_column = dynamic_cast<const Columns<Link>*>(left.get());
            if (link_column && link_column->link_map().get_nb_hops() == 1) {
                // We can fall back to Query::links_to for != and == operations on links, but only
                // for == on link lists. This is because negating query.links_to() is equivalent to
                // to "ALL linklist != row" rather than the "ANY linklist != row" semantics we're after.
                if (op == CompareNode::EQUAL) {
                    return drv->m_base_table->where().links_to(link_column->link_map().get_first_column_key(),
                                                               val.get<ObjKey>());
                }
            }
        }
    }
    if (case_sensitive) {
        switch (op) {
            case CompareNode::EQUAL:
            case CompareNode::IN:
                return Query(std::unique_ptr<Expression>(new Compare<Equal>(std::move(right), std::move(left))));
            case CompareNode::NOT_EQUAL:
                return Query(std::unique_ptr<Expression>(new Compare<NotEqual>(std::move(right), std::move(left))));
        }
    }
    else {
        switch (op) {
            case CompareNode::EQUAL:
            case CompareNode::IN:
                return Query(std::unique_ptr<Expression>(new Compare<EqualIns>(std::move(right), std::move(left))));
            case CompareNode::NOT_EQUAL:
                return Query(
                    std::unique_ptr<Expression>(new Compare<NotEqualIns>(std::move(right), std::move(left))));
        }
    }
    return {};
}

static std::map<int, std::string> opstr = {
    {CompareNode::GREATER, ">"},
    {CompareNode::LESS, "<"},
    {CompareNode::GREATER_EQUAL, ">="},
    {CompareNode::LESS_EQUAL, "<="},
    {CompareNode::BEGINSWITH, "beginswith"},
    {CompareNode::ENDSWITH, "endswith"},
    {CompareNode::CONTAINS, "contains"},
    {CompareNode::LIKE, "like"},
};

Query RelationalNode::visit(ParserDriver* drv)
{
    auto [left, right] = drv->cmp(values);

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    if (left_type == type_UUID) {
        throw std::logic_error(util::format(
            "Unsupported operator %1 in query. Only equal (==) and not equal (!=) are supported for this type.",
            opstr[op]));
    }

    if (left_type < 0 || right_type < 0 || !Mixed::data_types_are_comparable(left_type, right_type)) {
        throw std::runtime_error(util::format("Unsupported comparison between type '%1' and type '%2'",
                                              get_data_type_name(left_type), get_data_type_name(right_type)));
    }

    const ObjPropertyBase* prop = dynamic_cast<const ObjPropertyBase*>(left.get());
    if (prop && !prop->links_exist() && right->has_constant_evaluation() && left_type == right_type) {
        auto col_key = prop->column_key();
        switch (left->get_type()) {
            case type_Int:
                return drv->simple_query(op, col_key, right->get_mixed().get_int());
            case type_Bool:
                break;
            case type_String:
                break;
            case type_Binary:
                break;
            case type_Timestamp:
                return drv->simple_query(op, col_key, right->get_mixed().get<Timestamp>());
            case type_Float:
                return drv->simple_query(op, col_key, right->get_mixed().get_float());
                break;
            case type_Double:
                return drv->simple_query(op, col_key, right->get_mixed().get_double());
                break;
            case type_Decimal:
                return drv->simple_query(op, col_key, right->get_mixed().get<Decimal128>());
                break;
            case type_ObjectId:
                break;
            case type_UUID:
                break;
            default:
                break;
        }
    }
    switch (op) {
        case CompareNode::GREATER:
            return Query(std::unique_ptr<Expression>(new Compare<Less>(std::move(right), std::move(left))));
        case CompareNode::LESS:
            return Query(std::unique_ptr<Expression>(new Compare<Greater>(std::move(right), std::move(left))));
        case CompareNode::GREATER_EQUAL:
            return Query(std::unique_ptr<Expression>(new Compare<LessEqual>(std::move(right), std::move(left))));
        case CompareNode::LESS_EQUAL:
            return Query(std::unique_ptr<Expression>(new Compare<GreaterEqual>(std::move(right), std::move(left))));
    }
    return {};
}

Query StringOpsNode::visit(ParserDriver* drv)
{
    auto [left, right] = drv->cmp(values);

    auto right_type = right->get_type();
    const ObjPropertyBase* prop = dynamic_cast<const ObjPropertyBase*>(left.get());

    if (right_type != type_String && right_type != type_Binary) {
        throw std::runtime_error(util::format(
            "Unsupported comparison operator '%1' against type '%2', right side must be a string or binary type",
            opstr[op], get_data_type_name(right_type)));
    }

    if (prop && !prop->links_exist() && right->has_constant_evaluation() && left->get_type() == right_type) {
        auto col_key = prop->column_key();
        if (right_type == type_String) {
            StringData val = right->get_mixed().get_string();

            switch (op) {
                case CompareNode::BEGINSWITH:
                    return drv->m_base_table->where().begins_with(col_key, val, case_sensitive);
                case CompareNode::ENDSWITH:
                    return drv->m_base_table->where().ends_with(col_key, val, case_sensitive);
                case CompareNode::CONTAINS:
                    return drv->m_base_table->where().contains(col_key, val, case_sensitive);
                case CompareNode::LIKE:
                    return drv->m_base_table->where().like(col_key, val, case_sensitive);
            }
        }
        else if (right_type == type_Binary) {
            BinaryData val = right->get_mixed().get_binary();

            switch (op) {
                case CompareNode::BEGINSWITH:
                    return drv->m_base_table->where().begins_with(col_key, val, case_sensitive);
                case CompareNode::ENDSWITH:
                    return drv->m_base_table->where().ends_with(col_key, val, case_sensitive);
                case CompareNode::CONTAINS:
                    return drv->m_base_table->where().contains(col_key, val, case_sensitive);
                case CompareNode::LIKE:
                    return drv->m_base_table->where().like(col_key, val, case_sensitive);
            }
        }
    }

    if (case_sensitive) {
        switch (op) {
            case CompareNode::BEGINSWITH:
                return Query(std::unique_ptr<Expression>(new Compare<BeginsWith>(std::move(right), std::move(left))));
            case CompareNode::ENDSWITH:
                return Query(std::unique_ptr<Expression>(new Compare<EndsWith>(std::move(right), std::move(left))));
            case CompareNode::CONTAINS:
                return Query(std::unique_ptr<Expression>(new Compare<Contains>(std::move(right), std::move(left))));
            case CompareNode::LIKE:
                return Query(std::unique_ptr<Expression>(new Compare<Like>(std::move(right), std::move(left))));
        }
    }
    else {
        switch (op) {
            case CompareNode::BEGINSWITH:
                return Query(
                    std::unique_ptr<Expression>(new Compare<BeginsWithIns>(std::move(right), std::move(left))));
            case CompareNode::ENDSWITH:
                return Query(
                    std::unique_ptr<Expression>(new Compare<EndsWithIns>(std::move(right), std::move(left))));
            case CompareNode::CONTAINS:
                return Query(
                    std::unique_ptr<Expression>(new Compare<ContainsIns>(std::move(right), std::move(left))));
            case CompareNode::LIKE:
                return Query(std::unique_ptr<Expression>(new Compare<LikeIns>(std::move(right), std::move(left))));
        }
    }
    return {};
}

Query TrueOrFalseNode::visit(ParserDriver* drv)
{
    Query q = drv->m_base_table->where();
    if (true_or_false) {
        q.and_query(std::unique_ptr<realm::Expression>(new TrueExpression));
    }
    else {
        q.and_query(std::unique_ptr<realm::Expression>(new FalseExpression));
    }
    return q;
}

std::unique_ptr<Subexpr> PropNode::visit(ParserDriver* drv)
{
    if (identifier == "@links") {
        // This is a backlink aggregate query
        auto link_chain = path->visit(drv, comp_type);
        auto sub = link_chain.get_backlink_count<Int>();
        return sub.clone();
    }
    else {
        try {
            auto link_chain = path->visit(drv, comp_type);
            std::unique_ptr<Subexpr> subexpr{drv->column(link_chain, identifier)};

            if (post_op) {
                return post_op->visit(drv, subexpr.get());
            }
            return subexpr;
        }
        catch (const std::runtime_error& e) {
            // Is 'identifier' perhaps length operator?
            if (!post_op && is_length_suffix(identifier) && path->path_elems.size() > 0) {
                // If 'length' is the operator, the last id in the path must be the name
                // of a list property
                auto prop = path->path_elems.back();
                path->path_elems.pop_back();
                std::unique_ptr<Subexpr> subexpr{path->visit(drv, comp_type).column(prop)};
                if (auto list = dynamic_cast<ColumnListBase*>(subexpr.get())) {
                    if (auto length_expr = list->get_element_length())
                        return length_expr;
                }
            }
            throw e;
        }
    }
    REALM_UNREACHABLE();
    return {};
}

std::unique_ptr<Subexpr> SubqueryNode::visit(ParserDriver* drv)
{
    if (variable_name.size() < 2 || variable_name[0] != '$') {
        throw std::runtime_error(util::format("The subquery variable '%1' is invalid. The variable must start with "
                                              "'$' and cannot be empty; for example '$x'.",
                                              variable_name));
    }
    LinkChain lc = prop->path->visit(drv, prop->comp_type);
    drv->translate(lc, prop->identifier);

    if (prop->identifier.find("@links") == 0) {
        drv->backlink(lc, prop->identifier);
    }
    else {
        ColKey col_key = lc.get_current_table()->get_column_key(prop->identifier);
        if (col_key.is_list() && col_key.get_type() != col_type_LinkList) {
            throw std::runtime_error(util::format(
                "A subquery can not operate on a list of primitive values (property '%1')", prop->identifier));
        }
        if (col_key.get_type() != col_type_LinkList) {
            throw std::runtime_error(util::format("A subquery must operate on a list property, but '%1' is type '%2'",
                                                  prop->identifier,
                                                  realm::get_data_type_name(DataType(col_key.get_type()))));
        }
        lc.link(prop->identifier);
    }
    TableRef previous_table = drv->m_base_table;
    drv->m_base_table = lc.get_current_table().cast_away_const();
    bool did_add = drv->m_mapping.add_mapping(drv->m_base_table, variable_name, "");
    if (!did_add) {
        throw std::runtime_error(util::format("Unable to create a subquery expression with variable '%1' since an "
                                              "identical variable already exists in this context",
                                              variable_name));
    }
    Query sub = subquery->visit(drv);
    drv->m_mapping.remove_mapping(drv->m_base_table, variable_name);
    drv->m_base_table = previous_table;

    return std::unique_ptr<Subexpr>(lc.subquery(sub));
}

std::unique_ptr<Subexpr> PostOpNode::visit(ParserDriver*, Subexpr* subexpr)
{
    if (auto s = dynamic_cast<Columns<Link>*>(subexpr)) {
        return s->count().clone();
    }
    if (auto s = dynamic_cast<ColumnListBase*>(subexpr)) {
        return s->size().clone();
    }
    if (auto s = dynamic_cast<Columns<StringData>*>(subexpr)) {
        return s->size().clone();
    }
    if (auto s = dynamic_cast<Columns<BinaryData>*>(subexpr)) {
        return s->size().clone();
    }
    if (subexpr) {
        throw std::runtime_error(util::format("Operation '%1' is not supported on property of type '%2'", op_name,
                                              get_data_type_name(DataType(subexpr->get_type()))));
    }
    REALM_UNREACHABLE();
    return {};
}

std::unique_ptr<Subexpr> LinkAggrNode::visit(ParserDriver* drv)
{
    auto link_chain = path->visit(drv);
    auto subexpr = std::unique_ptr<Subexpr>(drv->column(link_chain, link));
    auto link_prop = dynamic_cast<Columns<Link>*>(subexpr.get());
    if (!link_prop) {
        throw std::runtime_error(util::format("Operation '%1' cannot apply to property '%2' because it is not a list",
                                              agg_op_type_to_str(aggr_op->type), link));
    }
    drv->translate(link_chain, prop);
    auto col_key = link_chain.get_current_table()->get_column_key(prop);

    std::unique_ptr<Subexpr> sub_column;
    switch (col_key.get_type()) {
        case col_type_Int:
            sub_column = link_prop->column<Int>(col_key).clone();
            break;
        case col_type_Float:
            sub_column = link_prop->column<float>(col_key).clone();
            break;
        case col_type_Double:
            sub_column = link_prop->column<double>(col_key).clone();
            break;
        case col_type_Decimal:
            sub_column = link_prop->column<Decimal>(col_key).clone();
            break;
        default:
            throw std::runtime_error(util::format("collection aggregate not supported for type '%1'",
                                                  get_data_type_name(DataType(col_key.get_type()))));
    }
    return aggr_op->visit(drv, sub_column.get());
}

std::unique_ptr<Subexpr> ListAggrNode::visit(ParserDriver* drv)
{
    auto link_chain = path->visit(drv);
    std::unique_ptr<Subexpr> subexpr{drv->column(link_chain, identifier)};
    return aggr_op->visit(drv, subexpr.get());
}

std::unique_ptr<Subexpr> AggrNode::visit(ParserDriver*, Subexpr* subexpr)
{
    std::unique_ptr<Subexpr> agg;
    if (auto list_prop = dynamic_cast<ColumnListBase*>(subexpr)) {
        switch (type) {
            case MAX:
                agg = list_prop->max_of();
                break;
            case MIN:
                agg = list_prop->min_of();
                break;
            case SUM:
                agg = list_prop->sum_of();
                break;
            case AVG:
                agg = list_prop->avg_of();
                break;
        }
    }
    else if (auto prop = dynamic_cast<SubColumnBase*>(subexpr)) {
        switch (type) {
            case MAX:
                agg = prop->max_of();
                break;
            case MIN:
                agg = prop->min_of();
                break;
            case SUM:
                agg = prop->sum_of();
                break;
            case AVG:
                agg = prop->avg_of();
                break;
        }
    }
    if (!agg) {
        throw std::runtime_error(
            util::format("Cannot use aggregate '%1' for this type of property", agg_op_type_to_str(type)));
    }

    return agg;
}

std::unique_ptr<Subexpr> ConstantNode::visit(ParserDriver* drv, DataType hint)
{
    Subexpr* ret = nullptr;
    switch (type) {
        case Type::NUMBER: {
            if (hint == type_Decimal) {
                ret = new Value<Decimal128>(Decimal128(text));
            }
            else {
                ret = new Value<int64_t>(strtol(text.c_str(), nullptr, 0));
            }
            break;
        }
        case Type::FLOAT: {
            switch (hint) {
                case type_Float: {
                    ret = new Value<float>(strtof(text.c_str(), nullptr));
                    break;
                }
                case type_Decimal:
                    ret = new Value<Decimal128>(Decimal128(text));
                    break;
                default:
                    ret = new Value<double>(strtod(text.c_str(), nullptr));
                    break;
            }
            break;
        }
        case Type::INFINITY_VAL: {
            bool negative = text[0] == '-';
            switch (hint) {
                case type_Float: {
                    auto inf = std::numeric_limits<float>::infinity();
                    ret = new Value<float>(negative ? -inf : inf);
                    break;
                }
                case type_Double: {
                    auto inf = std::numeric_limits<double>::infinity();
                    ret = new Value<double>(negative ? -inf : inf);
                    break;
                }
                case type_Decimal:
                    ret = new Value<Decimal128>(Decimal128(text));
                    break;
                default:
                    throw std::runtime_error(util::format("Infinity not supported for %1", get_data_type_name(hint)));
                    break;
            }
            break;
        }
        case Type::NAN_VAL: {
            switch (hint) {
                case type_Float:
                    ret = new Value<float>(type_punning<float>(0x7fc00000));
                    break;
                case type_Double:
                    ret = new Value<double>(type_punning<double>(0x7ff8000000000000));
                    break;
                case type_Decimal:
                    ret = new Value<Decimal128>(Decimal128::nan("0"));
                    break;
                default:
                    REALM_UNREACHABLE();
                    break;
            }
            break;
        }
        case Type::STRING: {
            std::string str = text.substr(1, text.size() - 2);
            switch (hint) {
                case type_Int:
                    ret = new Value<int64_t>(string_to<int64_t>(str));
                    break;
                case type_Float:
                    ret = new Value<float>(string_to<float>(str));
                    break;
                case type_Double:
                    ret = new Value<double>(string_to<double>(str));
                    break;
                case type_Decimal:
                    ret = new Value<Decimal128>(Decimal128(str.c_str()));
                    break;
                default:
                    ret = new ConstantStringValue(str);
                    break;
            }
            break;
        }
        case Type::BASE64: {
            const size_t encoded_size = text.size() - 5;
            size_t buffer_size = util::base64_decoded_size(encoded_size);
            drv->m_args.buffer_space.push_back({});
            auto& decode_buffer = drv->m_args.buffer_space.back();
            decode_buffer.resize(buffer_size);
            StringData window(text.c_str() + 4, encoded_size);
            util::Optional<size_t> decoded_size = util::base64_decode(window, decode_buffer.data(), buffer_size);
            if (!decoded_size) {
                throw std::runtime_error("Invalid base64 value");
            }
            REALM_ASSERT_DEBUG_EX(*decoded_size <= encoded_size, *decoded_size, encoded_size);
            decode_buffer.resize(*decoded_size); // truncate

            if (hint == type_String) {
                ret = new ConstantStringValue(StringData(decode_buffer.data(), decode_buffer.size()));
            }
            if (hint == type_Binary) {
                ret = new Value<BinaryData>(BinaryData(decode_buffer.data(), decode_buffer.size()));
            }
            if (hint == type_Mixed) {
                ret = new Value<BinaryData>(BinaryData(decode_buffer.data(), decode_buffer.size()));
            }
            break;
        }
        case Type::TIMESTAMP: {
            auto s = text;
            int64_t seconds;
            int32_t nanoseconds;
            if (s[0] == 'T') {
                size_t colon_pos = s.find(":");
                std::string s1 = s.substr(1, colon_pos - 1);
                std::string s2 = s.substr(colon_pos + 1);
                seconds = strtol(s1.c_str(), nullptr, 0);
                nanoseconds = int32_t(strtol(s2.c_str(), nullptr, 0));
            }
            else {
                // readable format YYYY-MM-DD-HH:MM:SS:NANOS nanos optional
                struct tm tmp = tm();
                char sep = s.find("@") < s.size() ? '@' : 'T';
                std::string fmt = "%d-%d-%d"s + sep + "%d:%d:%d:%d"s;
                int cnt = sscanf(s.c_str(), fmt.c_str(), &tmp.tm_year, &tmp.tm_mon, &tmp.tm_mday, &tmp.tm_hour,
                                 &tmp.tm_min, &tmp.tm_sec, &nanoseconds);
                REALM_ASSERT(cnt >= 6);
                tmp.tm_year -= 1900; // epoch offset (see man mktime)
                tmp.tm_mon -= 1;     // converts from 1-12 to 0-11

                if (tmp.tm_year < 0) {
                    // platform timegm functions do not throw errors, they return -1 which is also a valid time
                    throw std::logic_error("Conversion of dates before 1900 is not supported.");
                }

                seconds = platform_timegm(tmp); // UTC time
                if (cnt == 6) {
                    nanoseconds = 0;
                }
                if (nanoseconds < 0) {
                    throw std::logic_error("The nanoseconds of a Timestamp cannot be negative.");
                }
                if (seconds < 0) { // seconds determines the sign of the nanoseconds part
                    nanoseconds *= -1;
                }
            }
            ret = new Value<Timestamp>(get_timestamp_if_valid(seconds, nanoseconds));
            break;
        }
        case Type::UUID_T:
            ret = new Value<UUID>(UUID(text.substr(5, text.size() - 6)));
            break;
        case Type::OID:
            ret = new Value<ObjectId>(ObjectId(text.substr(4, text.size() - 5).c_str()));
            break;
        case Type::LINK: {
            ret = new Value<ObjKey>(ObjKey(strtol(text.substr(1, text.size() - 1).c_str(), nullptr, 0)));
            break;
        }
        case Type::NULL_VAL:
            if (hint == type_String) {
                ret = new ConstantStringValue(StringData()); // Null string
            }
            else if (hint == type_Binary) {
                ret = new Value<Binary>(BinaryData()); // Null string
            }
            else if (hint == type_LinkList) {
                throw std::runtime_error("Cannot compare linklist with NULL");
            }
            else {
                ret = new Value<null>(realm::null());
            }
            break;
        case Type::TRUE:
            ret = new Value<Bool>(true);
            break;
        case Type::FALSE:
            ret = new Value<Bool>(false);
            break;
        case Type::ARG: {
            size_t arg_no = size_t(strtol(text.substr(1).c_str(), nullptr, 10));
            if (drv->m_args.is_argument_null(arg_no)) {
                ret = new Value<null>(realm::null());
            }
            else {
                auto type = drv->m_args.type_for_argument(arg_no);
                switch (type) {
                    case type_Int:
                        ret = new Value<int64_t>(drv->m_args.long_for_argument(arg_no));
                        break;
                    case type_String:
                        ret = new ConstantStringValue(drv->m_args.string_for_argument(arg_no));
                        break;
                    case type_Binary:
                        ret = new Value<BinaryData>(drv->m_args.binary_for_argument(arg_no));
                        break;
                    case type_Bool:
                        ret = new Value<Bool>(drv->m_args.bool_for_argument(arg_no));
                        break;
                    case type_Float:
                        ret = new Value<float>(drv->m_args.float_for_argument(arg_no));
                        break;
                    case type_Double:
                        ret = new Value<double>(drv->m_args.double_for_argument(arg_no));
                        break;
                    case type_Timestamp: {
                        try {
                            ret = new Value<Timestamp>(drv->m_args.timestamp_for_argument(arg_no));
                        }
                        catch (const std::exception&) {
                            ret = new Value<ObjectId>(drv->m_args.objectid_for_argument(arg_no));
                        }
                        break;
                    }
                    case type_ObjectId: {
                        try {
                            ret = new Value<ObjectId>(drv->m_args.objectid_for_argument(arg_no));
                        }
                        catch (const std::exception&) {
                            ret = new Value<Timestamp>(drv->m_args.timestamp_for_argument(arg_no));
                        }
                        break;
                    }
                    case type_Decimal:
                        ret = new Value<Decimal128>(drv->m_args.decimal128_for_argument(arg_no));
                        break;
                    case type_UUID:
                        ret = new Value<UUID>(drv->m_args.uuid_for_argument(arg_no));
                        break;
                    default:
                        break;
                }
            }
            break;
        }
    }
    if (!ret) {
        throw std::runtime_error(
            util::format("Unsupported comparison between property of type '%1' and constant value '%2'",
                         get_data_type_name(hint), text));
    }
    return std::unique_ptr<Subexpr>{ret};
}

LinkChain PathNode::visit(ParserDriver* drv, ExpressionComparisonType comp_type)
{
    LinkChain link_chain(drv->m_base_table, comp_type);
    for (std::string path_elem : path_elems) {
        drv->translate(link_chain, path_elem);
        if (path_elem.find("@links.") == 0) {
            drv->backlink(link_chain, path_elem);
        }
        else if (path_elem.empty()) {
            continue; // this element has been removed, this happens in subqueries
        }
        else {
            link_chain.link(path_elem);
        }
    }
    return link_chain;
}

DescriptorNode::~DescriptorNode() {}

DescriptorOrderingNode::~DescriptorOrderingNode() {}

std::unique_ptr<DescriptorOrdering> DescriptorOrderingNode::visit(ParserDriver* drv)
{
    auto target = drv->m_base_table;
    std::unique_ptr<DescriptorOrdering> ordering;
    for (auto cur_ordering : orderings) {
        if (!ordering)
            ordering = std::make_unique<DescriptorOrdering>();
        if (cur_ordering->get_type() == DescriptorNode::LIMIT) {
            ordering->append_limit(LimitDescriptor(cur_ordering->limit));
        }
        else {
            bool is_distinct = cur_ordering->get_type() == DescriptorNode::DISTINCT;
            std::vector<std::vector<ColKey>> property_columns;
            for (auto& col_names : cur_ordering->columns) {
                std::vector<ColKey> columns;
                ConstTableRef cur_table = target;
                for (size_t ndx_in_path = 0; ndx_in_path < col_names.size(); ++ndx_in_path) {
                    ColKey col_key = cur_table->get_column_key(col_names[ndx_in_path]);
                    if (!col_key) {
                        throw std::runtime_error(util::format(
                            "No property '%1' found on object type '%2' specified in '%3' clause",
                            col_names[ndx_in_path], cur_table->get_name(), is_distinct ? "distinct" : "sort"));
                    }
                    columns.push_back(col_key);
                    if (ndx_in_path < col_names.size() - 1) {
                        cur_table = cur_table->get_link_target(col_key);
                    }
                }
                property_columns.push_back(columns);
            }

            if (is_distinct) {
                ordering->append_distinct(DistinctDescriptor(property_columns));
            }
            else {
                ordering->append_sort(SortDescriptor(property_columns, cur_ordering->ascending),
                                      SortDescriptor::MergeMode::prepend);
            }
        }
    }

    return ordering;
}

void verify_conditions(Subexpr* left, Subexpr* right)
{
    if (dynamic_cast<ColumnListBase*>(left) && dynamic_cast<ColumnListBase*>(right)) {
        util::serializer::SerialisationState state;
        throw std::runtime_error(
            util::format("Ordered comparison between two primitive lists is not implemented yet ('%1' and '%2')",
                         left->description(state), right->description(state)));
    }
    if (left->has_multiple_values() && right->has_multiple_values()) {
        util::serializer::SerialisationState state;
        throw std::runtime_error(util::format("Comparison between two lists is not supported ('%1' and '%2')",
                                              left->description(state), right->description(state)));
    }
}

ParserDriver::ParserDriver()
    : m_args(s_default_args)
    , m_mapping(s_default_mapping)
{
    yylex_init(&m_yyscanner);
}

ParserDriver::ParserDriver(TableRef t, Arguments& args, const query_parser::KeyPathMapping& mapping)
    : m_base_table(t)
    , m_args(args)
    , m_mapping(mapping)
{
    yylex_init(&m_yyscanner);
}

ParserDriver::~ParserDriver()
{
    yylex_destroy(m_yyscanner);
}


std::pair<std::unique_ptr<Subexpr>, std::unique_ptr<Subexpr>> ParserDriver::cmp(const std::vector<ValueNode*>& values)
{
    std::unique_ptr<Subexpr> left;
    std::unique_ptr<Subexpr> right;

    auto left_constant = values[0]->constant;
    auto right_constant = values[1]->constant;
    auto left_prop = values[0]->prop;
    auto right_prop = values[1]->prop;

    if (left_constant && right_constant) {
        throw std::runtime_error("Cannot compare two constants");
    }

    if (right_constant) {
        // Take left first - it cannot be a constant
        left = left_prop->visit(this);
        right = right_constant->visit(this, left->get_type());
    }
    else {
        right = right_prop->visit(this);
        if (left_constant) {
            left = left_constant->visit(this, right->get_type());
        }
        else {
            left = left_prop->visit(this);
        }
    }
    verify_conditions(left.get(), right.get());
    return {std::move(left), std::move(right)};
}

Subexpr* ParserDriver::column(LinkChain& link_chain, std::string identifier)
{
    translate(link_chain, identifier);

    if (identifier.find("@links.") == 0) {
        backlink(link_chain, identifier);
        return link_chain.create_subexpr<Link>(ColKey());
    }

    return link_chain.column(identifier);
}

void ParserDriver::backlink(LinkChain& link_chain, const std::string& identifier)
{
    auto table_column_pair = identifier.substr(7);
    auto dot_pos = table_column_pair.find('.');

    auto table_name = table_column_pair.substr(0, dot_pos);
    if (table_name.find(m_mapping.get_backlink_class_prefix()) != 0) {
        table_name = m_mapping.get_backlink_class_prefix() + table_name;
    }
    auto column_name = table_column_pair.substr(dot_pos + 1);
    auto origin_table = m_base_table->get_parent_group()->get_table(table_name);
    ColKey origin_column;
    if (origin_table) {
        origin_column = origin_table->get_column_key(column_name);
    }
    if (!origin_column) {
        auto current_table_name = link_chain.get_current_table()->get_name();
        throw std::runtime_error(util::format("No property '%1' found in type '%2' which links to type '%3'",
                                              column_name, get_printable_table_name(table_name),
                                              get_printable_table_name(current_table_name)));
    }
    link_chain.backlink(*origin_table, origin_column);
}

void ParserDriver::translate(LinkChain& link_chain, std::string& identifier)
{
    constexpr size_t max_substitutions_allowed = 50;
    size_t substitutions = 0;
    auto table = link_chain.get_current_table();
    auto tk = table->get_key();
    while (auto mapped = m_mapping.get_mapping(tk, identifier)) {
        if (substitutions > max_substitutions_allowed) {
            throw std::runtime_error(
                util::format("Substitution loop detected while processing '%1' -> '%2' found in type '%3'",
                             identifier, *mapped, get_printable_table_name(table->get_name())));
        }
        identifier = *mapped;
        substitutions++;
    }
}

int ParserDriver::parse(const std::string& str)
{
    // std::cout << str << std::endl;
    parse_buffer.append(str);
    parse_buffer.append("\0\0", 2);
    scan_begin(m_yyscanner, trace_scanning);
    yy::parser parse(*this, m_yyscanner);
    parse.set_debug_level(trace_parsing);
    int res = parse();
    if (parse_error) {
        std::string msg = "Invalid predicate: '" + str + "': " + error_string;
        throw std::runtime_error(msg);
    }
    return res;
}

void parse(const std::string& str)
{
    ParserDriver driver;
    driver.parse(str);
}

} // namespace query_parser

Query Table::query(const std::string& query_string, const std::vector<Mixed>& arguments) const
{
    MixedArguments args(arguments);
    return query(query_string, args, {});
}

Query Table::query(const std::string& query_string, query_parser::Arguments& args,
                   const query_parser::KeyPathMapping& mapping) const
{
    ParserDriver driver(m_own_ref, args, mapping);
    driver.parse(query_string);
    return driver.result->visit(&driver).set_ordering(driver.ordering->visit(&driver));
}

Subexpr* LinkChain::column(const std::string& col)
{
    if (!m_link_cols.empty()) {
        auto current_column = m_link_cols.back();
        if (current_column.is_dictionary()) {
            m_link_cols.pop_back();
            auto dict = new Columns<Dictionary>(current_column, m_base_table, m_link_cols, m_comparison_type);
            dict->key(col);
            return dict;
        }
    }

    auto col_key = m_current_table->get_column_key(col);
    if (!col_key) {
        throw std::runtime_error(util::format("'%1' has no property: '%2'", m_current_table->get_name(), col));
    }
    size_t list_count = 0;
    for (ColKey link_key : m_link_cols) {
        if (link_key.get_type() == col_type_LinkList || link_key.get_type() == col_type_BackLink) {
            list_count++;
        }
    }

    if (col_key.is_dictionary()) {
        return create_subexpr<Dictionary>(col_key);
    }
    else if (col_key.is_list()) {
        switch (col_key.get_type()) {
            case col_type_Int:
                return create_subexpr<Lst<Int>>(col_key);
            case col_type_Bool:
                return create_subexpr<Lst<Bool>>(col_key);
            case col_type_String:
                return create_subexpr<Lst<String>>(col_key);
            case col_type_Binary:
                return create_subexpr<Lst<Binary>>(col_key);
            case col_type_Float:
                return create_subexpr<Lst<Float>>(col_key);
            case col_type_Double:
                return create_subexpr<Lst<Double>>(col_key);
            case col_type_Timestamp:
                return create_subexpr<Lst<Timestamp>>(col_key);
            case col_type_Decimal:
                return create_subexpr<Lst<Decimal>>(col_key);
            case col_type_UUID:
                return create_subexpr<Lst<UUID>>(col_key);
            case col_type_ObjectId:
                return create_subexpr<Lst<ObjectId>>(col_key);
            case col_type_Mixed:
                return create_subexpr<Lst<Mixed>>(col_key);
            case col_type_LinkList:
                add(col_key);
                return create_subexpr<Link>(col_key);
            default:
                break;
        }
    }
    else {
        if (m_comparison_type != ExpressionComparisonType::Any && list_count == 0) {
            throw std::runtime_error(util::format("The keypath following '%1' must contain a list",
                                                  expression_cmp_type_to_str(m_comparison_type)));
        }

        switch (col_key.get_type()) {
            case col_type_Int:
                return create_subexpr<Int>(col_key);
            case col_type_Bool:
                return create_subexpr<Bool>(col_key);
            case col_type_String:
                return create_subexpr<String>(col_key);
            case col_type_Binary:
                return create_subexpr<Binary>(col_key);
            case col_type_Float:
                return create_subexpr<Float>(col_key);
            case col_type_Double:
                return create_subexpr<Double>(col_key);
            case col_type_Timestamp:
                return create_subexpr<Timestamp>(col_key);
            case col_type_Decimal:
                return create_subexpr<Decimal>(col_key);
            case col_type_UUID:
                return create_subexpr<UUID>(col_key);
            case col_type_ObjectId:
                return create_subexpr<ObjectId>(col_key);
            case col_type_Mixed:
                return create_subexpr<Mixed>(col_key);
            case col_type_Link:
                add(col_key);
                return create_subexpr<Link>(col_key);
            default:
                break;
        }
    }
    REALM_UNREACHABLE();
    return nullptr;
}

Subexpr* LinkChain::subquery(Query subquery)
{
    REALM_ASSERT(m_link_cols.size() > 0);
    auto col_key = m_link_cols.back();
    return new SubQueryCount(subquery, Columns<Link>(col_key, m_base_table, m_link_cols).link_map());
}

template <class T>
SubQuery<T> column(const Table& origin, ColKey origin_col_key, Query subquery)
{
    static_assert(std::is_same<T, BackLink>::value, "A subquery must involve a link list or backlink column");
    return SubQuery<T>(column<T>(origin, origin_col_key), std::move(subquery));
}

} // namespace realm
