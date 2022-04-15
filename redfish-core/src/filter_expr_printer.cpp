#include "filter_expr_printer.hpp"

#include "filter_expr_parser_ast.hpp"
#include "filter_expr_parser_grammar.hpp"
#include "logging.hpp"

#include <iostream>
#include <list>
#include <numeric>
#include <optional>
#include <string>

namespace redfish
{

///////////////////////////////////////////////////////////////////////////
//  The AST Printer
//  Prints a $filter AST as a string to be compared, including explicit braces
//  around all operations to help debugging AST issues.
///////////////////////////////////////////////////////////////////////////

using result_type = std::string;
std::string FilterExpressionPrinter::operator()(double x) const
{
    return std::format("double({})", x);
}
std::string FilterExpressionPrinter::operator()(int64_t x) const
{
    return std::format("int({})", x);
}
std::string
    FilterExpressionPrinter::operator()(const filter_ast::QuotedString& x) const
{
    return std::format("quoted_string(\"{}\")", static_cast<std::string>(x));
}
std::string FilterExpressionPrinter::operator()(
    const filter_ast::UnquotedString& x) const
{
    return std::format("unquoted_string(\"{}\")", static_cast<std::string>(x));
}

std::string
    FilterExpressionPrinter::operator()(const filter_ast::LogicalNot& x) const
{
    std::string prefix;
    std::string postfix;
    if (x.isLogicalNot)
    {
        prefix = "not(";
        postfix = ")";
    }
    return std::format("{}{}{}", prefix, (*this)(x.operand), postfix);
}
std::string
    FilterExpressionPrinter::operator()(const filter_ast::LogicalOr& x) const
{
    std::string out;
    if (!x.rest.empty())
    {
        out += "(";
    }
    out += (*this)(x.first);

    for (const filter_ast::LogicalNot& oper : x.rest)
    {
        out += ") or (";
        out += (*this)(oper);
    }
    if (!x.rest.empty())
    {
        out += ")";
    }
    return out;
}

std::string
    FilterExpressionPrinter::operator()(const filter_ast::LogicalAnd& x) const
{
    std::string out;
    if (!x.rest.empty())
    {
        out += "(";
    }
    out += (*this)(x.first);

    for (const filter_ast::LogicalOr& orValue : x.rest)
    {
        out += ") and (";
        out += (*this)(orValue);
    }
    if (!x.rest.empty())
    {
        out += ")";
    }
    return out;
}

static std::string toString(filter_ast::ComparisonOpEnum rel)
{
    switch (rel)
    {
        case filter_ast::ComparisonOpEnum::GreaterThan:
            return "Greater Than";
        case filter_ast::ComparisonOpEnum::GreaterThanOrEqual:
            return "Greater Than Or Equal";
        case filter_ast::ComparisonOpEnum::LessThan:
            return "Less Than";
        case filter_ast::ComparisonOpEnum::LessThanOrEqual:
            return "Less Than Or Equal";
        case filter_ast::ComparisonOpEnum::Equals:
            return "Equals";
        case filter_ast::ComparisonOpEnum::NotEquals:
            return "Not Equal";
        default:
            return "Invalid";
    }
}

std::string
    FilterExpressionPrinter::operator()(const filter_ast::Comparison& x) const
{
    std::string left = boost::apply_visitor(*this, x.left);
    std::string right = boost::apply_visitor(*this, x.right);

    return std::format("{} {} {}", left, toString(x.token), right);
}

std::string FilterExpressionPrinter::operator()(
    const filter_ast::BooleanOp& operation) const
{
    return boost::apply_visitor(*this, operation);
}

std::optional<filter_grammar::program> parseFilter(std::string_view expr)
{
    const auto& grammar = filter_grammar::grammar;
    filter_grammar::program program;

    std::string_view::iterator iter = expr.begin();
    const std::string_view::iterator end = expr.end();
    BMCWEB_LOG_DEBUG("Parsing input string \"{}\"", expr);
    bool r = boost::spirit::x3::parse(iter, end, grammar, program);

    if (!r)
    {
        std::string rest(iter, end);

        BMCWEB_LOG_ERROR("Parsing failed stopped at \"{}\"", rest);
        return std::nullopt;
    }
    BMCWEB_LOG_DEBUG("Parsed AST: \"{}\"", FilterExpressionPrinter()(program));
    return {std::move(program)};
}
} // namespace redfish
