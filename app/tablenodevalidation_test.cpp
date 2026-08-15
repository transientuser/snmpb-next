#include "tablenodevalidation.h"

#include <cstdio>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

SmiNode node(SmiNodekind kind)
{
    SmiNode value{};
    value.nodekind = kind;
    return value;
}
}

int main()
{
    SmiNode scalar = node(SMI_NODEKIND_SCALAR);
    SmiNode *resolved = &scalar;
    check(ResolveTableRowNode(nullptr, nullptr, &resolved) ==
              TableNodeValidation::MissingNode && resolved == nullptr,
          "unknown OID/null lookup is rejected safely");
    check(ResolveTableRowNode(nullptr, nullptr, &resolved) ==
              TableNodeValidation::MissingNode,
          "unloaded or unresolved module lookup is rejected safely");

    check(ResolveTableRowNode(&scalar, nullptr, &resolved) ==
              TableNodeValidation::WrongNodeKind,
          "non-table node is rejected");

    SmiNode table = node(SMI_NODEKIND_TABLE);
    check(ResolveTableRowNode(&table, nullptr, &resolved) ==
              TableNodeValidation::MissingRowNode,
          "table without a first child is rejected");
    SmiNode invalidChild = node(SMI_NODEKIND_SCALAR);
    check(ResolveTableRowNode(&table, &invalidChild, &resolved) ==
              TableNodeValidation::MissingRowNode,
          "table without a valid row child is rejected");

    SmiNode row = node(SMI_NODEKIND_ROW);
    check(ResolveTableRowNode(&row, nullptr, &resolved) ==
              TableNodeValidation::Valid && resolved == &row,
          "valid row path remains unchanged");
    check(ResolveTableRowNode(&table, &row, &resolved) ==
              TableNodeValidation::Valid && resolved == &row,
          "valid table resolves to its row child");

    SmiNode column = node(SMI_NODEKIND_COLUMN);
    check(IsValidTableColumnNode(&column), "valid column remains accepted");
    check(!IsValidTableColumnNode(nullptr) &&
          !IsValidTableColumnNode(&row),
          "null and non-column instance selections are rejected");
    check(IsTableQueryCapableNodeKind(SMI_NODEKIND_TABLE),
          "table kind is table-query capable");
    check(IsTableQueryCapableNodeKind(SMI_NODEKIND_ROW),
          "row kind is table-query capable");
    check(!IsTableQueryCapableNodeKind(SMI_NODEKIND_COLUMN) &&
          !IsTableQueryCapableNodeKind(SMI_NODEKIND_SCALAR),
          "columns and scalars are not default table-query targets");

    if (failures == 0)
        std::puts("All table node validation tests passed.");
    return failures == 0 ? 0 : 1;
}
