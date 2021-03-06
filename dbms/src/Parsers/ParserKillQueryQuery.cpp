#include <Parsers/ParserKillQueryQuery.h>
#include <Parsers/ASTKillQueryQuery.h>

#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/CommonParsers.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTTablesInSelectQuery.h>
#include <Parsers/ParserSelectQuery.h>
#include <Parsers/ExpressionElementParsers.h>
#include <Parsers/ExpressionListParsers.h>

#include <Common/typeid_cast.h>

namespace DB
{


bool ParserKillQueryQuery::parseImpl(Pos & pos, Pos end, ASTPtr & node, Pos & max_parsed_pos, Expected & expected)
{
    Pos begin = pos;
    auto query = std::make_shared<ASTKillQueryQuery>();

    ParserWhitespaceOrComments ws;

    ws.ignore(pos, end);

    if (!ParserKeyword{"KILL QUERY"}.ignore(pos, end, max_parsed_pos, expected))
        return false;

    ws.ignore(pos, end);

    if (!ParserKeyword{"WHERE"}.ignore(pos, end, max_parsed_pos, expected))
        return false;

    ws.ignore(pos, end);

    ParserExpressionWithOptionalAlias p_where_expression(false);
    if (!p_where_expression.parse(pos, end, query->where_expression, max_parsed_pos, expected))
        return false;

    ws.ignore(pos, end);

    if (ParserKeyword{"SYNC"}.ignore(pos, end))
        query->sync = true;
    else if (ParserKeyword{"ASYNC"}.ignore(pos, end))
        query->sync = false;
    else if (ParserKeyword{"TEST"}.ignore(pos, end))
        query->test = true;
    else
        expected = "[SYNC|ASYNC|TEST]";

    ws.ignore(pos, end);

    query->range = StringRange(begin, pos);

    node = std::move(query);

    return true;
}

}
